#ifndef UI_GAME_VIEW_H
#define UI_GAME_VIEW_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Define o destino da interface visual do cliente.
 */
void ui_game_view_set_stream(FILE *stream);

/*
 * Le uma tecla de comando do usuario, usando modo raw quando stdin e terminal.
 * Captura tambem sequencias ANSI de setas quando disponiveis.
 */
int ui_game_view_read_key(char *out, size_t out_len);

/*
 * Renderiza uma visualizacao de mapa recebida pelo cliente.
 */
void ui_game_view_render(const uint8_t *data, size_t side);

/*
 * Renderiza uma visualizacao de mapa com uma linha de status opcional.
 */
void ui_game_view_render_with_status(const uint8_t *data, size_t side,
                                     const char *status);

/*
 * Converte entrada textual ou sequencia ANSI de seta em tipo de frame de
 * movimento.
 */
int ui_game_view_frame_type_from_input(const char *text, uint8_t *out);

#endif
