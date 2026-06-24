#ifndef GAME_MAP_H
#define GAME_MAP_H

#include <stddef.h>
#include <stdint.h>

#define GAME_MAP_SIZE 40U
#define GAME_DEFAULT_MAP_PATH "resources/maps/default.csv"
#define GAME_GHOST_COUNT 4U
#define GAME_GHOST_RED 'R'
#define GAME_GHOST_BLUE 'B'
#define GAME_GHOST_GREEN 'G'
#define GAME_GHOST_YELLOW 'Y'

struct game_position {
    size_t row;
    size_t col;
};

struct game_ghost_state {
    char symbol;
    struct game_position position;
    uint8_t active;
    uint8_t direction;
    uint8_t green_prefers_right;
    uint32_t random_state;
    char under_cell;
};

struct game_map {
    char cells[GAME_MAP_SIZE][GAME_MAP_SIZE];
    uint8_t seen[GAME_MAP_SIZE][GAME_MAP_SIZE];
    struct game_position pacman;
    struct game_ghost_state ghosts[GAME_GHOST_COUNT];
};

/*
 * Carrega um mapa 40x40 separado por ponto e virgula.
 * Quando o caminho for o mapa default, posiciona os demais elementos do jogo
 * aleatoriamente em celulas livres fora da area 9x9 centrada no PacMan.
 *
 * Entrada:
 * - path: caminho do CSV. Se for NULL, usa GAME_DEFAULT_MAP_PATH.
 * - err_len: tamanho do buffer err.
 *
 * Saida:
 * - map: recebe a matriz, a posicao do PacMan e a matriz de celulas vistas
 *   inicialmente zerada.
 * - err: recebe mensagem curta em caso de erro.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se o arquivo nao abrir, tiver dimensao invalida, simbolo invalido
 *   ou nao tiver exatamente um PacMan.
 */
int game_map_load(struct game_map *map, const char *path,
                  char *err, size_t err_len);

/*
 * Verifica se um simbolo e aceito pelo mapa do jogo.
 *
 * Entrada:
 * - cell: caractere de uma celula.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - 1 se for P, X, 0, R, B, G, Y ou 1..6.
 * - 0 caso contrario.
 */
int game_map_is_valid_cell(char cell);

/*
 * Verifica se uma celula representa um dos fantasmas do jogo.
 *
 * Entrada:
 * - cell: caractere de uma celula.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - 1 se for R, B, G ou Y.
 * - 0 caso contrario.
 */
int game_map_is_ghost_cell(char cell);

/*
 * Reconstroi o estado de atores a partir de map->cells.
 *
 * Entrada:
 * - map: mapa com matriz e posicao do PacMan.
 *
 * Saida:
 * - map->ghosts: recebe posicoes, direcoes iniciais e estado interno dos
 *   fantasmas encontrados em map->cells.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se map for NULL.
 */
int game_map_refresh_actors(struct game_map *map);

#endif
