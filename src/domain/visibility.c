#include "game/visibility.h"

static size_t axis_distance(size_t a, size_t b)
{
    return a > b ? a - b : b - a;
}

static int is_inside_visibility_mask(const struct game_map *map,
                                     size_t row, size_t col,
                                     size_t visible_size)
{
    size_t dr;
    size_t dc;
    long double distance_squared;
    long double diameter;

    dr = axis_distance(row, map->pacman.row);
    dc = axis_distance(col, map->pacman.col);
    distance_squared = ((long double)dr * (long double)dr) +
                       ((long double)dc * (long double)dc);
    diameter = (long double)visible_size;

    return (4.0L * distance_squared) <= (diameter * diameter);
}

static int is_valid_visibility_mode(enum game_visibility_mode mode)
{
    return mode == GAME_VISIBILITY_MODE_CLEAR ||
           mode == GAME_VISIBILITY_MODE_FLASHLIGHT ||
           mode == GAME_VISIBILITY_MODE_EXPLORE;
}

static void write_real_map(const struct game_map *map, uint8_t *out)
{
    for (size_t row = 0U; row < GAME_MAP_SIZE; row++) {
        for (size_t col = 0U; col < GAME_MAP_SIZE; col++) {
            out[(row * GAME_MAP_SIZE) + col] = (uint8_t)map->cells[row][col];
        }
    }
}

static void mark_all_seen(struct game_map *map)
{
    for (size_t row = 0U; row < GAME_MAP_SIZE; row++) {
        for (size_t col = 0U; col < GAME_MAP_SIZE; col++) {
            map->seen[row][col] = 1U;
        }
    }
}

int game_visibility_all_seen(const struct game_map *map)
{
    if (map == NULL) {
        return 0;
    }

    for (size_t row = 0U; row < GAME_MAP_SIZE; row++) {
        for (size_t col = 0U; col < GAME_MAP_SIZE; col++) {
            if (!map->seen[row][col]) {
                return 0;
            }
        }
    }

    return 1;
}

int game_visibility_build(const struct game_map *map, size_t size,
                          uint8_t *out, size_t out_len)
{
    size_t index = 0U;
    size_t cells;
    int radius;

    if (map == NULL || out == NULL || size == 0U ||
        (size % 2U) == 0U || size > (SIZE_MAX / size)) {
        return -1;
    }

    cells = size * size;
    if (out_len < cells) {
        return -1;
    }

    radius = (int)(size / 2U);
    for (int dr = -radius; dr <= radius; dr++) {
        for (int dc = -radius; dc <= radius; dc++) {
            int row = (int)map->pacman.row + dr;
            int col = (int)map->pacman.col + dc;

            if (row < 0 || col < 0 ||
                row >= (int)GAME_MAP_SIZE || col >= (int)GAME_MAP_SIZE) {
                out[index++] = (uint8_t)'X';
            } else {
                out[index++] = (uint8_t)map->cells[row][col];
            }
        }
    }

    return 0;
}

int game_visibility_build_masked_map(struct game_map *map,
                                     enum game_visibility_mode mode,
                                     size_t visible_size,
                                     uint8_t *out, size_t out_len)
{
    if (map == NULL || out == NULL || visible_size == 0U ||
        !is_valid_visibility_mode(mode) ||
        out_len < GAME_MAP_SIZE * GAME_MAP_SIZE) {
        return -1;
    }

    if (mode == GAME_VISIBILITY_MODE_CLEAR) {
        mark_all_seen(map);
        write_real_map(map, out);
        return 0;
    }

    if (game_visibility_all_seen(map)) {
        write_real_map(map, out);
        return 0;
    }

    for (size_t row = 0U; row < GAME_MAP_SIZE; row++) {
        for (size_t col = 0U; col < GAME_MAP_SIZE; col++) {
            if (is_inside_visibility_mask(map, row, col, visible_size)) {
                map->seen[row][col] = 1U;
            }
        }
    }

    if (game_visibility_all_seen(map)) {
        write_real_map(map, out);
        return 0;
    }

    for (size_t row = 0U; row < GAME_MAP_SIZE; row++) {
        for (size_t col = 0U; col < GAME_MAP_SIZE; col++) {
            size_t index = (row * GAME_MAP_SIZE) + col;

            if (mode == GAME_VISIBILITY_MODE_FLASHLIGHT) {
                out[index] = is_inside_visibility_mask(map, row, col,
                                                       visible_size) ?
                             (uint8_t)map->cells[row][col] :
                             (uint8_t)GAME_VISIBILITY_MASKED_CELL;
            } else {
                out[index] = map->seen[row][col] ?
                             (uint8_t)map->cells[row][col] :
                             (uint8_t)GAME_VISIBILITY_MASKED_CELL;
            }
        }
    }

    return 0;
}

int game_visibility_initial(const struct game_map *map,
                            uint8_t *out, size_t out_len)
{
    return game_visibility_build(map, GAME_INITIAL_VIEW_SIZE, out, out_len);
}
