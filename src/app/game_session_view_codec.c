#include "app/game_session_view_codec.h"

#include "game/visibility.h"

static int view_cell_to_code(uint8_t cell, uint8_t *code)
{
    if (code == NULL) {
        return -1;
    }

    switch (cell) {
    case '0':
        *code = 0U;
        return 0;
    case 'X':
        *code = 1U;
        return 0;
    case 'P':
        *code = 2U;
        return 0;
    case GAME_GHOST_RED:
        *code = 3U;
        return 0;
    case GAME_GHOST_BLUE:
        *code = 4U;
        return 0;
    case GAME_GHOST_GREEN:
        *code = 5U;
        return 0;
    case GAME_GHOST_YELLOW:
        *code = 6U;
        return 0;
    case '1':
        *code = 7U;
        return 0;
    case '2':
        *code = 8U;
        return 0;
    case '3':
        *code = 9U;
        return 0;
    case '4':
        *code = 10U;
        return 0;
    case '5':
        *code = 11U;
        return 0;
    case '6':
        *code = 12U;
        return 0;
    case GAME_VISIBILITY_MASKED_CELL:
        *code = 13U;
        return 0;
    default:
        return -1;
    }
}

static int view_code_to_cell(uint8_t code, uint8_t *cell)
{
    if (cell == NULL) {
        return -1;
    }

    switch (code) {
    case 0U:
        *cell = (uint8_t)'0';
        return 0;
    case 1U:
        *cell = (uint8_t)'X';
        return 0;
    case 2U:
        *cell = (uint8_t)'P';
        return 0;
    case 3U:
        *cell = (uint8_t)GAME_GHOST_RED;
        return 0;
    case 4U:
        *cell = (uint8_t)GAME_GHOST_BLUE;
        return 0;
    case 5U:
        *cell = (uint8_t)GAME_GHOST_GREEN;
        return 0;
    case 6U:
        *cell = (uint8_t)GAME_GHOST_YELLOW;
        return 0;
    case 7U:
        *cell = (uint8_t)'1';
        return 0;
    case 8U:
        *cell = (uint8_t)'2';
        return 0;
    case 9U:
        *cell = (uint8_t)'3';
        return 0;
    case 10U:
        *cell = (uint8_t)'4';
        return 0;
    case 11U:
        *cell = (uint8_t)'5';
        return 0;
    case 12U:
        *cell = (uint8_t)'6';
        return 0;
    case 13U:
        *cell = (uint8_t)GAME_VISIBILITY_MASKED_CELL;
        return 0;
    default:
        return -1;
    }
}

int game_session_view_pack(const uint8_t *cells, size_t cells_len,
                           uint8_t *out, size_t out_capacity,
                           size_t *out_len)
{
    if (cells == NULL || out == NULL || out_len == NULL ||
        cells_len != GAME_SESSION_VIEW_CELL_COUNT ||
        out_capacity < GAME_SESSION_VIEW_PACKED_BYTES) {
        return -1;
    }

    for (size_t i = 0U; i < GAME_SESSION_VIEW_CELL_COUNT; i += 2U) {
        uint8_t high;
        uint8_t low;

        if (view_cell_to_code(cells[i], &high) != 0 ||
            view_cell_to_code(cells[i + 1U], &low) != 0) {
            return -1;
        }
        out[i / 2U] = (uint8_t)((high << 4U) | low);
    }

    *out_len = GAME_SESSION_VIEW_PACKED_BYTES;
    return 0;
}

int game_session_view_unpack(const uint8_t *packed, size_t packed_len,
                             uint8_t *out, size_t out_capacity,
                             size_t *out_len)
{
    if (packed == NULL || out == NULL || out_len == NULL ||
        packed_len != GAME_SESSION_VIEW_PACKED_BYTES ||
        out_capacity < GAME_SESSION_VIEW_CELL_COUNT) {
        return -1;
    }

    for (size_t i = 0U; i < GAME_SESSION_VIEW_PACKED_BYTES; i++) {
        uint8_t byte = packed[i];

        if (view_code_to_cell((uint8_t)((byte >> 4U) & 0x0fU),
                              &out[i * 2U]) != 0 ||
            view_code_to_cell((uint8_t)(byte & 0x0fU),
                              &out[(i * 2U) + 1U]) != 0) {
            return -1;
        }
    }

    *out_len = GAME_SESSION_VIEW_CELL_COUNT;
    return 0;
}
