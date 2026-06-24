#ifndef NET_FRAGMENT_H
#define NET_FRAGMENT_H

#include "net/frame.h"
#include "net/protocol_limits.h"

#include <stddef.h>
#include <stdint.h>

#define NET_TRANSFER_START_FALLBACK_DATA_BYTES 5U
#define NET_TRANSFER_START_DATA_BYTES 6U
#define NET_TRANSFER_WINDOW_NONE 0U
#define NET_TRANSFER_FILE_ID_NONE 0U
#define NET_TRANSFER_MAX_FILE_ID 7U
#define NET_FRAGMENT_DATA_BYTES FRAME_MAX_DATA_LEN
#define NET_FRAGMENT_MAX_MESSAGE_LEN UINT32_MAX

struct net_transfer_start {
    uint8_t message_type;
    uint8_t file_id;
    uint8_t window_size;
    uint32_t total_bytes;
};

struct net_fragment_reassembly {
    uint8_t type;
    uint8_t file_id;
    uint8_t base_sequence;
    uint8_t expected_sequence;
    uint8_t started;
    uint32_t total_bytes;
    uint32_t received_bytes;
};

/*
 * Calcula quantos frames sao necessarios para transmitir uma mensagem.
 *
 * Entrada:
 * - data_len: quantidade de bytes brutos a enviar.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - quantidade de frames DATA necessarios.
 * - 0 se data_len for 0 ou exceder o tamanho representavel.
 */
size_t net_fragment_count(size_t data_len);

/*
 * Monta um frame de inicio de transferencia fragmentada, sem janela declarada no
 * payload.
 *
 * Entrada:
 * - message_type: tipo transferivel da mensagem inteira (`VIEW` ou `FILE_*`).
 * - file_id: identificador de arquivo em 3 bits; deve ser 0 para tipos que
 *   nao representam arquivo.
 * - sequence: sequencia logica da mensagem, usada tambem no ACK/NACK final.
 * - total_bytes: tamanho total da mensagem em bytes.
 *
 * Saida:
 * - frame: recebe message_type e payload de metadados.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se os campos nao couberem ou violarem o contrato do protocolo.
 */
int net_fragment_build_start_frame(struct frame *frame, uint8_t message_type,
                                   uint8_t file_id, uint8_t sequence,
                                   size_t total_bytes);

/*
 * Monta o frame de inicio de uma transferencia fragmentada informando a janela que
 * o transmissor vai usar para os DATA seguintes.
 *
 * Entrada:
 * - message_type: tipo logico transferivel da mensagem inteira.
 * - file_id: identificador de arquivo em 3 bits; deve ser 0 para VIEW.
 * - sequence: sequencia logica da mensagem, usada no ACK/NACK do inicio.
 * - total_bytes: tamanho total da mensagem em bytes.
 * - window_size: janela declarada para a transferencia DATA, no intervalo
 *   definido pelo protocolo.
 *
 * Saida:
 * - frame: recebe message_type e payload com window_size.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se os campos violarem o contrato do protocolo.
 */
int net_fragment_build_start_frame_with_window(
    struct frame *frame, uint8_t message_type, uint8_t file_id,
    uint8_t sequence, size_t total_bytes, size_t window_size);

/*
 * Decodifica os metadados de um frame de inicio de transferencia.
 *
 * Entrada:
 * - frame: frame ja validado pelo codec.
 *
 * Saida:
 * - start: recebe tipo logico, identificador de arquivo, janela declarada
 *   quando presente e total de bytes.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se frame/start forem invalidos ou se o metadado nao respeitar o
 *   contrato do protocolo.
 */
int net_fragment_parse_start_frame(const struct frame *frame,
                                   struct net_transfer_start *start);

/*
 * Monta um frame contendo um chunk de uma mensagem maior.
 *
 * Entrada:
 * - type: tipo logico transferivel da mensagem inteira (`VIEW` ou `FILE_*`).
 * - base_sequence: sequencia logica da mensagem.
 * - data: bytes brutos da mensagem completa.
 * - data_len: tamanho total da mensagem.
 * - chunk_index: indice sequencial do chunk dentro do stream.
 *
 * Saida:
 * - frame: recebe FRAME_TYPE_DATA, sequencia derivada e payload fragmentado.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se argumentos forem invalidos ou se a mensagem nao couber.
 */
int net_fragment_build_frame(struct frame *frame, uint8_t type,
                             uint8_t base_sequence, const uint8_t *data,
                             size_t data_len, size_t chunk_index);

/*
 * Monta um frame DATA a partir de um chunk ja separado pelo chamador.
 *
 * Entrada:
 * - type: tipo logico transferivel da mensagem inteira (`VIEW` ou `FILE_*`).
 * - base_sequence: sequencia logica da mensagem.
 * - chunk: bytes brutos deste pedaco.
 * - chunk_len: tamanho do chunk, entre 1 e NET_FRAGMENT_DATA_BYTES.
 * - chunk_index: indice sequencial do chunk dentro do stream.
 *
 * Saida:
 * - frame: recebe FRAME_TYPE_DATA, sequencia derivada e payload do chunk.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se argumentos forem invalidos.
 */
int net_fragment_build_chunk_frame(struct frame *frame, uint8_t type,
                                   uint8_t base_sequence,
                                   const uint8_t *chunk, size_t chunk_len,
                                   size_t chunk_index);

/*
 * Inicializa o estado de remontagem sequencial de uma mensagem fragmentada.
 *
 * Esta superficie aceita apenas o proximo DATA esperado e permanece publica
 * para testes e compatibilidade. O caminho confiavel usado pela sessao de jogo,
 * com janela, duplicatas e entrega fora de ordem, e `net_window_receiver`.
 *
 * Entrada:
 * - type: tipo logico esperado para a mensagem inteira.
 *
 * Saida:
 * - reassembly: estado zerado e pronto para receber chunks.
 *
 * Retorno:
 * - nenhum. Se reassembly for NULL, a funcao apenas retorna.
 */
void net_fragment_reassembly_init(struct net_fragment_reassembly *reassembly,
                                  uint8_t type);

/*
 * Inicia a remontagem sequencial a partir de um frame de inicio da
 * transferencia.
 *
 * Entrada:
 * - frame: frame de inicio recebido e validado pelo codec.
 *
 * Saida:
 * - reassembly: salva tipo, file_id, tamanho total e proxima sequencia esperada.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se o frame nao for inicio valido para o tipo esperado.
 */
int net_fragment_reassembly_begin(struct net_fragment_reassembly *reassembly,
                                  const struct frame *frame);

/*
 * Consome o proximo frame de chunk sequencial e copia seus dados para o buffer
 * final.
 *
 * Entrada:
 * - frame: frame DATA recebido, ja validado pelo codec.
 * - out_capacity: tamanho do buffer out.
 *
 * Saida:
 * - reassembly: atualiza proxima sequencia esperada e bytes recebidos.
 * - out: recebe os bytes brutos no offset atual do stream.
 * - out_len: recebe o tamanho total quando complete for 1.
 * - complete: recebe 1 quando todos os chunks chegaram; 0 caso contrario.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se o frame nao for um chunk valido da mensagem atual.
 */
int net_fragment_reassembly_accept(
    struct net_fragment_reassembly *reassembly,
    const struct frame *frame,
    uint8_t *out, size_t out_capacity,
    size_t *out_len, int *complete);

/*
 * Consome o proximo frame de chunk sequencial sem copiar para um buffer
 * acumulado.
 *
 * Entrada:
 * - frame: frame DATA recebido, ja validado pelo codec.
 *
 * Saida:
 * - reassembly: atualiza proxima sequencia esperada e bytes recebidos.
 * - chunk_data: aponta para os bytes do payload dentro de frame.
 * - chunk_len: recebe o tamanho do payload.
 * - complete: recebe 1 quando todos os chunks chegaram; 0 caso contrario.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se o frame nao for um chunk valido da mensagem atual.
 */
int net_fragment_reassembly_accept_chunk(
    struct net_fragment_reassembly *reassembly,
    const struct frame *frame,
    const uint8_t **chunk_data, size_t *chunk_len, int *complete);

/*
 * Retorna a sequencia logica da mensagem em remontagem.
 *
 * Entrada:
 * - reassembly: estado de remontagem.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - sequencia base dos chunks ja recebidos.
 * - 0 se reassembly for NULL ou ainda nao tiver iniciado.
 */
uint8_t net_fragment_reassembly_sequence(
    const struct net_fragment_reassembly *reassembly);

/*
 * Retorna o identificador de arquivo informado no inicio da mensagem.
 *
 * Entrada:
 * - reassembly: estado de remontagem.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - identificador de arquivo em 3 bits.
 * - 0 quando nao for arquivo, reassembly for NULL ou nao houver inicio.
 */
uint8_t net_fragment_reassembly_file_id(
    const struct net_fragment_reassembly *reassembly);

#endif
