#ifndef NET_RAW_ETH_H
#define NET_RAW_ETH_H

#include "net/diag.h"

#include <stddef.h>
#include <stdint.h>

#define RAW_ETH_DEFAULT_ETHERTYPE 0x88b6U
#define RAW_ETH_MAX_PAYLOAD_LEN 1500U

struct raw_eth_socket {
    int fd;
    unsigned int ifindex;
    uint16_t ethertype;
    unsigned char local_mac[NET_MAC_LEN];
};

/*
 * Abre e faz bind de um raw socket Ethernet em uma interface.
 *
 * Entrada:
 * - iface: nome da interface cabeada.
 * - ethertype: EtherType privado usado pelo protocolo.
 * - err_len: tamanho do buffer err.
 *
 * Saida:
 * - sock: recebe fd, ifindex, ethertype e MAC local.
 * - err: recebe uma mensagem curta em caso de erro.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se interface, permissao, socket ou bind falharem.
 */
int raw_eth_open(struct raw_eth_socket *sock, const char *iface,
                 uint16_t ethertype, char *err, size_t err_len);

/*
 * Fecha um raw socket aberto.
 *
 * Entrada:
 * - sock: socket inicializado por raw_eth_open.
 *
 * Saida:
 * - sock->fd passa a ser -1.
 *
 * Retorno:
 * - nenhum.
 */
void raw_eth_close(struct raw_eth_socket *sock);

/*
 * Preenche um vetor MAC com o endereco de broadcast Ethernet.
 *
 * Entrada:
 * - nenhuma.
 *
 * Saida:
 * - out: recebe ff:ff:ff:ff:ff:ff.
 *
 * Retorno:
 * - nenhum. Se out for NULL, a funcao apenas retorna.
 */
void raw_eth_broadcast_mac(unsigned char out[NET_MAC_LEN]);

/*
 * Verifica se um MAC e o broadcast Ethernet.
 *
 * Entrada:
 * - mac: vetor com 6 bytes.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - 1 se todos os bytes forem 0xff.
 * - 0 caso contrario.
 */
int raw_eth_is_broadcast(const unsigned char mac[NET_MAC_LEN]);

/*
 * Envia um payload Ethernet para um MAC destino.
 *
 * Entrada:
 * - sock: raw socket aberto.
 * - dst_mac: MAC destino.
 * - payload: bytes do protocolo a transmitir.
 * - payload_len: tamanho do payload, limitado a RAW_ETH_MAX_PAYLOAD_LEN.
 * - err_len: tamanho do buffer err.
 *
 * Saida:
 * - err: recebe mensagem curta em caso de erro.
 *
 * Retorno:
 * - quantidade de bytes Ethernet enviados, incluindo padding minimo.
 * - -1 em erro.
 */
int raw_eth_send(const struct raw_eth_socket *sock,
                 const unsigned char dst_mac[NET_MAC_LEN],
                 const uint8_t *payload, size_t payload_len,
                 char *err, size_t err_len);

/*
 * Recebe um payload Ethernet usando timeout controlado pela aplicacao.
 *
 * Entrada:
 * - sock: raw socket aberto.
 * - timeout_ms: tempo maximo total de espera.
 * - payload_capacity: tamanho do buffer payload.
 * - err_len: tamanho do buffer err.
 *
 * Saida:
 * - src_mac: MAC origem do pacote aceito.
 * - dst_mac: MAC destino do pacote aceito.
 * - payload: bytes recebidos apos o cabecalho Ethernet.
 * - payload_len: quantidade de bytes copiados para payload.
 * - err: recebe mensagem curta em caso de erro.
 *
 * Retorno:
 * - 1 quando um payload do EtherType esperado for recebido.
 * - 0 quando o timeout expirar.
 * - -1 em erro.
 */
int raw_eth_recv(const struct raw_eth_socket *sock, int timeout_ms,
                 unsigned char src_mac[NET_MAC_LEN],
                 unsigned char dst_mac[NET_MAC_LEN],
                 uint8_t *payload, size_t payload_capacity,
                 size_t *payload_len, char *err, size_t err_len);

#endif
