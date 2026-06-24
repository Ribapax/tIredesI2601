#include "net/window.h"

#include <string.h>

static struct net_window_sender_slot *sender_find_slot(
    struct net_window_sender *sender, uint8_t sequence)
{
    if (sender == NULL) {
        return NULL;
    }

    for (size_t i = 0U; i < NET_WINDOW_MAX_SIZE; i++) {
        if (sender->slots[i].in_use &&
            !sender->slots[i].acked &&
            sender->slots[i].frame.sequence == sequence) {
            return &sender->slots[i];
        }
    }

    return NULL;
}

static const struct net_window_sender_slot *sender_find_const_slot(
    const struct net_window_sender *sender, uint8_t sequence)
{
    if (sender == NULL) {
        return NULL;
    }

    for (size_t i = 0U; i < NET_WINDOW_MAX_SIZE; i++) {
        if (sender->slots[i].in_use &&
            !sender->slots[i].acked &&
            sender->slots[i].frame.sequence == sequence) {
            return &sender->slots[i];
        }
    }

    return NULL;
}

static void sender_mark_through(struct net_window_sender *sender,
                                size_t chunk_index)
{
    if (sender == NULL) {
        return;
    }

    for (size_t i = 0U; i < NET_WINDOW_MAX_SIZE; i++) {
        struct net_window_sender_slot *slot = &sender->slots[i];

        if (slot->in_use && slot->chunk_index <= chunk_index) {
            slot->acked = 1U;
        }
    }
}

static struct net_window_sender_slot *sender_find_chunk_slot(
    struct net_window_sender *sender, size_t chunk_index)
{
    if (sender == NULL) {
        return NULL;
    }

    for (size_t i = 0U; i < NET_WINDOW_MAX_SIZE; i++) {
        if (sender->slots[i].in_use &&
            sender->slots[i].chunk_index == chunk_index) {
            return &sender->slots[i];
        }
    }

    return NULL;
}

static void sender_slide_base(struct net_window_sender *sender)
{
    for (;;) {
        struct net_window_sender_slot *slot;

        if (sender == NULL || sender->base_index >= sender->next_index) {
            return;
        }

        slot = sender_find_chunk_slot(sender, sender->base_index);
        if (slot == NULL || !slot->acked) {
            return;
        }

        memset(slot, 0, sizeof(*slot));
        sender->base_index++;
    }
}

static size_t receiver_expected_chunk_len(
    const struct net_window_receiver *receiver, size_t chunk_index)
{
    size_t offset;
    size_t remaining;

    if (receiver == NULL ||
        chunk_index >= receiver->total_chunks) {
        return 0U;
    }

    offset = chunk_index * NET_FRAGMENT_DATA_BYTES;
    if (offset >= receiver->total_bytes) {
        return 0U;
    }

    remaining = receiver->total_bytes - offset;
    return remaining > NET_FRAGMENT_DATA_BYTES ?
           NET_FRAGMENT_DATA_BYTES : remaining;
}

static int receiver_find_window_index(
    const struct net_window_receiver *receiver, uint8_t sequence,
    size_t *chunk_index)
{
    size_t end;

    if (receiver == NULL || chunk_index == NULL || !receiver->started) {
        return 0;
    }

    end = receiver->base_index + receiver->window_size;
    if (end > receiver->total_chunks) {
        end = receiver->total_chunks;
    }

    for (size_t i = receiver->base_index; i < end; i++) {
        if (net_window_sequence_for_index(receiver->base_sequence, i) ==
            sequence) {
            *chunk_index = i;
            return 1;
        }
    }

    return 0;
}

static int receiver_is_recent_duplicate(
    const struct net_window_receiver *receiver, uint8_t sequence)
{
    size_t start;

    if (receiver == NULL || !receiver->started ||
        receiver->base_index == 0U) {
        return 0;
    }

    start = receiver->base_index > receiver->window_size ?
            receiver->base_index - receiver->window_size : 0U;
    for (size_t i = start; i < receiver->base_index; i++) {
        if (net_window_sequence_for_index(receiver->base_sequence, i) ==
            sequence) {
            return 1;
        }
    }

    return 0;
}

int net_window_size_is_valid(size_t window_size)
{
    return window_size >= NET_WINDOW_MIN_SIZE &&
           window_size <= NET_WINDOW_MAX_SIZE;
}

uint8_t net_window_sequence_for_index(uint8_t base_sequence,
                                      size_t chunk_index)
{
    return (uint8_t)((base_sequence + ((chunk_index + 1U) &
                     FRAME_MAX_SEQUENCE)) & FRAME_MAX_SEQUENCE);
}

int net_window_sender_init(struct net_window_sender *sender,
                           size_t window_size, size_t total_chunks)
{
    if (sender == NULL || !net_window_size_is_valid(window_size) ||
        total_chunks == 0U) {
        return -1;
    }

    memset(sender, 0, sizeof(*sender));
    sender->window_size = window_size;
    sender->total_chunks = total_chunks;
    return 0;
}

int net_window_sender_can_queue(const struct net_window_sender *sender)
{
    if (sender == NULL) {
        return 0;
    }

    return sender->next_index < sender->total_chunks &&
           sender->next_index - sender->base_index < sender->window_size;
}

int net_window_sender_next_index(const struct net_window_sender *sender,
                                 size_t *chunk_index)
{
    if (sender == NULL || chunk_index == NULL ||
        !net_window_sender_can_queue(sender)) {
        return -1;
    }

    *chunk_index = sender->next_index;
    return 0;
}

int net_window_sender_queue(struct net_window_sender *sender,
                            const struct frame *frame,
                            size_t chunk_index)
{
    struct net_window_sender_slot *free_slot = NULL;

    if (sender == NULL || frame == NULL ||
        chunk_index != sender->next_index ||
        frame->type != FRAME_TYPE_DATA ||
        !net_window_sender_can_queue(sender)) {
        return -1;
    }

    for (size_t i = 0U; i < NET_WINDOW_MAX_SIZE; i++) {
        if (!sender->slots[i].in_use) {
            free_slot = &sender->slots[i];
            break;
        }
    }
    if (free_slot == NULL) {
        return -1;
    }

    free_slot->frame = *frame;
    free_slot->chunk_index = chunk_index;
    free_slot->in_use = 1U;
    free_slot->acked = 0U;
    sender->next_index++;
    return 0;
}

int net_window_sender_ack(struct net_window_sender *sender,
                          uint8_t sequence, size_t *chunk_index)
{
    struct net_window_sender_slot *slot = sender_find_slot(sender, sequence);

    if (sender == NULL) {
        return -1;
    }
    if (chunk_index != NULL) {
        *chunk_index = 0U;
    }
    if (slot == NULL) {
        return 0;
    }

    sender_mark_through(sender, slot->chunk_index);
    if (chunk_index != NULL) {
        *chunk_index = slot->chunk_index;
    }
    sender_slide_base(sender);
    return 1;
}

int net_window_sender_nack(struct net_window_sender *sender,
                           uint8_t sequence, const struct frame **frame,
                           size_t *chunk_index)
{
    struct net_window_sender_slot *slot = sender_find_slot(sender, sequence);

    if (sender == NULL || frame == NULL) {
        return -1;
    }
    *frame = NULL;
    if (chunk_index != NULL) {
        *chunk_index = 0U;
    }
    if (slot == NULL) {
        return 0;
    }

    if (slot->chunk_index > 0U) {
        sender_mark_through(sender, slot->chunk_index - 1U);
        sender_slide_base(sender);
    }

    *frame = &slot->frame;
    if (chunk_index != NULL) {
        *chunk_index = slot->chunk_index;
    }
    return 1;
}

int net_window_sender_has_sequence(const struct net_window_sender *sender,
                                   uint8_t sequence)
{
    return sender_find_const_slot(sender, sequence) != NULL;
}

size_t net_window_sender_unacked_count(const struct net_window_sender *sender)
{
    size_t count = 0U;

    if (sender == NULL) {
        return 0U;
    }

    for (size_t i = 0U; i < NET_WINDOW_MAX_SIZE; i++) {
        if (sender->slots[i].in_use && !sender->slots[i].acked) {
            count++;
        }
    }

    return count;
}

int net_window_sender_pending_frame(const struct net_window_sender *sender,
                                    size_t offset, const struct frame **frame,
                                    size_t *chunk_index)
{
    size_t seen = 0U;

    if (sender == NULL || frame == NULL) {
        return -1;
    }
    *frame = NULL;
    if (chunk_index != NULL) {
        *chunk_index = 0U;
    }

    for (size_t index = sender->base_index; index < sender->next_index;
         index++) {
        for (size_t slot_index = 0U; slot_index < NET_WINDOW_MAX_SIZE;
             slot_index++) {
            const struct net_window_sender_slot *slot =
                &sender->slots[slot_index];

            if (!slot->in_use || slot->acked ||
                slot->chunk_index != index) {
                continue;
            }
            if (seen == offset) {
                *frame = &slot->frame;
                if (chunk_index != NULL) {
                    *chunk_index = slot->chunk_index;
                }
                return 1;
            }
            seen++;
        }
    }

    return 0;
}

int net_window_sender_complete(const struct net_window_sender *sender)
{
    if (sender == NULL) {
        return 0;
    }

    return sender->base_index == sender->total_chunks &&
           sender->next_index == sender->total_chunks;
}

void net_window_receiver_init(struct net_window_receiver *receiver,
                              uint8_t type, size_t window_size)
{
    if (receiver == NULL) {
        return;
    }

    memset(receiver, 0, sizeof(*receiver));
    receiver->type = type;
    receiver->window_size = net_window_size_is_valid(window_size) ?
                            window_size : NET_WINDOW_MIN_SIZE;
}

int net_window_receiver_begin(struct net_window_receiver *receiver,
                              const struct frame *frame)
{
    struct net_transfer_start start;
    size_t chunks;

    if (receiver == NULL ||
        net_fragment_parse_start_frame(frame, &start) != 0 ||
        start.message_type != receiver->type) {
        return -1;
    }

    chunks = net_fragment_count(start.total_bytes);
    if (chunks == 0U) {
        return -1;
    }

    if (receiver->started) {
        return receiver->file_id == start.file_id &&
               receiver->total_bytes == start.total_bytes &&
               receiver->base_sequence == frame->sequence ? 0 : -1;
    }

    receiver->started = 1U;
    receiver->file_id = start.file_id;
    receiver->base_sequence = frame->sequence;
    receiver->total_bytes = start.total_bytes;
    receiver->delivered_bytes = 0U;
    receiver->total_chunks = chunks;
    receiver->base_index = 0U;
    return 0;
}

int net_window_receiver_accept(struct net_window_receiver *receiver,
                               const struct frame *frame,
                               enum net_window_receive_status *status)
{
    size_t chunk_index = 0U;
    size_t expected_len;
    struct net_window_receiver_slot *slot;

    if (status != NULL) {
        *status = NET_WINDOW_RX_INVALID;
    }
    if (receiver == NULL || frame == NULL || status == NULL ||
        !receiver->started || frame->type != FRAME_TYPE_DATA ||
        frame->length == 0U ||
        frame->length > NET_FRAGMENT_DATA_BYTES) {
        return -1;
    }

    if (!receiver_find_window_index(receiver, frame->sequence,
                                    &chunk_index)) {
        *status = receiver_is_recent_duplicate(receiver, frame->sequence) ?
                  NET_WINDOW_RX_DUPLICATE : NET_WINDOW_RX_INVALID;
        return 0;
    }

    expected_len = receiver_expected_chunk_len(receiver, chunk_index);
    if (expected_len == 0U || frame->length != expected_len) {
        *status = NET_WINDOW_RX_INVALID;
        return 0;
    }

    slot = &receiver->slots[chunk_index % receiver->window_size];
    if (slot->received && slot->chunk_index == chunk_index) {
        *status = NET_WINDOW_RX_DUPLICATE;
        return 0;
    }
    if (slot->received) {
        *status = NET_WINDOW_RX_INVALID;
        return 0;
    }

    memcpy(slot->data, frame->data, frame->length);
    slot->length = frame->length;
    slot->chunk_index = chunk_index;
    slot->received = 1U;
    *status = NET_WINDOW_RX_ACCEPTED;
    return 0;
}

int net_window_receiver_expected_sequence(
    const struct net_window_receiver *receiver, uint8_t *sequence)
{
    if (receiver == NULL || sequence == NULL || !receiver->started ||
        receiver->base_index >= receiver->total_chunks) {
        return -1;
    }

    *sequence = net_window_sequence_for_index(receiver->base_sequence,
                                             receiver->base_index);
    return 0;
}

int net_window_receiver_sequence_delivered(
    const struct net_window_receiver *receiver, uint8_t sequence)
{
    size_t start;

    if (receiver == NULL || !receiver->started ||
        receiver->base_index == 0U) {
        return 0;
    }

    start = receiver->base_index > receiver->window_size ?
            receiver->base_index - receiver->window_size : 0U;
    for (size_t i = start; i < receiver->base_index; i++) {
        if (net_window_sequence_for_index(receiver->base_sequence, i) ==
            sequence) {
            return 1;
        }
    }

    return 0;
}

int net_window_receiver_peek_ready(const struct net_window_receiver *receiver,
                                   const uint8_t **data, size_t *length,
                                   size_t *chunk_index)
{
    const struct net_window_receiver_slot *slot;

    if (receiver == NULL || data == NULL || length == NULL ||
        chunk_index == NULL || !receiver->started) {
        return -1;
    }
    *data = NULL;
    *length = 0U;
    *chunk_index = 0U;

    if (receiver->base_index >= receiver->total_chunks) {
        return 0;
    }

    slot = &receiver->slots[receiver->base_index % receiver->window_size];
    if (!slot->received || slot->chunk_index != receiver->base_index) {
        return 0;
    }

    *data = slot->data;
    *length = slot->length;
    *chunk_index = slot->chunk_index;
    return 1;
}

int net_window_receiver_pop_ready(struct net_window_receiver *receiver)
{
    struct net_window_receiver_slot *slot;

    if (receiver == NULL || !receiver->started) {
        return -1;
    }
    if (receiver->base_index >= receiver->total_chunks) {
        return 0;
    }

    slot = &receiver->slots[receiver->base_index % receiver->window_size];
    if (!slot->received || slot->chunk_index != receiver->base_index) {
        return 0;
    }

    receiver->delivered_bytes += (uint32_t)slot->length;
    memset(slot, 0, sizeof(*slot));
    receiver->base_index++;
    return 1;
}

int net_window_receiver_complete(const struct net_window_receiver *receiver)
{
    if (receiver == NULL || !receiver->started) {
        return 0;
    }

    return receiver->base_index == receiver->total_chunks;
}

uint8_t net_window_receiver_sequence(
    const struct net_window_receiver *receiver)
{
    if (receiver == NULL || !receiver->started) {
        return 0U;
    }

    return receiver->base_sequence;
}

uint8_t net_window_receiver_file_id(
    const struct net_window_receiver *receiver)
{
    if (receiver == NULL || !receiver->started) {
        return NET_TRANSFER_FILE_ID_NONE;
    }

    return receiver->file_id;
}

size_t net_window_receiver_total_chunks(
    const struct net_window_receiver *receiver)
{
    if (receiver == NULL || !receiver->started) {
        return 0U;
    }

    return receiver->total_chunks;
}

uint32_t net_window_receiver_delivered_bytes(
    const struct net_window_receiver *receiver)
{
    if (receiver == NULL || !receiver->started) {
        return 0U;
    }

    return receiver->delivered_bytes;
}
