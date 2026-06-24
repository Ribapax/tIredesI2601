#ifndef NET_HANDSHAKE_H
#define NET_HANDSHAKE_H

#include "net/frame.h"
#include "net/raw_eth.h"

#include <stdbool.h>
#include <stdint.h>

#define HANDSHAKE_TIMEOUT_MS 5000
#define HANDSHAKE_MAX_RETRIES 30
#define HANDSHAKE_INIT_SEQUENCE 0U

struct handshake_server_session {
    struct raw_eth_socket sock;
    unsigned char peer_mac[NET_MAC_LEN];
};

struct handshake_client_session {
    struct raw_eth_socket sock;
    unsigned char peer_mac[NET_MAC_LEN];
    struct frame init_frame;
};

/*
 * Monta um frame ACK para uma sequencia recebida.
 *
 * Entrada:
 * - sequence: sequencia reconhecida.
 *
 * Saida:
 * - frame: recebe tipo ACK e payload vazio.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se frame for NULL.
 */
int handshake_build_ack_frame(struct frame *frame, uint8_t sequence);

/*
 * Verifica se um frame e o ACK esperado para uma sequencia.
 *
 * Entrada:
 * - frame: frame recebido ou montado.
 * - sequence: sequencia que deve estar sendo reconhecida.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - true se for ACK vazio para sequence.
 * - false caso contrario.
 */
bool handshake_is_ack_for(const struct frame *frame, uint8_t sequence);

/*
 * Executa o handshake de servidor e mantem a conexao aberta para o jogo.
 *
 * Entrada:
 * - iface: interface cabeada usada pelo raw socket.
 * - ethertype: EtherType privado do protocolo.
 * - init_frame: frame tipo INIT vazio. Ele identifica a conexao, mas nao
 *   carrega visualizacao do jogo.
 *
 * Saida:
 * - session: recebe socket aberto e MAC do cliente conectado.
 * - logs em stdout/stderr indicando envios, timeouts e ACK recebido.
 *
 * Retorno:
 * - 0 quando um cliente responder com ACK.
 * - 1 em timeout final ou erro operacional.
 */
int handshake_establish_server(const char *iface, uint16_t ethertype,
                               const struct frame *init_frame,
                               struct handshake_server_session *session);

/*
 * Executa o handshake de cliente e mantem a conexao aberta para o jogo.
 *
 * Entrada:
 * - iface: interface cabeada usada pelo raw socket.
 * - ethertype: EtherType privado do protocolo.
 *
 * Saida:
 * - session: recebe socket aberto, MAC do servidor e frame INIT vazio recebido.
 * - logs em stdout/stderr indicando INIT recebido e ACK enviado.
 *
 * Retorno:
 * - 0 quando o ACK for enviado.
 * - 1 em erro operacional.
 */
int handshake_establish_client(const char *iface, uint16_t ethertype,
                               struct handshake_client_session *session);

/*
 * Fecha uma sessao de servidor criada pelo handshake.
 *
 * Entrada:
 * - session: sessao inicializada por handshake_establish_server.
 *
 * Saida:
 * - session->sock.fd passa a ser -1.
 *
 * Retorno:
 * - nenhum.
 */
void handshake_server_session_close(struct handshake_server_session *session);

/*
 * Fecha uma sessao de cliente criada pelo handshake.
 *
 * Entrada:
 * - session: sessao inicializada por handshake_establish_client.
 *
 * Saida:
 * - session->sock.fd passa a ser -1.
 *
 * Retorno:
 * - nenhum.
 */
void handshake_client_session_close(struct handshake_client_session *session);

/*
 * Executa o papel de servidor do handshake de rede.
 *
 * Entrada:
 * - iface: interface cabeada usada pelo raw socket.
 * - ethertype: EtherType privado do protocolo.
 * - init_frame: frame tipo INIT vazio usado para descoberta/conexao.
 *
 * Saida:
 * - logs em stdout/stderr indicando envios, timeouts e ACK recebido.
 *
 * Retorno:
 * - 0 quando um cliente responder com ACK.
 * - 1 em timeout final ou erro operacional.
 */
int handshake_run_server(const char *iface, uint16_t ethertype,
                         const struct frame *init_frame);

/*
 * Executa o papel de cliente do handshake de rede.
 *
 * Entrada:
 * - iface: interface cabeada usada pelo raw socket.
 * - ethertype: EtherType privado do protocolo.
 *
 * Saida:
 * - logs em stdout/stderr indicando INIT recebido e ACK enviado.
 *
 * Retorno:
 * - 0 quando o ACK for enviado.
 * - 1 em erro operacional.
 */
int handshake_run_client(const char *iface, uint16_t ethertype);

#endif
