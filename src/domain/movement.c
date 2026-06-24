#include "game/movement.h"

#include <stddef.h>

static int direction_delta(enum game_direction direction,
                           int *row_delta, int *col_delta)
{
    if (row_delta == NULL || col_delta == NULL) {
        return -1;
    }

    *row_delta = 0;
    *col_delta = 0;
    switch (direction) {
    case GAME_DIRECTION_RIGHT:
        *col_delta = 1;
        return 0;
    case GAME_DIRECTION_LEFT:
        *col_delta = -1;
        return 0;
    case GAME_DIRECTION_UP:
        *row_delta = -1;
        return 0;
    case GAME_DIRECTION_DOWN:
        *row_delta = 1;
        return 0;
    default:
        return -1;
    }
}

static enum game_direction turn_left(enum game_direction direction)
{
    switch (direction) {
    case GAME_DIRECTION_RIGHT:
        return GAME_DIRECTION_UP;
    case GAME_DIRECTION_LEFT:
        return GAME_DIRECTION_DOWN;
    case GAME_DIRECTION_UP:
        return GAME_DIRECTION_LEFT;
    case GAME_DIRECTION_DOWN:
    default:
        return GAME_DIRECTION_RIGHT;
    }
}

static enum game_direction turn_right(enum game_direction direction)
{
    switch (direction) {
    case GAME_DIRECTION_RIGHT:
        return GAME_DIRECTION_DOWN;
    case GAME_DIRECTION_LEFT:
        return GAME_DIRECTION_UP;
    case GAME_DIRECTION_UP:
        return GAME_DIRECTION_RIGHT;
    case GAME_DIRECTION_DOWN:
    default:
        return GAME_DIRECTION_LEFT;
    }
}

static enum game_direction turn_back(enum game_direction direction)
{
    switch (direction) {
    case GAME_DIRECTION_RIGHT:
        return GAME_DIRECTION_LEFT;
    case GAME_DIRECTION_LEFT:
        return GAME_DIRECTION_RIGHT;
    case GAME_DIRECTION_UP:
        return GAME_DIRECTION_DOWN;
    case GAME_DIRECTION_DOWN:
    default:
        return GAME_DIRECTION_UP;
    }
}

static struct game_ghost_state *ghost_for_symbol(struct game_map *map,
                                                 char symbol)
{
    if (map == NULL) {
        return NULL;
    }

    for (size_t i = 0U; i < GAME_GHOST_COUNT; i++) {
        if (map->ghosts[i].symbol == symbol) {
            return &map->ghosts[i];
        }
    }

    return NULL;
}

static const struct game_ghost_state *const_ghost_for_symbol(
    const struct game_map *map, char symbol)
{
    if (map == NULL) {
        return NULL;
    }

    for (size_t i = 0U; i < GAME_GHOST_COUNT; i++) {
        if (map->ghosts[i].symbol == symbol) {
            return &map->ghosts[i];
        }
    }

    return NULL;
}

static int ghost_at_position(const struct game_map *map,
                             size_t row, size_t col)
{
    if (map == NULL) {
        return -1;
    }

    for (size_t i = 0U; i < GAME_GHOST_COUNT; i++) {
        if (map->ghosts[i].active &&
            map->ghosts[i].position.row == row &&
            map->ghosts[i].position.col == col) {
            return (int)i;
        }
    }

    return -1;
}

static int position_for_direction(const struct game_position *from,
                                  enum game_direction direction,
                                  int *row_out, int *col_out)
{
    int row_delta;
    int col_delta;

    if (from == NULL || row_out == NULL || col_out == NULL ||
        direction_delta(direction, &row_delta, &col_delta) != 0) {
        return -1;
    }

    *row_out = (int)from->row + row_delta;
    *col_out = (int)from->col + col_delta;
    return 0;
}

static int ghost_can_move_to(const struct game_map *map,
                             int row, int col)
{
    char cell;

    if (map == NULL || row < 0 || col < 0 ||
        row >= (int)GAME_MAP_SIZE || col >= (int)GAME_MAP_SIZE) {
        return 0;
    }

    cell = map->cells[row][col];
    return cell != 'X' && !game_map_is_ghost_cell(cell);
}

static int choose_first_legal_direction(
    const struct game_map *map,
    const struct game_ghost_state *ghost,
    const enum game_direction *directions, size_t direction_count,
    enum game_direction *chosen)
{
    for (size_t i = 0U; i < direction_count; i++) {
        int row;
        int col;

        if (position_for_direction(&ghost->position, directions[i],
                                   &row, &col) == 0 &&
            ghost_can_move_to(map, row, col)) {
            *chosen = directions[i];
            return 0;
        }
    }

    return -1;
}

static uint32_t ghost_random_next(uint32_t *state)
{
    *state = (*state * 1103515245U) + 12345U;
    return *state;
}

static int choose_yellow_direction(const struct game_map *map,
                                   struct game_ghost_state *ghost,
                                   enum game_direction *chosen)
{
    enum game_direction legal[4];
    size_t legal_count = 0U;

    for (int direction = GAME_DIRECTION_RIGHT;
         direction <= GAME_DIRECTION_DOWN; direction++) {
        int row;
        int col;
        enum game_direction candidate = (enum game_direction)direction;

        if (position_for_direction(&ghost->position, candidate,
                                   &row, &col) == 0 &&
            ghost_can_move_to(map, row, col)) {
            legal[legal_count++] = candidate;
        }
    }

    if (legal_count == 0U) {
        return -1;
    }

    *chosen = legal[(size_t)(ghost_random_next(&ghost->random_state) %
                             (uint32_t)legal_count)];
    return 0;
}

static int choose_ghost_direction(const struct game_map *map,
                                  struct game_ghost_state *ghost,
                                  enum game_direction *chosen)
{
    enum game_direction current = (enum game_direction)ghost->direction;
    enum game_direction left_hand[4];
    enum game_direction right_hand[4];
    int prefers_right;

    if (direction_delta(current, &(int){0}, &(int){0}) != 0) {
        current = GAME_DIRECTION_UP;
    }

    left_hand[0] = turn_left(current);
    left_hand[1] = current;
    left_hand[2] = turn_right(current);
    left_hand[3] = turn_back(current);
    right_hand[0] = turn_right(current);
    right_hand[1] = current;
    right_hand[2] = turn_left(current);
    right_hand[3] = turn_back(current);

    if (ghost->symbol == GAME_GHOST_RED) {
        return choose_first_legal_direction(map, ghost, left_hand,
                                            sizeof(left_hand) /
                                                sizeof(left_hand[0]),
                                            chosen);
    }
    if (ghost->symbol == GAME_GHOST_BLUE) {
        return choose_first_legal_direction(map, ghost, right_hand,
                                            sizeof(right_hand) /
                                                sizeof(right_hand[0]),
                                            chosen);
    }
    if (ghost->symbol == GAME_GHOST_GREEN) {
        int rc;

        prefers_right = ghost->green_prefers_right != 0U;
        ghost->green_prefers_right = prefers_right ? 0U : 1U;
        rc = choose_first_legal_direction(
            map, ghost, prefers_right ? right_hand : left_hand,
            sizeof(right_hand) / sizeof(right_hand[0]), chosen);
        return rc;
    }
    if (ghost->symbol == GAME_GHOST_YELLOW) {
        return choose_yellow_direction(map, ghost, chosen);
    }

    return -1;
}

static void clear_ghost_origin(struct game_map *map,
                               const struct game_ghost_state *ghost)
{
    if (map->pacman.row == ghost->position.row &&
        map->pacman.col == ghost->position.col) {
        map->cells[ghost->position.row][ghost->position.col] = 'P';
    } else {
        map->cells[ghost->position.row][ghost->position.col] =
            ghost->under_cell;
    }
}

static void render_ghost_destination(struct game_map *map,
                                     struct game_ghost_state *ghost,
                                     size_t row, size_t col,
                                     char *encountered_ghost)
{
    char target = map->cells[row][col];

    ghost->position.row = row;
    ghost->position.col = col;
    if (target == 'P') {
        ghost->under_cell = '0';
        if (encountered_ghost != NULL && *encountered_ghost == '\0') {
            *encountered_ghost = ghost->symbol;
        }
        return;
    }

    ghost->under_cell = target;
    map->cells[row][col] = ghost->symbol;
}

static int move_single_ghost(struct game_map *map,
                             struct game_ghost_state *ghost,
                             char *encountered_ghost)
{
    enum game_direction chosen;
    int row;
    int col;

    if (!ghost->active) {
        return 0;
    }

    clear_ghost_origin(map, ghost);
    if (choose_ghost_direction(map, ghost, &chosen) != 0 ||
        position_for_direction(&ghost->position, chosen, &row, &col) != 0 ||
        !ghost_can_move_to(map, row, col)) {
        render_ghost_destination(map, ghost, ghost->position.row,
                                 ghost->position.col, encountered_ghost);
        return 0;
    }

    ghost->direction = (uint8_t)chosen;
    render_ghost_destination(map, ghost, (size_t)row, (size_t)col,
                             encountered_ghost);
    return 0;
}

int game_map_apply_direction_with_entry(struct game_map *map,
                                        enum game_direction direction,
                                        char *entered_cell)
{
    int row_delta;
    int col_delta;
    int next_row;
    int next_col;
    char target_cell;
    int ghost_index;

    if (map == NULL ||
        direction_delta(direction, &row_delta, &col_delta) != 0) {
        return -1;
    }

    next_row = (int)map->pacman.row + row_delta;
    next_col = (int)map->pacman.col + col_delta;
    if (next_row < 0 || next_col < 0 ||
        next_row >= (int)GAME_MAP_SIZE || next_col >= (int)GAME_MAP_SIZE) {
        return -1;
    }
    if (map->cells[next_row][next_col] == 'X') {
        return -1;
    }

    target_cell = map->cells[next_row][next_col];
    ghost_index = ghost_at_position(map, map->pacman.row, map->pacman.col);
    map->cells[map->pacman.row][map->pacman.col] =
        ghost_index >= 0 ? map->ghosts[ghost_index].symbol : '0';
    map->cells[next_row][next_col] = 'P';
    map->pacman.row = (size_t)next_row;
    map->pacman.col = (size_t)next_col;
    if (entered_cell != NULL) {
        *entered_cell = target_cell;
    }
    return 0;
}

int game_map_apply_direction(struct game_map *map,
                             enum game_direction direction)
{
    return game_map_apply_direction_with_entry(map, direction, NULL);
}

int game_map_move_ghosts(struct game_map *map, char *encountered_ghost)
{
    if (map == NULL) {
        return -1;
    }
    if (encountered_ghost != NULL) {
        *encountered_ghost = '\0';
    }

    for (size_t i = 0U; i < GAME_GHOST_COUNT; i++) {
        if (move_single_ghost(map, &map->ghosts[i], encountered_ghost) != 0) {
            return -1;
        }
    }

    return 0;
}

int game_map_apply_turn(struct game_map *map,
                        enum game_direction direction,
                        struct game_turn_result *result)
{
    struct game_turn_result local_result = {'\0', '\0', 0};
    char ghost_encounter = '\0';
    int row_delta;
    int col_delta;

    if (map == NULL ||
        direction_delta(direction, &row_delta, &col_delta) != 0) {
        return -1;
    }

    if (game_map_apply_direction_with_entry(
            map, direction, &local_result.pacman_entered_cell) == 0) {
        local_result.pacman_moved = 1;
        if (game_map_is_ghost_cell(local_result.pacman_entered_cell)) {
            local_result.encountered_ghost =
                local_result.pacman_entered_cell;
        }
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

int game_map_set_ghost_direction(struct game_map *map, char symbol,
                                 enum game_direction direction)
{
    struct game_ghost_state *ghost;
    int row_delta;
    int col_delta;

    if (direction_delta(direction, &row_delta, &col_delta) != 0) {
        return -1;
    }

    ghost = ghost_for_symbol(map, symbol);
    if (ghost == NULL || !ghost->active) {
        return -1;
    }

    ghost->direction = (uint8_t)direction;
    return 0;
}

int game_map_set_ghost_random_state(struct game_map *map, char symbol,
                                    uint32_t random_state)
{
    struct game_ghost_state *ghost = ghost_for_symbol(map, symbol);

    if (ghost == NULL || !ghost->active || random_state == 0U) {
        return -1;
    }

    ghost->random_state = random_state;
    return 0;
}

int game_map_get_ghost_position(const struct game_map *map, char symbol,
                                struct game_position *out)
{
    const struct game_ghost_state *ghost = const_ghost_for_symbol(map, symbol);

    if (ghost == NULL || !ghost->active || out == NULL) {
        return -1;
    }

    *out = ghost->position;
    return 0;
}
