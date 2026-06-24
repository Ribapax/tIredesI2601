#include "game/map.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define GAME_DEFAULT_ELEMENT_PADDING_RADIUS 4U

static const char game_default_random_elements[] = {
    GAME_GHOST_RED, GAME_GHOST_BLUE, GAME_GHOST_GREEN, GAME_GHOST_YELLOW,
    '1', '2', '3', '4', '5', '6'
};

static const char game_ghost_symbols[GAME_GHOST_COUNT] = {
    GAME_GHOST_RED, GAME_GHOST_BLUE, GAME_GHOST_GREEN, GAME_GHOST_YELLOW
};

static void set_error(char *err, size_t err_len, const char *fmt, ...)
{
    va_list args;

    if (err == NULL || err_len == 0U) {
        return;
    }

    va_start(args, fmt);
    (void)vsnprintf(err, err_len, fmt, args);
    va_end(args);
}

int game_map_is_valid_cell(char cell)
{
    return cell == 'P' || cell == 'X' || cell == '0' ||
           game_map_is_ghost_cell(cell) ||
           (cell >= '1' && cell <= '6');
}

int game_map_is_ghost_cell(char cell)
{
    for (size_t i = 0U; i < GAME_GHOST_COUNT; i++) {
        if (game_ghost_symbols[i] == cell) {
            return 1;
        }
    }

    return 0;
}

static uint32_t default_ghost_random_state(char symbol)
{
    return 0x9e3779b9U ^ ((uint32_t)(unsigned char)symbol * 2654435761U);
}

static void init_ghost_defaults(struct game_map *map)
{
    for (size_t i = 0U; i < GAME_GHOST_COUNT; i++) {
        map->ghosts[i].symbol = game_ghost_symbols[i];
        map->ghosts[i].position.row = 0U;
        map->ghosts[i].position.col = 0U;
        map->ghosts[i].active = 0U;
        map->ghosts[i].direction = 2U;
        map->ghosts[i].green_prefers_right = 1U;
        map->ghosts[i].random_state =
            default_ghost_random_state(game_ghost_symbols[i]);
        map->ghosts[i].under_cell = '0';
    }
}

static struct game_ghost_state *ghost_for_symbol(struct game_map *map,
                                                 char symbol)
{
    for (size_t i = 0U; i < GAME_GHOST_COUNT; i++) {
        if (map->ghosts[i].symbol == symbol) {
            return &map->ghosts[i];
        }
    }

    return NULL;
}

int game_map_refresh_actors(struct game_map *map)
{
    if (map == NULL) {
        return -1;
    }

    init_ghost_defaults(map);
    for (size_t row = 0U; row < GAME_MAP_SIZE; row++) {
        for (size_t col = 0U; col < GAME_MAP_SIZE; col++) {
            char cell = map->cells[row][col];

            if (cell == 'P') {
                map->pacman.row = row;
                map->pacman.col = col;
            } else if (game_map_is_ghost_cell(cell)) {
                struct game_ghost_state *ghost =
                    ghost_for_symbol(map, cell);

                if (ghost != NULL) {
                    ghost->active = 1U;
                    ghost->position.row = row;
                    ghost->position.col = col;
                    ghost->under_cell = '0';
                }
            }
        }
    }

    return 0;
}

static int is_default_map_path(const char *path, const char *chosen_path)
{
    return path == NULL || strcmp(chosen_path, GAME_DEFAULT_MAP_PATH) == 0;
}

static int is_randomized_element(char cell)
{
    for (size_t i = 0U; i < sizeof(game_default_random_elements); i++) {
        if (cell == game_default_random_elements[i]) {
            return 1;
        }
    }

    return 0;
}

static uint32_t map_random_seed(void)
{
    uint32_t seed = (uint32_t)time(NULL);

    seed ^= (uint32_t)clock();
    seed ^= 0x9e3779b9U;
    return seed == 0U ? 1U : seed;
}

static uint32_t map_random_next(uint32_t *state)
{
    *state = (*state * 1103515245U) + 12345U;
    return *state;
}

static size_t distance_axis(size_t a, size_t b)
{
    return a > b ? a - b : b - a;
}

static int is_inside_pacman_padding(const struct game_map *map,
                                    size_t row, size_t col)
{
    return distance_axis(row, map->pacman.row) <=
               GAME_DEFAULT_ELEMENT_PADDING_RADIUS &&
           distance_axis(col, map->pacman.col) <=
               GAME_DEFAULT_ELEMENT_PADDING_RADIUS;
}

static int randomize_default_elements(struct game_map *map,
                                      char *err, size_t err_len)
{
    struct game_position candidates[GAME_MAP_SIZE * GAME_MAP_SIZE];
    size_t candidate_count = 0U;
    uint32_t random_state = map_random_seed();

    for (size_t row = 0U; row < GAME_MAP_SIZE; row++) {
        for (size_t col = 0U; col < GAME_MAP_SIZE; col++) {
            if (is_randomized_element(map->cells[row][col])) {
                map->cells[row][col] = '0';
            }
        }
    }

    for (size_t row = 0U; row < GAME_MAP_SIZE; row++) {
        for (size_t col = 0U; col < GAME_MAP_SIZE; col++) {
            if (map->cells[row][col] == '0' &&
                !is_inside_pacman_padding(map, row, col)) {
                candidates[candidate_count].row = row;
                candidates[candidate_count].col = col;
                candidate_count++;
            }
        }
    }

    if (candidate_count < sizeof(game_default_random_elements)) {
        set_error(err, err_len,
                  "mapa default nao tem espaco livre para elementos");
        return -1;
    }

    for (size_t i = 0U; i < sizeof(game_default_random_elements); i++) {
        size_t selected =
            (size_t)(map_random_next(&random_state) % (uint32_t)candidate_count);
        struct game_position position = candidates[selected];

        map->cells[position.row][position.col] =
            game_default_random_elements[i];
        candidates[selected] = candidates[candidate_count - 1U];
        candidate_count--;
    }

    return 0;
}

int game_map_load(struct game_map *map, const char *path,
                  char *err, size_t err_len)
{
    const char *chosen_path = path == NULL ? GAME_DEFAULT_MAP_PATH : path;
    FILE *file;
    char line[256];
    size_t row = 0U;
    int pacman_count = 0;

    if (map == NULL) {
        set_error(err, err_len, "mapa de saida ausente");
        return -1;
    }

    file = fopen(chosen_path, "r");
    if (file == NULL) {
        set_error(err, err_len, "nao abriu mapa '%s'", chosen_path);
        return -1;
    }

    memset(map, 0, sizeof(*map));
    init_ghost_defaults(map);
    while (fgets(line, sizeof(line), file) != NULL) {
        size_t col = 0U;
        char *cursor = line;

        if (row >= GAME_MAP_SIZE) {
            set_error(err, err_len, "mapa tem mais de %u linhas",
                      (unsigned int)GAME_MAP_SIZE);
            (void)fclose(file);
            return -1;
        }

        for (;;) {
            char cell = *cursor;

            if (!game_map_is_valid_cell(cell)) {
                set_error(err, err_len, "simbolo invalido linha %zu coluna %zu",
                          row + 1U, col + 1U);
                (void)fclose(file);
                return -1;
            }
            if (col >= GAME_MAP_SIZE) {
                set_error(err, err_len, "linha %zu tem colunas demais",
                          row + 1U);
                (void)fclose(file);
                return -1;
            }

            map->cells[row][col] = cell;
            if (cell == 'P') {
                map->pacman.row = row;
                map->pacman.col = col;
                pacman_count++;
            }
            col++;
            cursor++;

            if (*cursor == ';') {
                cursor++;
                continue;
            }
            if (*cursor == '\n' || *cursor == '\0') {
                break;
            }

            set_error(err, err_len, "separador invalido linha %zu coluna %zu",
                      row + 1U, col);
            (void)fclose(file);
            return -1;
        }

        if (col != GAME_MAP_SIZE) {
            set_error(err, err_len, "linha %zu tem %zu colunas",
                      row + 1U, col);
            (void)fclose(file);
            return -1;
        }
        row++;
    }

    if (ferror(file)) {
        set_error(err, err_len, "erro lendo mapa '%s'", chosen_path);
        (void)fclose(file);
        return -1;
    }
    (void)fclose(file);

    if (row != GAME_MAP_SIZE) {
        set_error(err, err_len, "mapa tem %zu linhas", row);
        return -1;
    }
    if (pacman_count != 1) {
        set_error(err, err_len, "mapa deve ter exatamente um PacMan");
        return -1;
    }
    if (is_default_map_path(path, chosen_path) &&
        randomize_default_elements(map, err, err_len) != 0) {
        return -1;
    }
    if (game_map_refresh_actors(map) != 0) {
        set_error(err, err_len, "falha ao carregar atores do mapa");
        return -1;
    }

    return 0;
}
