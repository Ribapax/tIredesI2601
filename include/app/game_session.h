#ifndef APP_GAME_SESSION_H
#define APP_GAME_SESSION_H

#include "game/map.h"
#include "game/visibility.h"
#include "net/frame.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define GAME_SESSION_BASE_TIMEOUT_MS 200
#define GAME_SESSION_MAX_TIMEOUT_MS 5000
#define GAME_SESSION_BACKOFF_ERROR_STEP 5

struct handshake_client_session;
struct handshake_server_session;

/*
 * Define o destino da interface visual do cliente.
 *
 * Entrada:
 * - stream: stream aberto para a tela do usuario. Se for NULL, volta a usar
 *   stdout.
 *
 * Saida:
 * - visualizacoes renderizadas pelo cliente passam a ser escritas em stream.
 *
 * Retorno:
 * - nenhum.
 */
void game_session_set_ui_stream(FILE *stream);

/*
 * Executa a comunicacao do jogo no servidor apos o handshake.
 *
 * Entrada:
 * - session: sessao ja estabelecida pelo handshake. O header so declara o tipo
 *   para evitar acoplar todos os consumidores aos detalhes de raw socket.
 * - map: mapa do servidor, que sera atualizado conforme movimentos aceitos.
 * - visibility_mode: estrategia de visualizacao usada para montar cada VIEW.
 * - initial_view_size: diametro inicial da mascara revelada dentro do mapa
 *   40x40 enviado ao cliente. O padrao 3 representa raio 1.
 * - moves_per_view_increase: quantidade de movimentos aceitos antes de
 *   aumentar o raio da visualizacao em 1; internamente isso aumenta o diametro
 *   em 2.
 * - file_window_size: quantidade de frames DATA pendentes na transferencia de
 *   arquivo.
 *
 * Saida:
 * - map: reflete os movimentos aceitos do PacMan e dos fantasmas.
 * - logs em stdout/stderr indicam movimentos, ACK/NACK e visualizacoes.
 *
 * Retorno:
 * - 0 quando o cliente encerra a sessao.
 * - 1 em erro operacional irrecuperavel.
 */
int game_session_run_server(struct handshake_server_session *session,
                            struct game_map *map,
                            enum game_visibility_mode visibility_mode,
                            size_t initial_view_size,
                            int moves_per_view_increase,
                            size_t file_window_size);

/*
 * Executa a comunicacao do jogo no cliente apos o handshake.
 *
 * Entrada:
 * - session: sessao ja estabelecida pelo handshake, com INIT vazio confirmado.
 *   O tipo e intencionalmente opaco para consumidores deste header.
 * - file_window_size: quantidade de frames DATA aceitos na janela de arquivo.
 *
 * Saida:
 * - tela do terminal e atualizada com a primeira visualizacao recebida apos o
 *   handshake e com as visualizacoes seguintes.
 * - movimentos digitados pelo usuario sao enviados ao servidor.
 *
 * Retorno:
 * - 0 quando o usuario encerra a sessao.
 * - 1 em erro operacional irrecuperavel.
 */
int game_session_run_client(struct handshake_client_session *session,
                            size_t file_window_size);

/*
 * Converte uma entrada textual do usuario em tipo de frame de movimento.
 *
 * Entrada:
 * - text: texto digitado, como "w", "a", "s", "d", "q" ou sequencia ANSI de
 *   seta.
 *
 * Saida:
 * - out: recebe FRAME_TYPE_MOVE_* ou FRAME_TYPE_END.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se text/out forem invalidos ou a entrada for desconhecida.
 */
int game_session_frame_type_from_input(const char *text, uint8_t *out);

#endif
