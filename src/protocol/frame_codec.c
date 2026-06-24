#include "net/frame_codec.h"

#include <string.h>

#define FRAME_CRC8_POLYNOMIAL 0x07U
#define FRAME_CRC8_INITIAL 0x00U

static uint8_t frame_crc8(const uint8_t *data, size_t data_len)
{
    uint8_t crc = FRAME_CRC8_INITIAL;

    for (size_t i = 0U; i < data_len; i++) {
        crc ^= data[i];
        for (unsigned int bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x80U) != 0U) {
                crc = (uint8_t)((crc << 1U) ^ FRAME_CRC8_POLYNOMIAL);
            } else {
                crc = (uint8_t)(crc << 1U);
            }
        }
    }

    return crc;
}

size_t frame_encode(const struct frame *frame, uint8_t *out, size_t out_len)
{
    size_t wire_size = frame_wire_size(frame);
    size_t crc_len;

    if (wire_size == 0U || out == NULL || out_len < wire_size) {
        return 0U;
    }

    out[0] = frame->marker;
    out[1] = (uint8_t)((frame->length << 3) |
                       ((frame->sequence >> 3) & 0x07U));
    out[2] = (uint8_t)(((frame->sequence & 0x07U) << 5) |
                       (frame->type & 0x1fU));
    if (frame->length > 0U) {
        memcpy(out + FRAME_HEADER_BYTES, frame->data, frame->length);
    }
    crc_len = FRAME_HEADER_BYTES + frame->length;
    out[crc_len] = frame_crc8(out, crc_len);

    return wire_size;
}

bool frame_decode(struct frame *frame, const uint8_t *wire, size_t wire_len)
{
    uint8_t length;
    uint8_t sequence;
    uint8_t type;
    uint8_t received_crc;
    size_t needed;
    size_t crc_len;

    if (frame == NULL || wire == NULL || wire_len < FRAME_MIN_WIRE_LEN) {
        return false;
    }

    length = (uint8_t)(wire[1] >> 3);
    sequence = (uint8_t)(((wire[1] & 0x07U) << 3) | (wire[2] >> 5));
    type = (uint8_t)(wire[2] & 0x1fU);
    needed = FRAME_MIN_WIRE_LEN + length;
    if (wire_len < needed) {
        return false;
    }

    crc_len = FRAME_HEADER_BYTES + length;
    received_crc = wire[crc_len];
    if (received_crc != frame_crc8(wire, crc_len)) {
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    frame->marker = wire[0];
    frame->length = length;
    frame->sequence = sequence;
    frame->type = type;
    if (length > 0U) {
        memcpy(frame->data, wire + FRAME_HEADER_BYTES, length);
    }
    frame->crc = received_crc;

    return frame_has_valid_shape(frame);
}
