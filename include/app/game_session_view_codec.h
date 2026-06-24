#ifndef APP_GAME_SESSION_VIEW_CODEC_H
#define APP_GAME_SESSION_VIEW_CODEC_H

#include "game/map.h"

#include <stddef.h>
#include <stdint.h>

#define GAME_SESSION_VIEW_CELL_COUNT (GAME_MAP_SIZE * GAME_MAP_SIZE)
#define GAME_SESSION_VIEW_PACKED_BYTES ((GAME_SESSION_VIEW_CELL_COUNT + 1U) / 2U)

/*
 * Codifica a VIEW logica 40x40 em simbolos de 4 bits para trafego.
 *
 * Entrada:
 * - cells: GAME_SESSION_VIEW_CELL_COUNT celulas ja mascaradas.
 * - cells_len: deve ser GAME_SESSION_VIEW_CELL_COUNT.
 * - out_capacity: tamanho do buffer out.
 *
 * Saida:
 * - out: recebe dois simbolos por byte, celula par no nibble alto.
 * - out_len: recebe GAME_SESSION_VIEW_PACKED_BYTES.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se algum parametro/tamanho/simbolo for invalido.
 */
int game_session_view_pack(const uint8_t *cells, size_t cells_len,
                           uint8_t *out, size_t out_capacity,
                           size_t *out_len);

/*
 * Decodifica a VIEW compactada recebida da rede para a matriz 40x40 logica.
 *
 * Entrada:
 * - packed: GAME_SESSION_VIEW_PACKED_BYTES bytes.
 * - packed_len: deve ser GAME_SESSION_VIEW_PACKED_BYTES.
 * - out_capacity: tamanho do buffer out.
 *
 * Saida:
 * - out: recebe GAME_SESSION_VIEW_CELL_COUNT celulas.
 * - out_len: recebe GAME_SESSION_VIEW_CELL_COUNT.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se algum parametro/tamanho/codigo for invalido.
 */
int game_session_view_unpack(const uint8_t *packed, size_t packed_len,
                             uint8_t *out, size_t out_capacity,
                             size_t *out_len);

#endif
