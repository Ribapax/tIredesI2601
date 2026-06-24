#define _POSIX_C_SOURCE 200809L

#include "app/game_session.h"

#include "app/file_transfer.h"
#include "app/game_session_io.h"
#include "app/game_session_view_codec.h"
#include "game/movement.h"
#include "game/visibility.h"
#include "net/diag.h"
#include "net/fragment.h"
#include "net/handshake.h"
#include "net/window.h"
#include "ui/game_view.h"
#include "ui/log.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define GAME_SESSION_MAX_VIEW_BYTES GAME_SESSION_VIEW_CELL_COUNT
#define GAME_SESSION_VIEW_WINDOW_SIZE 1U
#define GAME_SESSION_FILE_RECV_MIN_TIMEOUT_MS 50
#define GAME_SESSION_FILE_ERROR_STATUS_MOVES 5U

struct game_session_timeout_state {
    unsigned int consecutive_errors;
};

struct game_session_sequence_cache {
    uint8_t sequences[NET_WINDOW_MAX_SIZE];
    size_t count;
};

struct game_session_failed_pellet {
    int active;
    struct game_position position;
    char cell;
};

struct game_session_view_chunk_source {
    const uint8_t *data;
    size_t data_len;
    uint8_t sequence;
};

struct game_session_file_chunk_source {
    FILE *file;
    uint8_t frame_type;
    uint8_t sequence;
    size_t remaining;
    int read_error;
};

typedef int (*game_session_build_chunk_fn)(void *ctx, size_t chunk_index,
                                           struct frame *frame);

enum game_session_file_status {
    GAME_SESSION_FILE_STATUS_NONE = 0,
    GAME_SESSION_FILE_STATUS_SUCCESS,
    GAME_SESSION_FILE_STATUS_ERROR
};

enum game_session_receive_status {
    GAME_SESSION_RECEIVE_STATUS_ERROR = -1,
    GAME_SESSION_RECEIVE_STATUS_VIEW = 1,
    GAME_SESSION_RECEIVE_STATUS_END = 2
};

struct game_session_client_status {
    char file_error[160];
    unsigned int file_error_moves_remaining;
    int file_error_active;
    int file_error_pending_first_render;
};

void game_session_set_ui_stream(FILE *stream)
{
    ui_game_view_set_stream(stream);
}

static uint8_t next_sequence(uint8_t sequence)
{
    return (uint8_t)((sequence + 1U) & FRAME_MAX_SEQUENCE);
}

static uint8_t sequence_add_count(uint8_t sequence, size_t count)
{
    return (uint8_t)((sequence + (count & FRAME_MAX_SEQUENCE)) &
                     FRAME_MAX_SEQUENCE);
}

static int sequence_cache_contains(
    const struct game_session_sequence_cache *cache, uint8_t sequence)
{
    if (cache == NULL) {
        return 0;
    }

    for (size_t i = 0U; i < cache->count; i++) {
        if (cache->sequences[i] == sequence) {
            return 1;
        }
    }

    return 0;
}

static int is_file_pellet_cell(char cell)
{
    return cell >= '1' && cell <= '6';
}

static int same_position(struct game_position a, struct game_position b)
{
    return a.row == b.row && a.col == b.col;
}

static void failed_pellet_set(struct game_session_failed_pellet *failed,
                              struct game_position position, char cell)
{
    if (failed == NULL || !is_file_pellet_cell(cell)) {
        return;
    }

    failed->active = 1;
    failed->position = position;
    failed->cell = cell;
}

static void failed_pellet_clear_if_matches(
    struct game_session_failed_pellet *failed,
    struct game_position position, char cell)
{
    if (failed != NULL && failed->active &&
        failed->cell == cell &&
        same_position(failed->position, position)) {
        failed->active = 0;
    }
}

static void failed_pellet_restore_if_unoccupied(
    const struct game_session_failed_pellet *failed,
    struct game_map *map)
{
    if (failed == NULL || !failed->active || map == NULL ||
        failed->position.row >= GAME_MAP_SIZE ||
        failed->position.col >= GAME_MAP_SIZE) {
        return;
    }

    if (map->cells[failed->position.row][failed->position.col] == '0') {
        map->cells[failed->position.row][failed->position.col] = failed->cell;
    }
}

static void failed_pellet_overlay_view(
    const struct game_session_failed_pellet *failed,
    const struct game_map *map,
    uint8_t *view_cells, size_t view_cells_len)
{
    size_t index;
    char map_cell;

    if (failed == NULL || !failed->active || map == NULL ||
        view_cells == NULL ||
        view_cells_len < GAME_SESSION_VIEW_CELL_COUNT ||
        failed->position.row >= GAME_MAP_SIZE ||
        failed->position.col >= GAME_MAP_SIZE) {
        return;
    }

    index = (failed->position.row * GAME_MAP_SIZE) + failed->position.col;
    map_cell = map->cells[failed->position.row][failed->position.col];
    if (view_cells[index] == (uint8_t)GAME_VISIBILITY_MASKED_CELL ||
        map_cell == 'P' ||
        game_map_is_ghost_cell(map_cell)) {
        return;
    }

    view_cells[index] = (uint8_t)failed->cell;
}

static void sequence_cache_set_message_data(
    struct game_session_sequence_cache *cache, uint8_t base_sequence,
    size_t chunks, size_t limit)
{
    size_t count;
    size_t start;

    if (cache == NULL) {
        return;
    }

    cache->count = 0U;
    if (chunks == 0U) {
        return;
    }

    count = chunks < limit ? chunks : limit;
    if (count > NET_WINDOW_MAX_SIZE) {
        count = NET_WINDOW_MAX_SIZE;
    }
    start = chunks - count;
    for (size_t i = 0U; i < count; i++) {
        cache->sequences[i] =
            net_window_sequence_for_index(base_sequence, start + i);
    }
    cache->count = count;
}

static int game_session_timeout_ms(
    const struct game_session_timeout_state *state)
{
    unsigned int groups;
    int timeout = GAME_SESSION_BASE_TIMEOUT_MS;

    if (state == NULL) {
        return timeout;
    }

    groups = state->consecutive_errors / GAME_SESSION_BACKOFF_ERROR_STEP;
    while (groups > 0U && timeout < GAME_SESSION_MAX_TIMEOUT_MS) {
        if (timeout > GAME_SESSION_MAX_TIMEOUT_MS / 2) {
            timeout = GAME_SESSION_MAX_TIMEOUT_MS;
        } else {
            timeout *= 2;
        }
        groups--;
    }

    return timeout;
}

static int game_session_file_receive_timeout_ms(
    const struct game_session_timeout_state *state, size_t file_window_size)
{
    int timeout = game_session_timeout_ms(state);
    int max_receiver_timeout = GAME_SESSION_BASE_TIMEOUT_MS / 2;

    if (file_window_size <= 1U) {
        return timeout;
    }

    timeout /= (int)file_window_size;
    if (max_receiver_timeout < GAME_SESSION_FILE_RECV_MIN_TIMEOUT_MS) {
        max_receiver_timeout = GAME_SESSION_FILE_RECV_MIN_TIMEOUT_MS;
    }
    if (timeout < GAME_SESSION_FILE_RECV_MIN_TIMEOUT_MS) {
        timeout = GAME_SESSION_FILE_RECV_MIN_TIMEOUT_MS;
    }
    if (timeout > max_receiver_timeout) {
        timeout = max_receiver_timeout;
    }

    return timeout;
}

static void game_session_record_success(
    struct game_session_timeout_state *state)
{
    if (state != NULL) {
        state->consecutive_errors = 0U;
    }
}

static void game_session_record_error(
    struct game_session_timeout_state *state, const char *label)
{
    int before;
    int after;

    if (state == NULL) {
        return;
    }

    before = game_session_timeout_ms(state);
    if (state->consecutive_errors < UINT_MAX) {
        state->consecutive_errors++;
    }
    after = game_session_timeout_ms(state);
    if (after != before ||
        (state->consecutive_errors % GAME_SESSION_BACKOFF_ERROR_STEP) == 0U) {
        ui_log(stdout, UI_LOG_TIMEOUT,
               "timeout adaptativo %s erros-seguidos=%u timeout_ms=%d\n",
               label, state->consecutive_errors, after);
    }
}

static int send_receiver_ack_for_chunk(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    const struct net_window_receiver *receiver,
    size_t chunk_index,
    const char *label)
{
    uint8_t sequence;

    if (receiver == NULL) {
        return -1;
    }

    sequence = net_window_sequence_for_index(
        net_window_receiver_sequence(receiver), chunk_index);
    return game_session_io_send_control_frame(sock, peer_mac, FRAME_TYPE_ACK,
                                              sequence, label);
}

static int send_receiver_nack_expected(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    const struct net_window_receiver *receiver,
    const char *label)
{
    uint8_t sequence = 0U;

    if (net_window_receiver_expected_sequence(receiver, &sequence) != 0) {
        return -1;
    }

    return game_session_io_send_control_frame(sock, peer_mac, FRAME_TYPE_NACK,
                                              sequence, label);
}

static int wait_for_control(const struct raw_eth_socket *sock,
                            const unsigned char peer_mac[NET_MAC_LEN],
                            uint8_t sequence,
                            struct game_session_timeout_state *timeout_state,
                            const char *timeout_label,
                            uint8_t duplicate_view_sequence,
                            int has_duplicate_view_sequence)
{
    for (;;) {
        int timeout_ms = game_session_timeout_ms(timeout_state);
        struct frame frame;
        int rc;

        rc = game_session_io_receive_peer_frame(sock, peer_mac, timeout_ms,
                                                &frame);
        if (rc < 0) {
            return -1;
        }
        if (rc == 0) {
            game_session_record_error(timeout_state, timeout_label);
            return 0;
        }
        if (frame.type == FRAME_TYPE_ERROR) {
            (void)game_session_io_log_error_frame(&frame, "controle");
            game_session_record_error(timeout_state, "controle-error");
            return -1;
        }
        if (has_duplicate_view_sequence &&
            frame.type == FRAME_TYPE_DATA &&
            frame.sequence == duplicate_view_sequence) {
            ui_log(stdout, UI_LOG_ACK,
                   "view DATA duplicado seq=%u durante espera de controle; reenviando ACK\n",
                   (unsigned int)frame.sequence);
            if (game_session_io_send_control_frame(
                    sock, peer_mac, FRAME_TYPE_ACK, frame.sequence,
                    "ack-view-data-duplicado") != 0) {
                return -1;
            }
            continue;
        }
        if (frame.sequence != sequence) {
            ui_log(stdout, UI_LOG_WARN,
                   "rx controle ignorado seq=%u esperado=%u\n",
                   (unsigned int)frame.sequence, (unsigned int)sequence);
            continue;
        }
        if (frame.type == FRAME_TYPE_ACK && frame.length == 0U) {
            return 1;
        }
        if (frame.type == FRAME_TYPE_NACK && frame.length == 0U) {
            return 2;
        }

        ui_log(stdout, UI_LOG_WARN,
               "rx controle ignorado tipo=%s seq=%u\n",
               frame_type_name(frame.type), (unsigned int)frame.sequence);
    }
}

static int wait_for_fragment_ack_or_duplicate_move(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    uint8_t view_sequence,
    uint8_t last_move_sequence,
    int has_last_move_sequence,
    int last_move_accepted,
    const char *message_label,
    struct game_session_timeout_state *timeout_state)
{
    for (;;) {
        int timeout_ms = game_session_timeout_ms(timeout_state);
        struct frame frame;
        int rc;

        rc = game_session_io_receive_peer_frame(sock, peer_mac, timeout_ms,
                                                &frame);
        if (rc < 0) {
            return -1;
        }
        if (rc == 0) {
            game_session_record_error(timeout_state, "fragment-ack-timeout");
            return 0;
        }

        if (frame.type == FRAME_TYPE_ERROR) {
            (void)game_session_io_log_error_frame(&frame, message_label);
            game_session_record_error(timeout_state, "fragment-error");
            return -1;
        }

        if (frame.sequence == view_sequence &&
            frame.type == FRAME_TYPE_ACK && frame.length == 0U) {
            return 1;
        }
        if (frame.sequence == view_sequence &&
            frame.type == FRAME_TYPE_NACK && frame.length == 0U) {
            return 2;
        }

        if (has_last_move_sequence && frame.sequence == last_move_sequence &&
            frame_is_move_type(frame.type) && frame.length == 0U) {
            ui_log(stdout, last_move_accepted ? UI_LOG_ACK : UI_LOG_NACK,
                   "movimento duplicado durante espera de %s seq=%u; reenviando %s do movimento\n",
                   message_label,
                   (unsigned int)frame.sequence,
                   last_move_accepted ? "ACK" : "NACK");
            if (game_session_io_send_control_frame(
                    sock, peer_mac,
                    last_move_accepted ? FRAME_TYPE_ACK : FRAME_TYPE_NACK,
                    frame.sequence,
                    last_move_accepted ? "ack-movimento-duplicado" :
                                         "nack-movimento-duplicado") != 0) {
                return -1;
            }
            continue;
        }

        ui_log(stdout, UI_LOG_WARN,
               "rx %s-control ignorado tipo=%s seq=%u esperado=%u\n",
               message_label,
               frame_type_name(frame.type), (unsigned int)frame.sequence,
               (unsigned int)view_sequence);
    }
}

static int wait_for_window_control_or_duplicate_move(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    const struct net_window_sender *sender,
    uint8_t last_move_sequence,
    int has_last_move_sequence,
    int last_move_accepted,
    const char *message_label,
    struct game_session_timeout_state *timeout_state,
    uint8_t *sequence,
    int *is_nack)
{
    for (;;) {
        int timeout_ms = game_session_timeout_ms(timeout_state);
        struct frame frame;
        int rc;

        rc = game_session_io_receive_peer_frame(sock, peer_mac, timeout_ms, &frame);
        if (rc < 0) {
            return -1;
        }
        if (rc == 0) {
            game_session_record_error(timeout_state, "window-ack-timeout");
            return 0;
        }

        if (frame.type == FRAME_TYPE_ERROR) {
            (void)game_session_io_log_error_frame(&frame, message_label);
            game_session_record_error(timeout_state, "window-error");
            return -1;
        }

        if ((frame.type == FRAME_TYPE_ACK ||
             frame.type == FRAME_TYPE_NACK) &&
            frame.length == 0U &&
            net_window_sender_has_sequence(sender, frame.sequence)) {
            if (sequence != NULL) {
                *sequence = frame.sequence;
            }
            if (is_nack != NULL) {
                *is_nack = frame.type == FRAME_TYPE_NACK;
            }
            return 1;
        }

        if (has_last_move_sequence && frame.sequence == last_move_sequence &&
            frame_is_move_type(frame.type) && frame.length == 0U) {
            ui_log(stdout, last_move_accepted ? UI_LOG_ACK : UI_LOG_NACK,
                   "movimento duplicado durante espera de janela %s seq=%u; reenviando %s do movimento\n",
                   message_label,
                   (unsigned int)frame.sequence,
                   last_move_accepted ? "ACK" : "NACK");
            if (game_session_io_send_control_frame(sock, peer_mac,
                                   last_move_accepted ?
                                       FRAME_TYPE_ACK : FRAME_TYPE_NACK,
                                   frame.sequence,
                                   last_move_accepted ?
                                       "ack-movimento-duplicado" :
                                       "nack-movimento-duplicado") != 0) {
                return -1;
            }
            continue;
        }

        ui_log(stdout, UI_LOG_WARN,
               "rx %s-window-control ignorado tipo=%s seq=%u\n",
               message_label, frame_type_name(frame.type),
               (unsigned int)frame.sequence);
    }
}

static int send_frame_and_wait_control_ack(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    const struct frame *frame,
    const char *label,
    const char *timeout_label,
    struct game_session_timeout_state *timeout_state,
    uint8_t duplicate_view_sequence,
    int has_duplicate_view_sequence)
{
    for (;;) {
        int control;

        if (game_session_io_send_frame_to_peer(sock, peer_mac, frame, label) != 0) {
            return -1;
        }

        control = wait_for_control(sock, peer_mac, frame->sequence,
                                   timeout_state, timeout_label,
                                   duplicate_view_sequence,
                                   has_duplicate_view_sequence);
        if (control < 0) {
            return -1;
        }
        if (control == 3) {
            return 1;
        }
        if (control == 1) {
            game_session_record_success(timeout_state);
            return 0;
        }
        if (control == 2) {
            game_session_record_error(timeout_state, "controle-nack");
            ui_log(stdout, UI_LOG_NACK,
                   "%s recebeu NACK seq=%u; retransmitindo\n",
                   label, (unsigned int)frame->sequence);
            continue;
        }

        ui_log(stdout, UI_LOG_TIMEOUT,
               "%s timeout seq=%u; retransmitindo\n",
               label, (unsigned int)frame->sequence);
    }
}

static int direction_from_frame_type(uint8_t type,
                                     enum game_direction *direction)
{
    if (direction == NULL) {
        return -1;
    }

    switch ((enum frame_type)type) {
    case FRAME_TYPE_MOVE_RIGHT:
        *direction = GAME_DIRECTION_RIGHT;
        return 0;
    case FRAME_TYPE_MOVE_LEFT:
        *direction = GAME_DIRECTION_LEFT;
        return 0;
    case FRAME_TYPE_MOVE_UP:
        *direction = GAME_DIRECTION_UP;
        return 0;
    case FRAME_TYPE_MOVE_DOWN:
        *direction = GAME_DIRECTION_DOWN;
        return 0;
    default:
        return -1;
    }
}

static int build_view_cells(struct game_map *map,
                            enum game_visibility_mode visibility_mode,
                            size_t view_size,
                            const struct game_session_failed_pellet *failed,
                            uint8_t *out, size_t out_capacity)
{
    if (map == NULL || out == NULL || view_size == 0U ||
        out_capacity < GAME_SESSION_VIEW_CELL_COUNT) {
        return -1;
    }

    if (game_visibility_build_masked_map(map, visibility_mode, view_size,
                                         out, out_capacity) != 0) {
        return -1;
    }
    failed_pellet_overlay_view(failed, map, out, out_capacity);
    return 0;
}

static int game_session_apply_server_turn(
    struct game_map *map,
    enum game_direction direction,
    const struct game_session_failed_pellet *failed,
    struct game_turn_result *result)
{
    struct game_turn_result local_result = {'\0', '\0', 0};
    char ghost_encounter = '\0';

    if (map == NULL) {
        return -1;
    }

    if (game_map_apply_direction_with_entry(
            map, direction, &local_result.pacman_entered_cell) == 0) {
        local_result.pacman_moved = 1;
        if (game_map_is_ghost_cell(local_result.pacman_entered_cell)) {
            local_result.encountered_ghost =
                local_result.pacman_entered_cell;
        }
        failed_pellet_restore_if_unoccupied(failed, map);
    }
    if (game_map_move_ghosts(map, &ghost_encounter) != 0) {
        return -1;
    }
    if (local_result.encountered_ghost == '\0') {
        local_result.encountered_ghost = ghost_encounter;
    }

    if (result != NULL) {
        *result = local_result;
    }
    return 0;
}

static int build_view_payload_from_cells(const uint8_t *cells, size_t cells_len,
                                         uint8_t *out, size_t out_capacity,
                                         size_t *out_len)
{
    if (cells == NULL || out == NULL || out_len == NULL ||
        cells_len != GAME_SESSION_VIEW_CELL_COUNT) {
        return -1;
    }

    return game_session_view_pack(cells, cells_len, out,
                                  out_capacity, out_len);
}

static size_t increased_view_size(size_t current)
{
    if (current > SIZE_MAX - 2U) {
        return current;
    }

    return current + 2U;
}

static int send_fragment_frame_and_wait_ack(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    const struct frame *frame,
    const char *label,
    const char *message_label,
    uint8_t last_move_sequence,
    int has_last_move_sequence,
    int last_move_accepted,
    struct game_session_timeout_state *timeout_state)
{
    for (;;) {
        int control;

        if (game_session_io_send_frame_to_peer(sock, peer_mac, frame, label) != 0) {
            return -1;
        }

        control = wait_for_fragment_ack_or_duplicate_move(
            sock, peer_mac, frame->sequence, last_move_sequence,
            has_last_move_sequence, last_move_accepted, message_label,
            timeout_state);
        if (control < 0) {
            return -1;
        }
        if (control == 1) {
            game_session_record_success(timeout_state);
            return 0;
        }
        if (control == 2) {
            game_session_record_error(timeout_state, "fragment-nack");
            ui_log(stdout, UI_LOG_NACK,
                   "%s recebeu NACK seq=%u; retransmitindo\n",
                   label, (unsigned int)frame->sequence);
            continue;
        }

        ui_log(stdout, UI_LOG_TIMEOUT,
               "%s timeout seq=%u; retransmitindo\n",
               label, (unsigned int)frame->sequence);
    }
}

static int send_end_and_wait_ack(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    uint8_t *server_sequence,
    uint8_t last_move_sequence,
    int has_last_move_sequence,
    int last_move_accepted,
    struct game_session_timeout_state *timeout_state)
{
    struct frame end;
    uint8_t end_sequence;

    if (server_sequence == NULL) {
        return -1;
    }

    end_sequence = next_sequence(*server_sequence);
    if (game_session_io_build_empty_frame(&end, FRAME_TYPE_END,
                                          end_sequence) != 0) {
        return -1;
    }
    if (send_fragment_frame_and_wait_ack(
            sock, peer_mac, &end, "fim-servidor", "fim-servidor",
            last_move_sequence, has_last_move_sequence, last_move_accepted,
            timeout_state) != 0) {
        return -1;
    }

    *server_sequence = end_sequence;
    return 0;
}

static int build_view_chunk(void *ctx, size_t chunk_index,
                            struct frame *frame)
{
    struct game_session_view_chunk_source *source = ctx;

    if (source == NULL) {
        return -1;
    }

    return net_fragment_build_frame(frame, FRAME_TYPE_VIEW,
                                    source->sequence, source->data,
                                    source->data_len, chunk_index);
}

static int build_file_chunk(void *ctx, size_t chunk_index,
                            struct frame *frame)
{
    struct game_session_file_chunk_source *source = ctx;
    uint8_t chunk_data[NET_FRAGMENT_DATA_BYTES];
    size_t expected;
    size_t got;

    if (source == NULL || source->file == NULL ||
        source->remaining == 0U) {
        return -1;
    }

    expected = source->remaining > NET_FRAGMENT_DATA_BYTES ?
               NET_FRAGMENT_DATA_BYTES : source->remaining;
    got = fread(chunk_data, 1U, expected, source->file);
    if (got != expected ||
        net_fragment_build_chunk_frame(frame, source->frame_type,
                                       source->sequence, chunk_data, got,
                                       chunk_index) != 0) {
        source->read_error = 1;
        return -1;
    }

    source->remaining -= got;
    return 0;
}

static int retransmit_unacked_window(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    const struct net_window_sender *sender,
    const char *label)
{
    size_t count = net_window_sender_unacked_count(sender);

    for (size_t i = 0U; i < count; i++) {
        const struct frame *frame = NULL;
        size_t chunk_index = 0U;
        int rc = net_window_sender_pending_frame(sender, i, &frame,
                                                 &chunk_index);

        if (rc < 0) {
            return -1;
        }
        if (rc == 0) {
            break;
        }
        ui_log(stdout, UI_LOG_TIMEOUT,
               "%s timeout chunk=%zu seq=%u; retransmitindo\n",
               label, chunk_index, (unsigned int)frame->sequence);
        if (game_session_io_send_frame_to_peer(sock, peer_mac, frame, label) != 0) {
            return -1;
        }
    }

    return 0;
}

static int send_fragmented_data_and_wait_ack(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    size_t chunks,
    size_t window_size,
    game_session_build_chunk_fn build_chunk,
    void *build_ctx,
    const char *label,
    const char *message_label,
    uint8_t last_move_sequence,
    int has_last_move_sequence,
    int last_move_accepted,
    struct game_session_timeout_state *timeout_state)
{
    struct net_window_sender sender;

    if (build_chunk == NULL ||
        net_window_sender_init(&sender, window_size, chunks) != 0) {
        return -1;
    }

    while (!net_window_sender_complete(&sender)) {
        while (net_window_sender_can_queue(&sender)) {
            size_t chunk_index = 0U;
            struct frame chunk;

            if (net_window_sender_next_index(&sender, &chunk_index) != 0 ||
                build_chunk(build_ctx, chunk_index, &chunk) != 0 ||
                net_window_sender_queue(&sender, &chunk, chunk_index) != 0) {
                return -1;
            }
            if (game_session_io_send_frame_to_peer(sock, peer_mac, &chunk, label) != 0) {
                return -1;
            }
        }

        if (!net_window_sender_complete(&sender)) {
            uint8_t control_sequence = 0U;
            int is_nack = 0;
            int control = wait_for_window_control_or_duplicate_move(
                sock, peer_mac, &sender, last_move_sequence,
                has_last_move_sequence, last_move_accepted, message_label,
                timeout_state, &control_sequence, &is_nack);

            if (control < 0) {
                return -1;
            }
            if (control == 0) {
                if (retransmit_unacked_window(sock, peer_mac, &sender,
                                              label) != 0) {
                    return -1;
                }
                continue;
            }

            if (is_nack) {
                const struct frame *frame = NULL;
                size_t chunk_index = 0U;
                int nack = net_window_sender_nack(&sender, control_sequence,
                                                  &frame, &chunk_index);

                if (nack < 0) {
                    return -1;
                }
                if (nack > 0) {
                    game_session_record_error(timeout_state, "window-nack");
                    ui_log(stdout, UI_LOG_NACK,
                           "%s recebeu NACK chunk=%zu seq=%u; retransmitindo\n",
                           label, chunk_index,
                           (unsigned int)control_sequence);
                    if (game_session_io_send_frame_to_peer(sock, peer_mac, frame,
                                           label) != 0) {
                        return -1;
                    }
                }
                continue;
            }

            {
                size_t chunk_index = 0U;
                int ack = net_window_sender_ack(&sender, control_sequence,
                                                &chunk_index);

                if (ack < 0) {
                    return -1;
                }
                if (ack > 0) {
                    game_session_record_success(timeout_state);
                    ui_log(stdout, UI_LOG_ACK,
                           "%s ACK chunk=%zu seq=%u janela=%zu\n",
                           label, chunk_index,
                           (unsigned int)control_sequence, window_size);
                }
            }
        }
    }

    return 0;
}

static int send_view_and_wait_ack(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    const uint8_t *view_data, size_t view_len,
    uint8_t sequence,
    uint8_t last_move_sequence,
    int has_last_move_sequence,
    int last_move_accepted,
    struct game_session_timeout_state *timeout_state)
{
    size_t chunks = net_fragment_count(view_len);
    struct frame start;
    struct game_session_view_chunk_source source;

    if (chunks == 0U ||
        net_fragment_build_start_frame_with_window(
            &start, FRAME_TYPE_VIEW, NET_TRANSFER_FILE_ID_NONE, sequence,
            view_len, GAME_SESSION_VIEW_WINDOW_SIZE) != 0) {
        return -1;
    }

    if (send_fragment_frame_and_wait_ack(
            sock, peer_mac, &start, "view-start", "view",
            last_move_sequence, has_last_move_sequence, last_move_accepted,
            timeout_state) != 0) {
        return -1;
    }

    source.data = view_data;
    source.data_len = view_len;
    source.sequence = sequence;
    if (send_fragmented_data_and_wait_ack(
            sock, peer_mac, chunks, GAME_SESSION_VIEW_WINDOW_SIZE,
            build_view_chunk, &source, "view-data", "view",
            last_move_sequence, has_last_move_sequence,
            last_move_accepted, timeout_state) != 0) {
        return -1;
    }

    ui_log(stdout, UI_LOG_VIEW,
           "view confirmada seq=%u bytes=%zu chunks=%zu\n",
           (unsigned int)sequence, view_len, chunks);
    return 0;
}

static int send_view_cells(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    const uint8_t *view_cells, size_t view_cells_len,
    uint8_t *server_sequence,
    uint8_t last_move_sequence,
    int has_last_move_sequence,
    int last_move_accepted,
    struct game_session_timeout_state *timeout_state)
{
    uint8_t view_data[GAME_SESSION_VIEW_PACKED_BYTES];
    size_t view_len = 0U;
    size_t view_data_frames;
    uint8_t view_sequence;

    if (view_cells == NULL || server_sequence == NULL) {
        return -1;
    }

    if (build_view_payload_from_cells(view_cells, view_cells_len, view_data,
                                      sizeof(view_data), &view_len) != 0) {
        return -1;
    }

    view_data_frames = net_fragment_count(view_len);
    view_sequence = next_sequence(*server_sequence);
    if (view_data_frames == 0U) {
        return -1;
    }

    if (send_view_and_wait_ack(sock, peer_mac, view_data, view_len,
                               view_sequence, last_move_sequence,
                               has_last_move_sequence, last_move_accepted,
                               timeout_state) != 0) {
        return -1;
    }

    *server_sequence = sequence_add_count(view_sequence, view_data_frames);
    return 0;
}

static int file_size_for_transfer(const char *path, size_t *out)
{
    struct stat info;

    if (path == NULL || out == NULL || stat(path, &info) != 0 ||
        info.st_size <= 0) {
        return -1;
    }
    if ((uint64_t)info.st_size > (uint64_t)NET_FRAGMENT_MAX_MESSAGE_LEN) {
        return -1;
    }

    *out = (size_t)info.st_size;
    return 0;
}

static int send_file_and_wait_ack(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    const struct game_file_transfer_metadata *metadata,
    uint8_t sequence,
    size_t file_window_size,
    uint8_t last_move_sequence,
    int has_last_move_sequence,
    int last_move_accepted,
    struct game_session_timeout_state *timeout_state,
    size_t *data_frame_count)
{
    FILE *file;
    size_t file_len;
    size_t chunks;
    struct frame start;
    struct game_session_file_chunk_source source;

    if (metadata == NULL || data_frame_count == NULL) {
        return -1;
    }
    *data_frame_count = 0U;

    if (file_size_for_transfer(metadata->source_path, &file_len) != 0) {
        if (game_session_io_send_error_frame(sock, peer_mac, sequence,
                             FRAME_ERROR_FILE_UNAVAILABLE,
                             metadata->frame_type, metadata->file_id,
                             "erro-arquivo-indisponivel") != 0) {
            return -1;
        }
        game_session_record_error(timeout_state, "arquivo-indisponivel");
        return 1;
    }

    chunks = net_fragment_count(file_len);
    if (chunks == 0U ||
        net_fragment_build_start_frame_with_window(
            &start, metadata->frame_type, metadata->file_id, sequence,
            file_len, file_window_size) != 0) {
        if (game_session_io_send_error_frame(sock, peer_mac, sequence,
                             FRAME_ERROR_INVALID_TRANSFER,
                             metadata->frame_type, metadata->file_id,
                             "erro-arquivo-metadados") != 0) {
            return -1;
        }
        game_session_record_error(timeout_state, "arquivo-metadados");
        return 1;
    }

    file = fopen(metadata->source_path, "rb");
    if (file == NULL) {
        if (game_session_io_send_error_frame(sock, peer_mac, sequence,
                             FRAME_ERROR_FILE_UNAVAILABLE,
                             metadata->frame_type, metadata->file_id,
                             "erro-arquivo-abertura") != 0) {
            return -1;
        }
        game_session_record_error(timeout_state, "arquivo-abertura");
        return 1;
    }

    ui_log(stdout, UI_LOG_FILE,
           "arquivo envio inicio nome=%s tipo=%s id=%u janela=%zu bytes=%zu chunks=%zu\n",
           metadata->file_name, frame_type_name(metadata->frame_type),
           (unsigned int)metadata->file_id, file_window_size, file_len,
           chunks);
    if (send_fragment_frame_and_wait_ack(sock, peer_mac, &start,
                                         "arquivo-start", "arquivo",
                                         last_move_sequence,
                                         has_last_move_sequence,
                                         last_move_accepted,
                                         timeout_state) != 0) {
        (void)fclose(file);
        return -1;
    }

    source.file = file;
    source.frame_type = metadata->frame_type;
    source.sequence = sequence;
    source.remaining = file_len;
    source.read_error = 0;
    if (send_fragmented_data_and_wait_ack(
            sock, peer_mac, chunks, file_window_size,
            build_file_chunk, &source, "arquivo-data", "arquivo",
            last_move_sequence, has_last_move_sequence,
            last_move_accepted, timeout_state) != 0) {
        (void)fclose(file);
        if (source.read_error &&
            game_session_io_send_error_frame(sock, peer_mac, sequence,
                             FRAME_ERROR_FILE_READ,
                             metadata->frame_type, metadata->file_id,
                             "erro-arquivo-leitura") == 0) {
            game_session_record_error(timeout_state, "arquivo-leitura");
            return 1;
        }
        return -1;
    }
    if (source.remaining != 0U) {
        (void)fclose(file);
        if (game_session_io_send_error_frame(sock, peer_mac, sequence,
                             FRAME_ERROR_FILE_READ,
                             metadata->frame_type, metadata->file_id,
                             "erro-arquivo-leitura") != 0) {
            return -1;
        }
        game_session_record_error(timeout_state, "arquivo-leitura");
        return 1;
    }

    if (fclose(file) != 0) {
        if (game_session_io_send_error_frame(sock, peer_mac, sequence,
                             FRAME_ERROR_FILE_READ,
                             metadata->frame_type, metadata->file_id,
                             "erro-arquivo-fechamento") != 0) {
            return -1;
        }
        game_session_record_error(timeout_state, "arquivo-fechamento");
        return 1;
    }

    *data_frame_count = chunks;
    ui_log(stdout, UI_LOG_FILE,
           "arquivo confirmado nome=%s seq=%u bytes=%zu chunks=%zu\n",
           metadata->file_name, (unsigned int)sequence, file_len, chunks);
    return 0;
}

static int send_file_for_cell_if_needed(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    char cell,
    uint8_t *server_sequence,
    size_t file_window_size,
    uint8_t last_move_sequence,
    int has_last_move_sequence,
    int last_move_accepted,
    struct game_session_timeout_state *timeout_state,
    enum game_session_file_status *status)
{
    struct game_file_transfer_metadata file_metadata;
    size_t file_data_frames = 0U;
    uint8_t file_sequence;
    int file_rc;

    if (status != NULL) {
        *status = GAME_SESSION_FILE_STATUS_NONE;
    }
    if (server_sequence == NULL ||
        game_file_transfer_metadata_for_cell(cell, &file_metadata) != 0) {
        return 0;
    }

    file_sequence = next_sequence(*server_sequence);
    file_rc = send_file_and_wait_ack(
        sock, peer_mac, &file_metadata, file_sequence, file_window_size,
        last_move_sequence, has_last_move_sequence, last_move_accepted,
        timeout_state, &file_data_frames);
    if (file_rc < 0) {
        ui_log(stderr, UI_LOG_ERROR,
               "erro: falha ao transmitir arquivo %s\n",
               file_metadata.source_path);
        return -1;
    }
    if (file_rc > 0) {
        *server_sequence = file_sequence;
    } else {
        *server_sequence = sequence_add_count(file_sequence,
                                              file_data_frames);
    }
    if (status != NULL) {
        *status = file_rc > 0 ? GAME_SESSION_FILE_STATUS_ERROR :
                                GAME_SESSION_FILE_STATUS_SUCCESS;
    }

    return 0;
}

static int view_side_from_length(size_t length, size_t *side)
{
    for (size_t candidate = 1U; candidate <= GAME_MAP_SIZE; candidate++) {
        size_t cells = candidate * candidate;

        if (cells == length) {
            *side = candidate;
            return 0;
        }
        if (cells > length) {
            break;
        }
    }

    return -1;
}

static void format_remote_file_error_status(const struct frame *frame,
                                            char *out, size_t out_len)
{
    char error_details[96];

    if (out == NULL || out_len == 0U) {
        return;
    }
    if (game_session_io_format_error_frame(
            frame, error_details, sizeof(error_details)) == 0) {
        (void)snprintf(out, out_len,
                       "Erro no download: %s", error_details);
    } else {
        (void)snprintf(out, out_len,
                       "Erro no download recebido do servidor");
    }
}

static void client_file_error_status_set(
    struct game_session_client_status *client_status,
    const char *status)
{
    if (client_status == NULL || status == NULL || status[0] == '\0') {
        return;
    }

    (void)snprintf(client_status->file_error,
                   sizeof(client_status->file_error), "%s", status);
    client_status->file_error_moves_remaining =
        GAME_SESSION_FILE_ERROR_STATUS_MOVES;
    client_status->file_error_active = 1;
    client_status->file_error_pending_first_render = 1;
}

static const char *client_file_error_status_text(
    const struct game_session_client_status *client_status)
{
    if (client_status == NULL ||
        !client_status->file_error_active ||
        client_status->file_error[0] == '\0') {
        return NULL;
    }

    return client_status->file_error;
}

static void client_file_error_status_after_move_render(
    struct game_session_client_status *client_status)
{
    if (client_status == NULL || !client_status->file_error_active) {
        return;
    }
    if (client_status->file_error_pending_first_render) {
        client_status->file_error_pending_first_render = 0;
        return;
    }
    if (client_status->file_error_moves_remaining > 0U) {
        client_status->file_error_moves_remaining--;
    }
    if (client_status->file_error_moves_remaining == 0U) {
        client_status->file_error_active = 0;
        client_status->file_error[0] = '\0';
    }
}

static void render_client_view_with_status(
    const uint8_t *current_view,
    size_t current_view_side,
    const struct game_session_client_status *client_status)
{
    const char *status = client_file_error_status_text(client_status);

    if (status != NULL) {
        ui_game_view_render_with_status(current_view, current_view_side,
                                        status);
        return;
    }

    ui_game_view_render(current_view, current_view_side);
}

static void render_remote_file_error_status(
    const struct frame *frame,
    const uint8_t *current_view,
    size_t current_view_len,
    size_t current_view_side,
    struct game_session_client_status *client_status)
{
    char error_status[160];

    format_remote_file_error_status(frame, error_status,
                                    sizeof(error_status));
    client_file_error_status_set(client_status, error_status);

    if (current_view == NULL ||
        current_view_len != current_view_side * current_view_side) {
        return;
    }

    ui_game_view_render_with_status(current_view, current_view_side,
                                    error_status);
}

static int receive_file_message(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    const struct frame *start_frame,
    const struct net_transfer_start *start,
    const uint8_t *current_view,
    size_t current_view_len,
    size_t current_view_side,
    size_t file_window_size,
    struct game_session_sequence_cache *recent_file_data,
    struct game_session_timeout_state *timeout_state,
    struct game_session_client_status *client_status)
{
    struct game_file_transfer_metadata metadata;
    struct net_window_receiver receiver;
    FILE *file = NULL;
    char path[GAME_FILE_TRANSFER_MAX_PATH_LEN];
    char status[160];
    size_t receive_window_size;
    size_t delivered_since_feedback = 0U;
    size_t last_delivered_index = 0U;
    int has_last_delivered = 0;

    if (start_frame == NULL || start == NULL ||
        game_file_transfer_metadata_for_message(start->message_type,
                                                start->file_id,
                                                &metadata) != 0) {
        if (start_frame != NULL) {
            (void)game_session_io_send_error_frame(sock, peer_mac, start_frame->sequence,
                                   FRAME_ERROR_INVALID_TRANSFER,
                                   start_frame->type,
                                   start == NULL ?
                                       NET_TRANSFER_FILE_ID_NONE :
                                       start->file_id,
                                   "erro-arquivo-start");
        }
        game_session_record_error(timeout_state, "arquivo-start-invalido");
        return 0;
    }

    receive_window_size = start->window_size == NET_TRANSFER_WINDOW_NONE ?
                          file_window_size : (size_t)start->window_size;
    net_window_receiver_init(&receiver, start->message_type,
                             receive_window_size);
    if (net_window_receiver_begin(&receiver, start_frame) != 0) {
        (void)game_session_io_send_error_frame(sock, peer_mac, start_frame->sequence,
                               FRAME_ERROR_INVALID_TRANSFER,
                               start_frame->type, start->file_id,
                               "erro-arquivo-start");
        game_session_record_error(timeout_state, "arquivo-start-invalido");
        return -1;
    }
    if (game_file_transfer_open_received_file(&metadata, NULL, path,
                                              sizeof(path), &file) != 0) {
        (void)game_session_io_send_error_frame(sock, peer_mac, start_frame->sequence,
                               FRAME_ERROR_STORAGE,
                               start_frame->type, start->file_id,
                               "erro-arquivo-destino");
        game_session_record_error(timeout_state, "arquivo-destino");
        return -1;
    }

    if (current_view != NULL &&
        current_view_len == current_view_side * current_view_side) {
        (void)snprintf(status, sizeof(status), "Baixando arquivo: %s",
                       metadata.file_name);
        ui_game_view_render_with_status(current_view, current_view_side,
                                        status);
    }

    if (game_session_io_send_control_frame(sock, peer_mac, FRAME_TYPE_ACK,
                           start_frame->sequence, "ack-arquivo-start") != 0) {
        (void)fclose(file);
        return -1;
    }
    ui_log(stdout, UI_LOG_FILE,
           "arquivo inicio aceito nome=%s id=%u janela=%zu bytes=%u\n",
           metadata.file_name, (unsigned int)metadata.file_id,
           receive_window_size, (unsigned int)start->total_bytes);

    for (;;) {
        struct frame frame;
        enum net_window_receive_status rx_status = NET_WINDOW_RX_INVALID;
        int timeout_ms = game_session_file_receive_timeout_ms(
            timeout_state, receive_window_size);
        int rc = game_session_io_receive_peer_frame(sock, peer_mac, timeout_ms, &frame);

        if (rc < 0) {
            (void)fclose(file);
            return -1;
        }
        if (rc == 0) {
            game_session_record_error(timeout_state, "arquivo-timeout");
            if (!net_window_receiver_complete(&receiver) &&
                send_receiver_nack_expected(sock, peer_mac, &receiver,
                                            "nack-arquivo-data-esperado") != 0) {
                (void)fclose(file);
                return -1;
            }
            continue;
        }

        if (frame_is_transfer_type(frame.type)) {
            if (frame.sequence == net_window_receiver_sequence(&receiver) &&
                net_window_receiver_begin(&receiver, &frame) == 0) {
                ui_log(stdout, UI_LOG_ACK,
                       "arquivo inicio duplicado tipo=%s seq=%u; reenviando ACK\n",
                       frame_type_name(frame.type),
                       (unsigned int)frame.sequence);
                if (game_session_io_send_control_frame(sock, peer_mac, FRAME_TYPE_ACK,
                                       frame.sequence,
                                       "ack-arquivo-start-duplicado") != 0) {
                    (void)fclose(file);
                    return -1;
                }
                continue;
            }

            (void)game_session_io_send_error_frame(sock, peer_mac, frame.sequence,
                                   FRAME_ERROR_INVALID_TRANSFER,
                                   frame.type,
                                   frame.length > 0U ? frame.data[0] :
                                       NET_TRANSFER_FILE_ID_NONE,
                                   "erro-arquivo-start");
            game_session_record_error(timeout_state, "arquivo-start-invalido");
            continue;
        }

        if (frame.type == FRAME_TYPE_ERROR) {
            (void)game_session_io_log_error_frame(&frame, "arquivo");
            render_remote_file_error_status(&frame, current_view,
                                            current_view_len,
                                            current_view_side,
                                            client_status);
            game_session_record_error(timeout_state, "arquivo-error-remoto");
            (void)fclose(file);
            return 0;
        }

        if (frame.type != FRAME_TYPE_DATA) {
            ui_log(stdout, UI_LOG_WARN,
                   "rx ignorado aguardando DATA de arquivo tipo=%s seq=%u\n",
                   frame_type_name(frame.type), (unsigned int)frame.sequence);
            continue;
        }

        if (net_window_receiver_accept(&receiver, &frame, &rx_status) != 0 ||
            rx_status == NET_WINDOW_RX_INVALID) {
            (void)send_receiver_nack_expected(sock, peer_mac, &receiver,
                                              "nack-arquivo-data");
            game_session_record_error(timeout_state, "arquivo-data-invalido");
            continue;
        }

        if (rx_status == NET_WINDOW_RX_DUPLICATE) {
            if (net_window_receiver_sequence_delivered(&receiver,
                                                       frame.sequence)) {
                ui_log(stdout, UI_LOG_ACK,
                       "arquivo DATA duplicado seq=%u; reenviando ACK\n",
                       (unsigned int)frame.sequence);
                if (game_session_io_send_control_frame(sock, peer_mac, FRAME_TYPE_ACK,
                                       frame.sequence,
                                       "ack-arquivo-data-duplicado") != 0) {
                    (void)fclose(file);
                    return -1;
                }
            } else if (send_receiver_nack_expected(
                           sock, peer_mac, &receiver,
                           "nack-arquivo-data-esperado") != 0) {
                (void)fclose(file);
                return -1;
            }
        } else {
            size_t delivered_now = 0U;

            for (;;) {
                const uint8_t *chunk_data = NULL;
                size_t chunk_len = 0U;
                size_t chunk_index = 0U;
                int ready = net_window_receiver_peek_ready(
                    &receiver, &chunk_data, &chunk_len, &chunk_index);

                if (ready < 0) {
                    (void)fclose(file);
                    return -1;
                }
                if (ready == 0) {
                    break;
                }
                if (fwrite(chunk_data, 1U, chunk_len, file) != chunk_len ||
                    net_window_receiver_pop_ready(&receiver) != 1) {
                    (void)game_session_io_send_error_frame(sock, peer_mac,
                                           net_window_receiver_sequence(
                                               &receiver),
                                           FRAME_ERROR_FILE_WRITE,
                                           start->message_type,
                                           start->file_id,
                                           "erro-arquivo-escrita");
                    (void)fclose(file);
                    return -1;
                }
                last_delivered_index = chunk_index;
                has_last_delivered = 1;
                delivered_since_feedback++;
                delivered_now++;
                ui_log(stdout, UI_LOG_FILE,
                       "arquivo chunk entregue index=%zu bytes=%zu\n",
                       chunk_index, chunk_len);
            }

            if (delivered_now == 0U) {
                if (send_receiver_nack_expected(
                        sock, peer_mac, &receiver,
                        "nack-arquivo-data-esperado") != 0) {
                    (void)fclose(file);
                    return -1;
                }
            } else if (has_last_delivered &&
                       (net_window_receiver_complete(&receiver) ||
                        delivered_since_feedback >= receive_window_size)) {
                if (send_receiver_ack_for_chunk(
                        sock, peer_mac, &receiver, last_delivered_index,
                        "ack-arquivo-data") != 0) {
                    (void)fclose(file);
                    return -1;
                }
                delivered_since_feedback = 0U;
            }
        }

        if (net_window_receiver_complete(&receiver)) {
            sequence_cache_set_message_data(
                recent_file_data, net_window_receiver_sequence(&receiver),
                net_window_receiver_total_chunks(&receiver),
                receive_window_size);
            if (fclose(file) != 0) {
                (void)game_session_io_send_error_frame(sock, peer_mac,
                                       net_window_receiver_sequence(&receiver),
                                       FRAME_ERROR_FILE_WRITE,
                                       start->message_type, start->file_id,
                                       "erro-arquivo-fechamento");
                return -1;
            }
            file = NULL;

            if (current_view != NULL &&
                current_view_len == current_view_side * current_view_side) {
                (void)snprintf(status, sizeof(status), "Arquivo baixado: %s",
                               metadata.file_name);
                ui_game_view_render_with_status(current_view,
                                                current_view_side, status);
            }
            if (game_file_transfer_open_received_path(path) != 0) {
                ui_log(stderr, UI_LOG_WARN,
                       "aviso: arquivo recebido, mas abertura falhou: %s\n",
                       path);
            }
            ui_log(stdout, UI_LOG_FILE,
                   "arquivo recebido nome=%s caminho=%s bytes=%u\n",
                   metadata.file_name, path,
                   (unsigned int)net_window_receiver_delivered_bytes(
                       &receiver));
            return 1;
        }
    }
}

static int receive_view_message(const struct raw_eth_socket *sock,
                                const unsigned char peer_mac[NET_MAC_LEN],
                                uint8_t *out, size_t out_capacity,
                                size_t *out_len, uint8_t *sequence,
                                uint8_t *last_data_sequence,
                                const uint8_t *current_view,
                                size_t current_view_len,
                                size_t current_view_side,
                                size_t file_window_size,
                                struct game_session_timeout_state *timeout_state,
                                struct game_session_client_status *client_status)
{
    struct net_window_receiver receiver;
    struct game_session_sequence_cache recent_file_data = {{0U}, 0U};
    uint8_t packed_view[GAME_SESSION_VIEW_PACKED_BYTES];

    net_window_receiver_init(&receiver, FRAME_TYPE_VIEW,
                             GAME_SESSION_VIEW_WINDOW_SIZE);

    for (;;) {
        struct frame frame;
        int rc;
        int timeout_ms = game_session_timeout_ms(timeout_state);

        rc = game_session_io_receive_peer_frame(sock, peer_mac, timeout_ms, &frame);
        if (rc < 0) {
            return -1;
        }
        if (rc == 0) {
            game_session_record_error(timeout_state, "view-timeout");
            if (receiver.started &&
                !net_window_receiver_complete(&receiver) &&
                send_receiver_nack_expected(sock, peer_mac, &receiver,
                                            "nack-view-data-esperado") != 0) {
                return -1;
            }
            continue;
        }

        if (frame.type == FRAME_TYPE_END && frame.length == 0U) {
            if (game_session_io_send_control_frame(
                    sock, peer_mac, FRAME_TYPE_ACK, frame.sequence,
                    "ack-fim-servidor") != 0) {
                return GAME_SESSION_RECEIVE_STATUS_ERROR;
            }
            ui_log(stdout, UI_LOG_GAME, "jogo encerrado pelo servidor\n");
            return GAME_SESSION_RECEIVE_STATUS_END;
        }

        if (!receiver.started) {
            struct net_transfer_start start;

            if (frame.type == FRAME_TYPE_ERROR) {
                (void)game_session_io_log_error_frame(&frame, "inicio-transferencia");
                render_remote_file_error_status(&frame, current_view,
                                                current_view_len,
                                                current_view_side,
                                                client_status);
                game_session_record_error(timeout_state,
                                          "transferencia-error-remoto");
                continue;
            }

            if (frame.type == FRAME_TYPE_DATA &&
                sequence_cache_contains(&recent_file_data, frame.sequence)) {
                ui_log(stdout, UI_LOG_ACK,
                       "arquivo DATA duplicado apos conclusao seq=%u; reenviando ACK\n",
                       (unsigned int)frame.sequence);
                if (game_session_io_send_control_frame(sock, peer_mac, FRAME_TYPE_ACK,
                                       frame.sequence,
                                       "ack-arquivo-data-duplicado") != 0) {
                    return -1;
                }
                continue;
            }

            if (!frame_is_transfer_type(frame.type)) {
                ui_log(stdout, UI_LOG_WARN,
                       "rx ignorado aguardando inicio de transferencia tipo=%s seq=%u\n",
                       frame_type_name(frame.type),
                       (unsigned int)frame.sequence);
                continue;
            }
            *sequence = frame.sequence;
            if (net_fragment_parse_start_frame(&frame, &start) != 0) {
                (void)game_session_io_send_error_frame(sock, peer_mac, frame.sequence,
                                       FRAME_ERROR_INVALID_TRANSFER,
                                       frame.type,
                                       frame.length > 0U ? frame.data[0] :
                                           NET_TRANSFER_FILE_ID_NONE,
                                       "erro-start");
                game_session_record_error(timeout_state, "start-invalido");
                continue;
            }
            if (frame_is_file_type(start.message_type)) {
                int file_rc = receive_file_message(sock, peer_mac, &frame,
                                                   &start, current_view,
                                                   current_view_len,
                                                   current_view_side,
                                                   file_window_size,
                                                   &recent_file_data,
                                                   timeout_state,
                                                   client_status);
                if (file_rc < 0) {
                    return -1;
                }
                continue;
            }
            if (start.message_type != FRAME_TYPE_VIEW ||
                start.file_id != NET_TRANSFER_FILE_ID_NONE ||
                (start.window_size != NET_TRANSFER_WINDOW_NONE &&
                start.window_size != GAME_SESSION_VIEW_WINDOW_SIZE) ||
                start.total_bytes != GAME_SESSION_VIEW_PACKED_BYTES ||
                out_capacity < GAME_SESSION_VIEW_CELL_COUNT ||
                net_window_receiver_begin(&receiver, &frame) != 0) {
                ui_log(stdout, UI_LOG_WARN,
                       "view inicio invalido seq=%u tipo=%s file_id=%u janela=%u bytes=%u esperado=%u\n",
                       (unsigned int)frame.sequence,
                       frame_type_name(start.message_type),
                       (unsigned int)start.file_id,
                       (unsigned int)start.window_size,
                       (unsigned int)start.total_bytes,
                       (unsigned int)GAME_SESSION_VIEW_PACKED_BYTES);
                (void)game_session_io_send_error_frame(sock, peer_mac, frame.sequence,
                                       FRAME_ERROR_INVALID_TRANSFER,
                                       frame.type, start.file_id,
                                       "erro-view-start");
                game_session_record_error(timeout_state, "view-start-invalido");
                continue;
            }
            if (game_session_io_send_control_frame(sock, peer_mac, FRAME_TYPE_ACK,
                                   frame.sequence, "ack-view-start") != 0) {
                return -1;
            }
            continue;
        }

        if (frame_is_transfer_type(frame.type)) {
            if (frame.sequence == net_window_receiver_sequence(&receiver) &&
                net_window_receiver_begin(&receiver, &frame) == 0) {
                ui_log(stdout, UI_LOG_ACK,
                       "view inicio duplicado tipo=%s seq=%u; reenviando ACK\n",
                       frame_type_name(frame.type),
                       (unsigned int)frame.sequence);
                if (game_session_io_send_control_frame(sock, peer_mac, FRAME_TYPE_ACK,
                                       frame.sequence,
                                       "ack-view-start-duplicado") != 0) {
                    return -1;
                }
                continue;
            }

            (void)game_session_io_send_error_frame(sock, peer_mac, frame.sequence,
                                   FRAME_ERROR_INVALID_TRANSFER,
                                   frame.type,
                                   frame.length > 0U ? frame.data[0] :
                                       NET_TRANSFER_FILE_ID_NONE,
                                   "erro-view-start");
            game_session_record_error(timeout_state, "view-start-invalido");
            continue;
        }

        if (frame.type == FRAME_TYPE_ERROR) {
            (void)game_session_io_log_error_frame(&frame, "view");
            game_session_record_error(timeout_state, "view-error-remoto");
            return -1;
        }

        if (frame.type != FRAME_TYPE_DATA) {
            ui_log(stdout, UI_LOG_WARN,
                   "rx ignorado aguardando DATA tipo=%s seq=%u\n",
                   frame_type_name(frame.type), (unsigned int)frame.sequence);
            (void)send_receiver_nack_expected(sock, peer_mac, &receiver,
                                              "nack-view-data-esperado");
            continue;
        }

        {
            enum net_window_receive_status rx_status =
                NET_WINDOW_RX_INVALID;
            size_t delivered_now = 0U;
            size_t last_delivered_index = 0U;
            int has_last_delivered = 0;

            if (net_window_receiver_accept(&receiver, &frame, &rx_status) != 0 ||
                rx_status == NET_WINDOW_RX_INVALID) {
                if (net_window_receiver_expected_sequence(&receiver,
                                                          sequence) != 0) {
                    *sequence = frame.sequence;
                }
                (void)send_receiver_nack_expected(sock, peer_mac, &receiver,
                                                  "nack-view-data");
                game_session_record_error(timeout_state,
                                          "view-data-invalido");
                continue;
            }

            if (rx_status == NET_WINDOW_RX_DUPLICATE) {
                if (net_window_receiver_sequence_delivered(&receiver,
                                                           frame.sequence)) {
                    ui_log(stdout, UI_LOG_ACK,
                           "view DATA duplicado seq=%u; reenviando ACK\n",
                           (unsigned int)frame.sequence);
                    if (game_session_io_send_control_frame(sock, peer_mac, FRAME_TYPE_ACK,
                                           frame.sequence,
                                           "ack-view-data-duplicado") != 0) {
                        return -1;
                    }
                } else if (send_receiver_nack_expected(
                               sock, peer_mac, &receiver,
                               "nack-view-data-esperado") != 0) {
                    return -1;
                }
            } else {
                for (;;) {
                    const uint8_t *chunk_data = NULL;
                    size_t chunk_len = 0U;
                    size_t chunk_index = 0U;
                    size_t offset;
                    int ready = net_window_receiver_peek_ready(
                        &receiver, &chunk_data, &chunk_len, &chunk_index);

                    if (ready < 0) {
                        return -1;
                    }
                    if (ready == 0) {
                        break;
                    }
                    offset = chunk_index * NET_FRAGMENT_DATA_BYTES;
                    if (offset > sizeof(packed_view) ||
                        chunk_len > sizeof(packed_view) - offset) {
                        return -1;
                    }
                    memcpy(packed_view + offset, chunk_data, chunk_len);
                    if (net_window_receiver_pop_ready(&receiver) != 1) {
                        return -1;
                    }
                    last_delivered_index = chunk_index;
                    has_last_delivered = 1;
                    delivered_now++;
                }

                if (delivered_now == 0U) {
                    if (send_receiver_nack_expected(
                            sock, peer_mac, &receiver,
                            "nack-view-data-esperado") != 0) {
                        return -1;
                    }
                } else if (has_last_delivered) {
                    if (send_receiver_ack_for_chunk(
                            sock, peer_mac, &receiver, last_delivered_index,
                            "ack-view-data") != 0) {
                        return -1;
                    }
                }
            }
            if (net_window_receiver_complete(&receiver)) {
                size_t chunks = net_window_receiver_total_chunks(&receiver);
                size_t packed_len = net_window_receiver_delivered_bytes(&receiver);

                *sequence = net_window_receiver_sequence(&receiver);
                if (game_session_view_unpack(packed_view, packed_len, out,
                                             out_capacity, out_len) != 0) {
                    (void)game_session_io_send_error_frame(
                        sock, peer_mac, *sequence,
                        FRAME_ERROR_INVALID_TRANSFER, FRAME_TYPE_VIEW,
                        NET_TRANSFER_FILE_ID_NONE, "erro-view-payload");
                    game_session_record_error(timeout_state,
                                              "view-payload-invalido");
                    return -1;
                }
                if (last_data_sequence != NULL && chunks > 0U) {
                    *last_data_sequence = net_window_sequence_for_index(
                        net_window_receiver_sequence(&receiver), chunks - 1U);
                }
                return GAME_SESSION_RECEIVE_STATUS_VIEW;
            }
        }
    }
}

int game_session_frame_type_from_input(const char *text, uint8_t *out)
{
    return ui_game_view_frame_type_from_input(text, out);
}

int game_session_run_server(struct handshake_server_session *session,
                            struct game_map *map,
                            enum game_visibility_mode visibility_mode,
                            size_t initial_view_size,
                            int moves_per_view_increase,
                            size_t file_window_size)
{
    uint8_t server_sequence = HANDSHAKE_INIT_SEQUENCE;
    size_t view_size = initial_view_size;
    unsigned int accepted_moves = 0U;
    uint8_t last_client_sequence = 0U;
    int has_last_client_sequence = 0;
    int last_move_accepted = 0;
    struct game_session_timeout_state timeout_state = {0U};
    struct game_session_failed_pellet failed_pellet = {0};
    char peer_text[NET_MAC_TEXT_LEN];

    if (session == NULL || session->sock.fd < 0 || map == NULL ||
        !net_window_size_is_valid(file_window_size)) {
        ui_log(stderr, UI_LOG_ERROR, "erro: sessao de servidor invalida\n");
        return 1;
    }

    net_format_mac(session->peer_mac, peer_text);
    ui_log(stdout, UI_LOG_GAME,
           "jogo servidor iniciado peer=%s diametro=%zu mapa=%ux%u\n",
           peer_text, view_size, (unsigned int)GAME_MAP_SIZE,
           (unsigned int)GAME_MAP_SIZE);

    {
        uint8_t view_cells[GAME_SESSION_VIEW_CELL_COUNT];

        if (build_view_cells(map, visibility_mode, view_size, &failed_pellet,
                             view_cells, sizeof(view_cells)) != 0) {
            ui_log(stderr, UI_LOG_ERROR,
                   "erro: falha ao montar primeira visualizacao\n");
            return 1;
        }
        if (send_view_cells(
                &session->sock, session->peer_mac, view_cells,
                sizeof(view_cells), &server_sequence,
                last_client_sequence, has_last_client_sequence,
                last_move_accepted, &timeout_state) != 0) {
            return 1;
        }
    }

    for (;;) {
        struct frame received;
        uint8_t view_cells[GAME_SESSION_VIEW_CELL_COUNT];
        enum game_direction direction;
        struct game_turn_result turn = {'\0', '\0', 0};
        int accepted = 0;
        int duplicate = 0;
        int rc;

        rc = game_session_io_receive_peer_frame(&session->sock, session->peer_mac,
                                1000, &received);
        if (rc < 0) {
            return 1;
        }
        if (rc == 0) {
            continue;
        }

        if (received.type == FRAME_TYPE_END && received.length == 0U) {
            if (game_session_io_send_control_frame(&session->sock, session->peer_mac,
                                   FRAME_TYPE_ACK, received.sequence,
                                   "ack-fim") != 0) {
                return 1;
            }
            ui_log(stdout, UI_LOG_GAME, "jogo encerrado pelo cliente\n");
            return 0;
        }

        if (received.type == FRAME_TYPE_ERROR) {
            (void)game_session_io_log_error_frame(&received, "servidor");
            game_session_record_error(&timeout_state, "cliente-error");
            continue;
        }

        if (!frame_is_move_type(received.type) || received.length != 0U) {
            (void)game_session_io_send_control_frame(&session->sock, session->peer_mac,
                                     FRAME_TYPE_NACK, received.sequence,
                                     "nack-movimento");
            game_session_record_error(&timeout_state, "movimento-invalido");
            continue;
        }

        duplicate = has_last_client_sequence &&
                    last_client_sequence == received.sequence;
        if (duplicate) {
            accepted = last_move_accepted;
            ui_log(stdout, accepted ? UI_LOG_ACK : UI_LOG_NACK,
                   "movimento duplicado seq=%u reaproveitando resposta=%s\n",
                   (unsigned int)received.sequence,
                   accepted ? "ACK" : "NACK");
        } else if (direction_from_frame_type(received.type, &direction) == 0) {
            accepted = 1;
            accepted_moves++;
            last_client_sequence = received.sequence;
            has_last_client_sequence = 1;
            last_move_accepted = 1;
            if (game_session_apply_server_turn(
                    map, direction, &failed_pellet, &turn) != 0) {
                ui_log(stderr, UI_LOG_ERROR,
                       "erro: falha ao aplicar turno seq=%u\n",
                       (unsigned int)received.sequence);
                return 1;
            }
            if (!turn.pacman_moved) {
                ui_log(stdout, UI_LOG_GAME,
                       "movimento valido sem deslocamento seq=%u\n",
                       (unsigned int)received.sequence);
            }
        } else {
            accepted = 0;
            last_client_sequence = received.sequence;
            has_last_client_sequence = 1;
            last_move_accepted = 0;
        }

        if (accepted) {
            game_session_record_success(&timeout_state);
        } else {
            game_session_record_error(&timeout_state, "movimento-recusado");
        }

        if (game_session_io_send_control_frame(&session->sock, session->peer_mac,
                               accepted ? FRAME_TYPE_ACK : FRAME_TYPE_NACK,
                               received.sequence,
                               accepted ? "ack-movimento" :
                                          "nack-movimento") != 0) {
            return 1;
        }

        if (!game_visibility_all_seen(map) &&
            accepted && !duplicate && moves_per_view_increase > 0 &&
            (accepted_moves % (unsigned int)moves_per_view_increase) == 0U) {
            size_t next_view_size = increased_view_size(view_size);

            if (next_view_size != view_size) {
                view_size = next_view_size;
                ui_log(stdout, UI_LOG_VIEW,
                       "visualizacao aumentada movimentos=%u diametro=%zu\n",
                       accepted_moves, view_size);
            }
        }

        if (accepted && !duplicate) {
            enum game_session_file_status file_status =
                GAME_SESSION_FILE_STATUS_NONE;
            struct game_position file_position = map->pacman;

            if (send_file_for_cell_if_needed(
                    &session->sock, session->peer_mac,
                    turn.pacman_entered_cell, &server_sequence,
                    file_window_size, last_client_sequence,
                    has_last_client_sequence, last_move_accepted,
                    &timeout_state, &file_status) != 0) {
                return 1;
            }
            if (is_file_pellet_cell(turn.pacman_entered_cell)) {
                if (file_status == GAME_SESSION_FILE_STATUS_ERROR) {
                    failed_pellet_set(&failed_pellet, file_position,
                                      turn.pacman_entered_cell);
                } else if (file_status == GAME_SESSION_FILE_STATUS_SUCCESS) {
                    failed_pellet_clear_if_matches(
                        &failed_pellet, file_position,
                        turn.pacman_entered_cell);
                }
            }
            if (turn.encountered_ghost != '\0' &&
                turn.encountered_ghost != turn.pacman_entered_cell) {
                enum game_session_file_status ghost_file_status =
                    GAME_SESSION_FILE_STATUS_NONE;

                if (send_file_for_cell_if_needed(
                        &session->sock, session->peer_mac,
                        turn.encountered_ghost, &server_sequence,
                        file_window_size, last_client_sequence,
                        has_last_client_sequence, last_move_accepted,
                        &timeout_state, &ghost_file_status) != 0) {
                    return 1;
                }
            }
            if (turn.encountered_ghost != '\0') {
                if (send_end_and_wait_ack(
                        &session->sock, session->peer_mac,
                        &server_sequence, last_client_sequence,
                        has_last_client_sequence, last_move_accepted,
                        &timeout_state) != 0) {
                    return 1;
                }
                ui_log(stdout, UI_LOG_GAME,
                       "jogo encerrado por encontro com fantasma\n");
                return 0;
            }
        }

        failed_pellet_restore_if_unoccupied(&failed_pellet, map);
        if (build_view_cells(map, visibility_mode, view_size, &failed_pellet,
                             view_cells, sizeof(view_cells)) != 0) {
            ui_log(stderr, UI_LOG_ERROR,
                   "erro: falha ao montar visualizacao\n");
            return 1;
        }
        if (send_view_cells(
                &session->sock, session->peer_mac, view_cells,
                sizeof(view_cells), &server_sequence,
                last_client_sequence, has_last_client_sequence,
                last_move_accepted, &timeout_state) != 0) {
            return 1;
        }
    }
}

int game_session_run_client(struct handshake_client_session *session,
                            size_t file_window_size)
{
    uint8_t move_sequence = 0U;
    uint8_t last_view_data_sequence = 0U;
    uint8_t current_view[GAME_SESSION_MAX_VIEW_BYTES];
    size_t current_view_len = 0U;
    size_t view_side = 0U;
    int has_last_view_data_sequence = 0;
    struct game_session_timeout_state timeout_state = {0U};
    struct game_session_client_status client_status = {{0}, 0U, 0, 0};
    char peer_text[NET_MAC_TEXT_LEN];

    if (session == NULL || session->sock.fd < 0 ||
        !net_window_size_is_valid(file_window_size)) {
        ui_log(stderr, UI_LOG_ERROR, "erro: sessao de cliente invalida\n");
        return 1;
    }
    if (session->init_frame.type != FRAME_TYPE_INIT ||
        session->init_frame.length != 0U) {
        ui_log(stderr, UI_LOG_ERROR,
               "erro: INIT de handshake deve ser vazio\n");
        return 1;
    }

    net_format_mac(session->peer_mac, peer_text);
    ui_log(stdout, UI_LOG_GAME, "jogo cliente iniciado servidor=%s\n",
           peer_text);

    {
        uint8_t view_data[GAME_SESSION_MAX_VIEW_BYTES];
        size_t view_len = 0U;
        uint8_t view_sequence = 0U;
        uint8_t view_data_sequence = 0U;
        int rc = receive_view_message(&session->sock, session->peer_mac,
                                      view_data, sizeof(view_data),
                                      &view_len, &view_sequence,
                                      &view_data_sequence, NULL, 0U, 0U,
                                      file_window_size,
                                      &timeout_state, &client_status);

        if (rc < 0) {
            return 1;
        }
        if (rc == GAME_SESSION_RECEIVE_STATUS_END) {
            printf("sessao encerrada pelo servidor\n");
            return 0;
        }
        if (view_len != GAME_SESSION_MAX_VIEW_BYTES ||
            view_side_from_length(view_len, &view_side) != 0 ||
            view_side != GAME_MAP_SIZE) {
            game_session_record_error(&timeout_state,
                                      "view-inicial-tamanho-invalido");
            return 1;
        }

        game_session_record_success(&timeout_state);
        last_view_data_sequence = view_data_sequence;
        has_last_view_data_sequence = 1;
        memcpy(current_view, view_data, view_len);
        current_view_len = view_len;
        move_sequence = next_sequence(last_view_data_sequence);
        render_client_view_with_status(current_view, view_side,
                                       &client_status);
    }

    for (;;) {
        char input[32];
        uint8_t frame_type;
        struct frame move;
        int input_rc;

        printf("Movimento [w/a/s/d ou setas, q encerra]: ");
        fflush(stdout);
        input_rc = ui_game_view_read_key(input, sizeof(input));
        if (input_rc < 0) {
            ui_log(stderr, UI_LOG_ERROR, "erro: falha ao ler teclado\n");
            return 1;
        }
        if (input_rc == 0) {
            return 0;
        }
        if (game_session_frame_type_from_input(input, &frame_type) != 0) {
            printf("movimento invalido\n");
            continue;
        }

        if (game_session_io_build_empty_frame(&move, frame_type, move_sequence) != 0) {
            ui_log(stderr, UI_LOG_ERROR, "erro: falha ao montar movimento\n");
            return 1;
        }

        if (send_frame_and_wait_control_ack(
                &session->sock, session->peer_mac, &move,
                frame_type == FRAME_TYPE_END ? "fim" : "movimento",
                "movimento-timeout", &timeout_state,
                last_view_data_sequence,
                has_last_view_data_sequence) != 0) {
            return 1;
        }
        if (frame_type == FRAME_TYPE_END) {
            printf("sessao encerrada\n");
            return 0;
        }

        printf("movimento aceito seq=%u\n", (unsigned int)move.sequence);

        {
            uint8_t view_data[GAME_SESSION_MAX_VIEW_BYTES];
            size_t view_len = 0U;
            uint8_t view_sequence = 0U;
            uint8_t view_data_sequence = 0U;
            int rc = receive_view_message(&session->sock, session->peer_mac,
                                          view_data, sizeof(view_data),
                                          &view_len, &view_sequence,
                                          &view_data_sequence,
                                          current_view, current_view_len,
                                          view_side,
                                          file_window_size,
                                          &timeout_state, &client_status);

            if (rc < 0) {
                return 1;
            }
            if (rc == GAME_SESSION_RECEIVE_STATUS_END) {
                printf("sessao encerrada pelo servidor\n");
                return 0;
            }
            if (view_side_from_length(view_len, &view_side) != 0) {
                game_session_record_error(&timeout_state,
                                          "view-tamanho-invalido");
                return 1;
            }

            game_session_record_success(&timeout_state);
            last_view_data_sequence = view_data_sequence;
            has_last_view_data_sequence = 1;
            memcpy(current_view, view_data, view_len);
            current_view_len = view_len;
            move_sequence = next_sequence(last_view_data_sequence);
            render_client_view_with_status(current_view, view_side,
                                           &client_status);
            client_file_error_status_after_move_render(&client_status);
        }
    }
}
