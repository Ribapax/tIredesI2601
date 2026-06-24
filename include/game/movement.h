#ifndef GAME_MOVEMENT_H
#define GAME_MOVEMENT_H

#include "game/map.h"

enum game_direction {
    GAME_DIRECTION_RIGHT = 0,
    GAME_DIRECTION_LEFT,
    GAME_DIRECTION_UP,
    GAME_DIRECTION_DOWN
};

struct game_turn_result {
    char pacman_entered_cell;
    char encountered_ghost;
    int pacman_moved;
};

/*
 * Aplica um movimento do PacMan no mapa.
 *
 * Entrada:
 * - map: mapa carregado com a posicao atual do PacMan.
 * - direction: direcao desejada.
 *
 * Saida:
 * - map: atualiza a posicao do PacMan quando o movimento for valido.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se map for NULL, a direcao for invalida, sair do mapa ou bater em X.
 */
int game_map_apply_direction(struct game_map *map,
                             enum game_direction direction);

/*
 * Aplica um movimento e informa qual celula foi ocupada pelo PacMan.
 *
 * Entrada:
 * - map: mapa carregado com a posicao atual do PacMan.
 * - direction: direcao desejada.
 *
 * Saida:
 * - map: atualiza a posicao do PacMan quando o movimento for valido.
 * - entered_cell: recebe o simbolo que existia na celula de destino antes de
 *   ser substituido por 'P'. Pode ser NULL quando o chamador nao precisa dele.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se map for NULL, a direcao for invalida, sair do mapa ou bater em X.
 */
int game_map_apply_direction_with_entry(struct game_map *map,
                                        enum game_direction direction,
                                        char *entered_cell);

/*
 * Aplica um turno aceito pelo protocolo: tenta mover o PacMan e, em seguida,
 * move os fantasmas ativos uma vez, mesmo quando o PacMan fica parado por
 * parede ou limite do mapa.
 *
 * Entrada:
 * - map: mapa carregado com PacMan e fantasmas.
 * - direction: direcao solicitada pelo PacMan.
 *
 * Saida:
 * - map: atualiza PacMan, fantasmas e a matriz renderizada.
 * - result: recebe celula ocupada pelo PacMan, fantasma encontrado e flag de
 *   deslocamento do PacMan. Pode ser NULL quando o chamador nao precisa.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se map for NULL ou a direcao for invalida.
 */
int game_map_apply_turn(struct game_map *map,
                        enum game_direction direction,
                        struct game_turn_result *result);

/*
 * Move apenas os fantasmas ativos, usado por testes de dominio.
 *
 * Regras:
 * - vermelho: mao esquerda.
 * - azul: mao direita.
 * - verde: alterna direita/esquerda a cada decisao.
 * - amarelo: pseudoaleatorio entre movimentos legais.
 *
 * Entrada:
 * - map: mapa carregado com fantasmas.
 *
 * Saida:
 * - map: atualiza posicoes dos fantasmas e a matriz renderizada.
 * - encountered_ghost: recebe o simbolo do fantasma que alcancou o PacMan, ou
 *   '\0' quando nao houve encontro. Pode ser NULL.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se map for NULL.
 */
int game_map_move_ghosts(struct game_map *map, char *encountered_ghost);

int game_map_set_ghost_direction(struct game_map *map, char symbol,
                                 enum game_direction direction);
int game_map_set_ghost_random_state(struct game_map *map, char symbol,
                                    uint32_t random_state);
int game_map_get_ghost_position(const struct game_map *map, char symbol,
                                struct game_position *out);

#endif
