#include "net/frame.h"

#include <string.h>

void frame_init(struct frame *frame, uint8_t type, uint8_t sequence)
{
    if (frame == NULL) {
        return;
    }

    memset(frame, 0, sizeof(*frame));
    frame->marker = FRAME_MARKER;
    frame->type = type;
    frame->sequence = sequence;
}

int frame_set_data(struct frame *frame, const uint8_t *data, size_t length)
{
    if (frame == NULL || length > FRAME_MAX_DATA_LEN) {
        return -1;
    }
    if (length > 0U && data == NULL) {
        return -1;
    }

    memset(frame->data, 0, sizeof(frame->data));
    if (length > 0U) {
        memcpy(frame->data, data, length);
    }
    frame->length = (uint8_t)length;
    return 0;
}

bool frame_is_known_type(uint8_t type)
{
    switch ((enum frame_type)type) {
    case FRAME_TYPE_ACK:
    case FRAME_TYPE_NACK:
    case FRAME_TYPE_VIEW:
    case FRAME_TYPE_INIT:
    case FRAME_TYPE_DATA:
    case FRAME_TYPE_FILE_TXT:
    case FRAME_TYPE_FILE_JPG:
    case FRAME_TYPE_FILE_MP4:
    case FRAME_TYPE_MOVE_RIGHT:
    case FRAME_TYPE_MOVE_LEFT:
    case FRAME_TYPE_MOVE_UP:
    case FRAME_TYPE_MOVE_DOWN:
    case FRAME_TYPE_ERROR:
    case FRAME_TYPE_END:
        return true;
    default:
        return false;
    }
}

bool frame_is_file_type(uint8_t type)
{
    return type == FRAME_TYPE_FILE_TXT ||
           type == FRAME_TYPE_FILE_JPG ||
           type == FRAME_TYPE_FILE_MP4;
}

bool frame_is_transfer_type(uint8_t type)
{
    return type == FRAME_TYPE_VIEW || frame_is_file_type(type);
}

bool frame_is_move_type(uint8_t type)
{
    return type == FRAME_TYPE_MOVE_RIGHT ||
           type == FRAME_TYPE_MOVE_LEFT ||
           type == FRAME_TYPE_MOVE_UP ||
           type == FRAME_TYPE_MOVE_DOWN;
}

const char *frame_type_name(uint8_t type)
{
    switch ((enum frame_type)type) {
    case FRAME_TYPE_ACK:
        return "ack";
    case FRAME_TYPE_NACK:
        return "nack";
    case FRAME_TYPE_VIEW:
        return "visualizacao";
    case FRAME_TYPE_INIT:
        return "inicializacao";
    case FRAME_TYPE_DATA:
        return "dados";
    case FRAME_TYPE_FILE_TXT:
        return "arquivo-txt";
    case FRAME_TYPE_FILE_JPG:
        return "arquivo-jpg";
    case FRAME_TYPE_FILE_MP4:
        return "arquivo-mp4";
    case FRAME_TYPE_MOVE_RIGHT:
        return "direita";
    case FRAME_TYPE_MOVE_LEFT:
        return "esquerda";
    case FRAME_TYPE_MOVE_UP:
        return "cima";
    case FRAME_TYPE_MOVE_DOWN:
        return "baixo";
    case FRAME_TYPE_ERROR:
        return "erro";
    case FRAME_TYPE_END:
        return "fim";
    default:
        return "desconhecido";
    }
}

const char *frame_file_extension(uint8_t type)
{
    switch ((enum frame_type)type) {
    case FRAME_TYPE_FILE_TXT:
        return ".txt";
    case FRAME_TYPE_FILE_JPG:
        return ".jpg";
    case FRAME_TYPE_FILE_MP4:
        return ".mp4";
    default:
        return "";
    }
}

bool frame_has_valid_shape(const struct frame *frame)
{
    if (frame == NULL) {
        return false;
    }

    if (frame->marker != FRAME_MARKER) {
        return false;
    }
    if (frame->length > FRAME_MAX_DATA_LEN) {
        return false;
    }
    if (frame->sequence > FRAME_MAX_SEQUENCE) {
        return false;
    }
    if (frame->type > FRAME_MAX_TYPE) {
        return false;
    }

    return frame_is_known_type(frame->type);
}

size_t frame_wire_size(const struct frame *frame)
{
    if (!frame_has_valid_shape(frame)) {
        return 0U;
    }

    return FRAME_MIN_WIRE_LEN + frame->length;
}
