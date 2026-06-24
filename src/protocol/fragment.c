#include "net/fragment.h"

#include <string.h>

static uint8_t sequence_add(uint8_t base_sequence, size_t chunk_index)
{
    return (uint8_t)((base_sequence + (chunk_index & FRAME_MAX_SEQUENCE)) &
                     FRAME_MAX_SEQUENCE);
}

static uint8_t sequence_next(uint8_t sequence)
{
    return (uint8_t)((sequence + 1U) & FRAME_MAX_SEQUENCE);
}

static int is_valid_start_metadata(uint8_t message_type, uint8_t file_id,
                                   uint8_t window_size, size_t total_bytes)
{
    if (!frame_is_transfer_type(message_type) ||
        file_id > NET_TRANSFER_MAX_FILE_ID ||
        total_bytes == 0U ||
        total_bytes > NET_FRAGMENT_MAX_MESSAGE_LEN) {
        return 0;
    }
    if (window_size != NET_TRANSFER_WINDOW_NONE &&
        (window_size < NET_PROTOCOL_MIN_TRANSFER_WINDOW_SIZE ||
         window_size > NET_PROTOCOL_MAX_TRANSFER_WINDOW_SIZE)) {
        return 0;
    }

    if (!frame_is_file_type(message_type) &&
        file_id != NET_TRANSFER_FILE_ID_NONE) {
        return 0;
    }
    if (frame_is_file_type(message_type) &&
        file_id == NET_TRANSFER_FILE_ID_NONE) {
        return 0;
    }

    return 1;
}

size_t net_fragment_count(size_t data_len)
{
    size_t chunks;

    if (data_len == 0U || data_len > NET_FRAGMENT_MAX_MESSAGE_LEN) {
        return 0U;
    }

    chunks = (data_len + NET_FRAGMENT_DATA_BYTES - 1U) /
             NET_FRAGMENT_DATA_BYTES;
    if (chunks == 0U) {
        return 0U;
    }

    return chunks;
}

int net_fragment_build_start_frame(struct frame *frame, uint8_t message_type,
                                   uint8_t file_id, uint8_t sequence,
                                   size_t total_bytes)
{
    uint8_t payload[NET_TRANSFER_START_FALLBACK_DATA_BYTES];

    if (frame == NULL ||
        !is_valid_start_metadata(message_type, file_id,
                                 NET_TRANSFER_WINDOW_NONE,
                                 total_bytes)) {
        return -1;
    }

    payload[0] = (uint8_t)(file_id & 0x07U);
    payload[1] = (uint8_t)((total_bytes >> 24) & 0xffU);
    payload[2] = (uint8_t)((total_bytes >> 16) & 0xffU);
    payload[3] = (uint8_t)((total_bytes >> 8) & 0xffU);
    payload[4] = (uint8_t)(total_bytes & 0xffU);

    frame_init(frame, message_type, sequence);
    return frame_set_data(frame, payload, sizeof(payload));
}

int net_fragment_build_start_frame_with_window(
    struct frame *frame, uint8_t message_type, uint8_t file_id,
    uint8_t sequence, size_t total_bytes, size_t window_size)
{
    uint8_t payload[NET_TRANSFER_START_DATA_BYTES];

    if (frame == NULL || window_size == NET_TRANSFER_WINDOW_NONE ||
        window_size > NET_PROTOCOL_MAX_TRANSFER_WINDOW_SIZE ||
        !is_valid_start_metadata(message_type, file_id,
                                 (uint8_t)window_size, total_bytes)) {
        return -1;
    }

    payload[0] = (uint8_t)(file_id & 0x07U);
    payload[1] = (uint8_t)window_size;
    payload[2] = (uint8_t)((total_bytes >> 24) & 0xffU);
    payload[3] = (uint8_t)((total_bytes >> 16) & 0xffU);
    payload[4] = (uint8_t)((total_bytes >> 8) & 0xffU);
    payload[5] = (uint8_t)(total_bytes & 0xffU);

    frame_init(frame, message_type, sequence);
    return frame_set_data(frame, payload, sizeof(payload));
}

int net_fragment_parse_start_frame(const struct frame *frame,
                                   struct net_transfer_start *start)
{
    uint8_t message_type;
    uint8_t file_id;
    uint8_t window_size = NET_TRANSFER_WINDOW_NONE;
    uint32_t total_bytes;

    if (frame == NULL || start == NULL ||
        !frame_is_transfer_type(frame->type) ||
        frame->data[0] > NET_TRANSFER_MAX_FILE_ID) {
        return -1;
    }

    message_type = frame->type;
    file_id = frame->data[0];
    if (frame->length == NET_TRANSFER_START_FALLBACK_DATA_BYTES) {
        total_bytes = ((uint32_t)frame->data[1] << 24) |
                      ((uint32_t)frame->data[2] << 16) |
                      ((uint32_t)frame->data[3] << 8) |
                      (uint32_t)frame->data[4];
    } else if (frame->length == NET_TRANSFER_START_DATA_BYTES) {
        window_size = frame->data[1];
        total_bytes = ((uint32_t)frame->data[2] << 24) |
                      ((uint32_t)frame->data[3] << 16) |
                      ((uint32_t)frame->data[4] << 8) |
                      (uint32_t)frame->data[5];
    } else {
        return -1;
    }

    if (!is_valid_start_metadata(message_type, file_id, window_size,
                                 total_bytes)) {
        return -1;
    }

    start->message_type = message_type;
    start->file_id = file_id;
    start->window_size = window_size;
    start->total_bytes = total_bytes;
    return 0;
}

int net_fragment_build_frame(struct frame *frame, uint8_t type,
                             uint8_t base_sequence, const uint8_t *data,
                             size_t data_len, size_t chunk_index)
{
    size_t data_frames = net_fragment_count(data_len);
    size_t offset;
    size_t remaining;
    size_t chunk_len;
    uint8_t payload[FRAME_MAX_DATA_LEN];

    if (frame == NULL || data == NULL || data_frames == 0U ||
        chunk_index >= data_frames || !frame_is_transfer_type(type)) {
        return -1;
    }

    offset = (size_t)chunk_index * NET_FRAGMENT_DATA_BYTES;
    remaining = data_len - offset;
    chunk_len = remaining > NET_FRAGMENT_DATA_BYTES ?
                NET_FRAGMENT_DATA_BYTES : remaining;

    memcpy(payload, data + offset, chunk_len);

    frame_init(frame, FRAME_TYPE_DATA,
               sequence_add(base_sequence, chunk_index + 1U));
    return frame_set_data(frame, payload, chunk_len);
}

int net_fragment_build_chunk_frame(struct frame *frame, uint8_t type,
                                   uint8_t base_sequence,
                                   const uint8_t *chunk, size_t chunk_len,
                                   size_t chunk_index)
{
    if (frame == NULL || chunk == NULL || chunk_len == 0U ||
        chunk_len > NET_FRAGMENT_DATA_BYTES ||
        !frame_is_transfer_type(type)) {
        return -1;
    }

    frame_init(frame, FRAME_TYPE_DATA,
               sequence_add(base_sequence, chunk_index + 1U));
    return frame_set_data(frame, chunk, chunk_len);
}

void net_fragment_reassembly_init(struct net_fragment_reassembly *reassembly,
                                  uint8_t type)
{
    if (reassembly == NULL) {
        return;
    }

    memset(reassembly, 0, sizeof(*reassembly));
    reassembly->type = type;
}

int net_fragment_reassembly_begin(struct net_fragment_reassembly *reassembly,
                                  const struct frame *frame)
{
    struct net_transfer_start start;

    if (reassembly == NULL ||
        net_fragment_parse_start_frame(frame, &start) != 0 ||
        start.message_type != reassembly->type) {
        return -1;
    }

    if (reassembly->started) {
        return reassembly->file_id == start.file_id &&
               reassembly->total_bytes == start.total_bytes &&
               reassembly->base_sequence == frame->sequence ? 0 : -1;
    }

    reassembly->started = 1U;
    reassembly->file_id = start.file_id;
    reassembly->base_sequence = frame->sequence;
    reassembly->expected_sequence = sequence_next(frame->sequence);
    reassembly->total_bytes = start.total_bytes;
    reassembly->received_bytes = 0U;
    return 0;
}

int net_fragment_reassembly_accept(
    struct net_fragment_reassembly *reassembly,
    const struct frame *frame,
    uint8_t *out, size_t out_capacity,
    size_t *out_len, int *complete)
{
    uint32_t remaining;
    uint32_t expected_len;
    size_t chunk_len;
    size_t offset;

    if (complete != NULL) {
        *complete = 0;
    }
    if (out_len != NULL) {
        *out_len = 0U;
    }

    if (reassembly == NULL || frame == NULL || out == NULL ||
        out_len == NULL || complete == NULL || !reassembly->started ||
        frame->type != FRAME_TYPE_DATA || frame->length == 0U ||
        frame->length > NET_FRAGMENT_DATA_BYTES) {
        return -1;
    }

    if (frame->sequence != reassembly->expected_sequence ||
        reassembly->received_bytes >= reassembly->total_bytes) {
        return -1;
    }

    remaining = reassembly->total_bytes - reassembly->received_bytes;
    expected_len = remaining > NET_FRAGMENT_DATA_BYTES ?
                   NET_FRAGMENT_DATA_BYTES : remaining;
    chunk_len = frame->length;
    if (chunk_len != expected_len) {
        return -1;
    }

    offset = reassembly->received_bytes;
    if (offset > out_capacity || chunk_len > out_capacity - offset) {
        return -1;
    }

    memcpy(out + offset, frame->data, chunk_len);
    reassembly->received_bytes += (uint32_t)chunk_len;
    reassembly->expected_sequence =
        sequence_next(reassembly->expected_sequence);

    if (reassembly->received_bytes == reassembly->total_bytes) {
        *out_len = reassembly->total_bytes;
        *complete = 1;
    }

    return 0;
}

int net_fragment_reassembly_accept_chunk(
    struct net_fragment_reassembly *reassembly,
    const struct frame *frame,
    const uint8_t **chunk_data, size_t *chunk_len, int *complete)
{
    uint32_t remaining;
    uint32_t expected_len;

    if (complete != NULL) {
        *complete = 0;
    }
    if (chunk_data != NULL) {
        *chunk_data = NULL;
    }
    if (chunk_len != NULL) {
        *chunk_len = 0U;
    }

    if (reassembly == NULL || frame == NULL || chunk_data == NULL ||
        chunk_len == NULL || complete == NULL || !reassembly->started ||
        frame->type != FRAME_TYPE_DATA || frame->length == 0U ||
        frame->length > NET_FRAGMENT_DATA_BYTES) {
        return -1;
    }

    if (frame->sequence != reassembly->expected_sequence ||
        reassembly->received_bytes >= reassembly->total_bytes) {
        return -1;
    }

    remaining = reassembly->total_bytes - reassembly->received_bytes;
    expected_len = remaining > NET_FRAGMENT_DATA_BYTES ?
                   NET_FRAGMENT_DATA_BYTES : remaining;
    if (frame->length != expected_len) {
        return -1;
    }

    reassembly->received_bytes += (uint32_t)frame->length;
    reassembly->expected_sequence =
        sequence_next(reassembly->expected_sequence);

    *chunk_data = frame->data;
    *chunk_len = frame->length;
    if (reassembly->received_bytes == reassembly->total_bytes) {
        *complete = 1;
    }

    return 0;
}

uint8_t net_fragment_reassembly_sequence(
    const struct net_fragment_reassembly *reassembly)
{
    if (reassembly == NULL || !reassembly->started) {
        return 0U;
    }

    return reassembly->base_sequence;
}

uint8_t net_fragment_reassembly_file_id(
    const struct net_fragment_reassembly *reassembly)
{
    if (reassembly == NULL || !reassembly->started) {
        return NET_TRANSFER_FILE_ID_NONE;
    }

    return reassembly->file_id;
}
