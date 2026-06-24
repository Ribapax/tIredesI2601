#ifndef GAME_VISIBILITY_H
#define GAME_VISIBILITY_H

#include "game/map.h"

#include <stddef.h>
#include <stdint.h>

#define GAME_INITIAL_VIEW_RADIUS 1U
#define GAME_INITIAL_VIEW_SIZE ((GAME_INITIAL_VIEW_RADIUS * 2U) + 1U)
#define GAME_INITIAL_VIEW_CELLS (GAME_INITIAL_VIEW_SIZE * GAME_INITIAL_VIEW_SIZE)
#define GAME_VISIBILITY_MASKED_CELL 0xdbU

enum game_visibility_mode {
    GAME_VISIBILITY_MODE_CLEAR = 0,
    GAME_VISIBILITY_MODE_FLASHLIGHT,
    GAME_VISIBILITY_MODE_EXPLORE
};

/*
 * Calcula uma visualizacao quadrada ao redor do PacMan.
 *
 * Entrada:
 * - map: mapa carregado com a posicao atual do PacMan.
 * - size: largura e altura da matriz, deve ser impar.
 * - out_len: tamanho do buffer out.
 *
 * Saida:
 * - out: recebe a matriz linearizada, centrada no PacMan.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se map/out forem invalidos, size for par/zero ou out_len for pequeno.
 */
int game_visibility_build(const struct game_map *map, size_t size,
                          uint8_t *out, size_t out_len);

/*
 * Calcula a visualizacao enviada pela rede como mapa 40x40 mascarado.
 *
 * Entrada:
 * - map: mapa carregado com a posicao atual do PacMan.
 * - mode: estrategia usada para esconder ou revelar celulas do mapa.
 * - visible_size: diametro da mascara circular visivel centrada no PacMan. O
 *   valor de visible_size controla apenas a mascara atual; quando todas as
 *   celulas ja estiverem marcadas como vistas, o mapa inteiro e enviado sem
 *   recalcular a mascara.
 * - out_len: tamanho do buffer out.
 *
 * Saida:
 * - map->seen: marca como vistas as celulas reveladas pela estrategia atual.
 * - out: recebe sempre GAME_MAP_SIZE * GAME_MAP_SIZE bytes. No modo claro,
 *   todas as celulas recebem o simbolo real. No modo lanterna, somente a
 *   mascara atual mostra simbolos reais enquanto ainda houver celulas nao
 *   vistas. No modo exploracao, celulas ja vistas continuam mostrando simbolos
 *   reais.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se map/out forem invalidos, visible_size for invalido ou out_len for
 *   menor que o mapa completo.
 */
int game_visibility_build_masked_map(struct game_map *map,
                                     enum game_visibility_mode mode,
                                     size_t visible_size,
                                     uint8_t *out, size_t out_len);

/*
 * Verifica se todas as celulas do mapa ja foram vistas.
 *
 * Entrada:
 * - map: mapa com a matriz seen atualizada.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - 1 se todas as celulas estiverem marcadas como vistas.
 * - 0 se map for NULL ou existir alguma celula ainda nao vista.
 */
int game_visibility_all_seen(const struct game_map *map);

/*
 * Calcula a visualizacao inicial ao redor do PacMan.
 *
 * Entrada:
 * - map: mapa carregado com a posicao atual do PacMan.
 * - out_len: tamanho do buffer out.
 *
 * Saida:
 * - out: recebe a matriz 3x3 linearizada, centrada no PacMan.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se map/out forem invalidos ou out_len for menor que 9.
 */
int game_visibility_initial(const struct game_map *map,
                            uint8_t *out, size_t out_len);

#endif
