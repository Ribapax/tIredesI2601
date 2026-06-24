#ifndef NET_DIAG_H
#define NET_DIAG_H

#include <stddef.h>
#include <stdint.h>

#define NET_MAC_LEN 6
#define NET_MAC_TEXT_LEN 18

/*
 * Converte um endereco MAC em texto para bytes.
 *
 * Entrada:
 * - text: string no formato "aa:bb:cc:dd:ee:ff".
 *
 * Saida:
 * - out: vetor preenchido com os 6 bytes do MAC quando a conversao funcionar.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se text/out forem invalidos ou se o texto nao estiver no formato MAC.
 */
int net_parse_mac(const char *text, unsigned char out[NET_MAC_LEN]);

/*
 * Converte um endereco MAC em bytes para texto.
 *
 * Entrada:
 * - mac: vetor com os 6 bytes do MAC.
 *
 * Saida:
 * - out: buffer preenchido com "aa:bb:cc:dd:ee:ff" e terminador nulo.
 *
 * Retorno:
 * - nenhum. Se mac/out forem NULL, a funcao apenas retorna.
 */
void net_format_mac(const unsigned char mac[NET_MAC_LEN],
                    char out[NET_MAC_TEXT_LEN]);

/*
 * Resolve o indice numerico de uma interface de rede.
 *
 * Entrada:
 * - iface: nome da interface, por exemplo "enp3s0".
 * - err_len: tamanho do buffer err.
 *
 * Saida:
 * - ifindex: recebe o indice da interface usado por AF_PACKET.
 * - err: recebe uma mensagem curta quando ocorrer erro.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se a interface for invalida, inexistente ou se ifindex for NULL.
 */
int net_get_ifindex(const char *iface, unsigned int *ifindex,
                    char *err, size_t err_len);

/*
 * Le o endereco MAC local de uma interface de rede.
 *
 * Entrada:
 * - iface: nome da interface, por exemplo "enp3s0".
 * - err_len: tamanho do buffer err.
 *
 * Saida:
 * - mac: recebe os 6 bytes do MAC da interface.
 * - err: recebe uma mensagem curta quando ocorrer erro.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se iface/mac forem invalidos ou se a consulta ao sistema falhar.
 */
int net_get_iface_mac(const char *iface, unsigned char mac[NET_MAC_LEN],
                      char *err, size_t err_len);

/*
 * Converte texto para int e valida um intervalo fechado.
 *
 * Entrada:
 * - text: string numerica aceita por strtol, como "10" ou "0x10".
 * - min: menor valor aceito.
 * - max: maior valor aceito.
 *
 * Saida:
 * - out: recebe o valor convertido quando estiver dentro do intervalo.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se a conversao falhar, sobrar lixo no texto ou o valor sair do intervalo.
 */
int net_parse_int_range(const char *text, int min, int max, int *out);

/*
 * Converte texto para uint16_t e valida um intervalo fechado.
 *
 * Entrada:
 * - text: string numerica aceita por strtoul, como "2048" ou "0x88b5".
 * - min: menor valor aceito.
 * - max: maior valor aceito, limitado a 0xffff.
 *
 * Saida:
 * - out: recebe o valor convertido como uint16_t.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se a conversao falhar, sobrar lixo no texto ou o valor sair do intervalo.
 */
int net_parse_u16_range(const char *text, unsigned int min,
                        unsigned int max, uint16_t *out);

#endif
