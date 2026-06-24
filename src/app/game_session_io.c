#define _POSIX_C_SOURCE 200809L

#include "app/game_session_io.h"

#include "net/diag.h"
#include "net/fault_injection.h"
#include "net/frame_codec.h"
#include "ui/log.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define GAME_SESSION_ERROR_PAYLOAD_BYTES 3U

static long long monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return ((long long)ts.tv_sec * 1000LL) +
           ((long long)ts.tv_nsec / 1000000LL);
}

int game_session_io_build_empty_frame(struct frame *frame, uint8_t type,
                                      uint8_t sequence)
{
    if (frame == NULL) {
        return -1;
    }

    frame_init(frame, type, sequence);
    return frame_set_data(frame, NULL, 0U);
}

static const char *game_session_io_error_name(uint8_t code)
{
    switch ((enum frame_error_code)code) {
    case FRAME_ERROR_INVALID_TRANSFER:
        return "transferencia-invalida";
    case FRAME_ERROR_FILE_UNAVAILABLE:
        return "arquivo-indisponivel";
    case FRAME_ERROR_FILE_READ:
        return "falha-leitura-arquivo";
    case FRAME_ERROR_FILE_WRITE:
        return "falha-escrita-arquivo";
    case FRAME_ERROR_STORAGE:
        return "armazenamento-indisponivel";
    case FRAME_ERROR_INTERNAL:
        return "erro-interno";
    case FRAME_ERROR_NONE:
    default:
        return "erro-desconhecido";
    }
}

static int game_session_io_build_error_frame(
    struct frame *frame, uint8_t sequence,
    uint8_t code, uint8_t related_type, uint8_t file_id)
{
    uint8_t payload[GAME_SESSION_ERROR_PAYLOAD_BYTES];

    if (frame == NULL || code == FRAME_ERROR_NONE) {
        return -1;
    }

    payload[0] = code;
    payload[1] = related_type;
    payload[2] = file_id;

    frame_init(frame, FRAME_TYPE_ERROR, sequence);
    return frame_set_data(frame, payload, sizeof(payload));
}

int game_session_io_log_error_frame(const struct frame *frame,
                                    const char *context)
{
    uint8_t code;
    uint8_t related_type;
    uint8_t file_id;

    if (frame == NULL || frame->type != FRAME_TYPE_ERROR ||
        frame->length != GAME_SESSION_ERROR_PAYLOAD_BYTES) {
        return -1;
    }

    code = frame->data[0];
    related_type = frame->data[1];
    file_id = frame->data[2];
    ui_log(stdout, UI_LOG_ERROR,
           "%s recebeu ERROR seq=%u codigo=%s tipo=%s file_id=%u\n",
           context == NULL ? "sessao" : context,
           (unsigned int)frame->sequence,
           game_session_io_error_name(code),
           frame_type_name(related_type), (unsigned int)file_id);
    return 0;
}

int game_session_io_format_error_frame(const struct frame *frame,
                                       char *out, size_t out_len)
{
    int written;

    if (frame == NULL || frame->type != FRAME_TYPE_ERROR ||
        frame->length != GAME_SESSION_ERROR_PAYLOAD_BYTES ||
        out == NULL || out_len == 0U) {
        return -1;
    }

    written = snprintf(out, out_len, "ERROR codigo=%s tipo=%s file_id=%u",
                       game_session_io_error_name(frame->data[0]),
                       frame_type_name(frame->data[1]),
                       (unsigned int)frame->data[2]);
    if (written < 0 || (size_t)written >= out_len) {
        return -1;
    }

    return 0;
}

int game_session_io_send_frame_to_peer(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    const struct frame *frame,
    const char *label)
{
    uint8_t payload[FRAME_MAX_WIRE_LEN];
    size_t payload_len;
    char err[256];
    char peer_text[NET_MAC_TEXT_LEN];
    int sent;

    payload_len = frame_encode(frame, payload, sizeof(payload));
    if (payload_len == 0U) {
        ui_log(stderr, UI_LOG_ERROR,
               "erro: falha ao codificar frame %s\n", label);
        return -1;
    }

    (void)net_fault_maybe_flip_bit(payload, payload_len, label);

    sent = raw_eth_send(sock, peer_mac, payload, payload_len,
                        err, sizeof(err));
    if (sent < 0) {
        ui_log(stderr, UI_LOG_ERROR, "erro: %s\n", err);
        return -1;
    }

    net_format_mac(peer_mac, peer_text);
    ui_log(stdout, UI_LOG_TX,
           "tx %s dst=%s tipo=%s seq=%u payload=%zu ethernet=%d\n",
           label, peer_text, frame_type_name(frame->type),
           (unsigned int)frame->sequence, payload_len, sent);
    return 0;
}

int game_session_io_send_control_frame(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    uint8_t type, uint8_t sequence,
    const char *label)
{
    struct frame frame;

    if (type != FRAME_TYPE_ACK && type != FRAME_TYPE_NACK) {
        return -1;
    }
    if (game_session_io_build_empty_frame(&frame, type, sequence) != 0) {
        return -1;
    }

    return game_session_io_send_frame_to_peer(sock, peer_mac, &frame, label);
}

int game_session_io_send_error_frame(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    uint8_t sequence, uint8_t code,
    uint8_t related_type, uint8_t file_id,
    const char *label)
{
    struct frame frame;

    if (game_session_io_build_error_frame(&frame, sequence, code,
                                          related_type, file_id) != 0) {
        return -1;
    }

    ui_log(stdout, UI_LOG_ERROR,
           "tx ERROR codigo=%s tipo=%s file_id=%u\n",
           game_session_io_error_name(code), frame_type_name(related_type),
           (unsigned int)file_id);
    return game_session_io_send_frame_to_peer(sock, peer_mac, &frame, label);
}

int game_session_io_receive_peer_frame(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    int timeout_ms, struct frame *out)
{
    long long deadline = monotonic_ms() + (long long)timeout_ms;

    for (;;) {
        long long now = monotonic_ms();
        int remaining;
        unsigned char src_mac[NET_MAC_LEN];
        unsigned char dst_mac[NET_MAC_LEN];
        uint8_t payload[RAW_ETH_MAX_PAYLOAD_LEN];
        size_t payload_len = 0U;
        char err[256];
        char src_text[NET_MAC_TEXT_LEN];
        int rc;

        if (now >= deadline) {
            return 0;
        }

        remaining = (int)(deadline - now);
        rc = raw_eth_recv(sock, remaining, src_mac, dst_mac, payload,
                          sizeof(payload), &payload_len, err, sizeof(err));
        if (rc < 0) {
            ui_log(stderr, UI_LOG_ERROR, "erro: %s\n", err);
            return -1;
        }
        if (rc == 0) {
            return 0;
        }

        net_format_mac(src_mac, src_text);
        if (memcmp(src_mac, peer_mac, NET_MAC_LEN) != 0) {
            ui_log(stdout, UI_LOG_WARN,
                   "rx ignorado src=%s motivo=peer-diferente\n", src_text);
            continue;
        }
        if (!frame_decode(out, payload, payload_len)) {
            ui_log(stdout, UI_LOG_NACK,
                   "rx ignorado src=%s motivo=frame-invalido-ou-crc\n",
                   src_text);
            continue;
        }

        ui_log(stdout, UI_LOG_RX,
               "rx jogo src=%s tipo=%s seq=%u payload=%u\n",
               src_text, frame_type_name(out->type),
               (unsigned int)out->sequence, (unsigned int)out->length);
        return 1;
    }
}
