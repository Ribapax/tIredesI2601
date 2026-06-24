#ifndef NET_WINDOW_H
#define NET_WINDOW_H

#include "net/fragment.h"
#include "net/frame.h"
#include "net/protocol_limits.h"

#include <stddef.h>
#include <stdint.h>

#define NET_WINDOW_MIN_SIZE NET_PROTOCOL_MIN_TRANSFER_WINDOW_SIZE
#define NET_WINDOW_MAX_SIZE NET_PROTOCOL_MAX_TRANSFER_WINDOW_SIZE

enum net_window_receive_status {
    NET_WINDOW_RX_ACCEPTED = 0,
    NET_WINDOW_RX_DUPLICATE,
    NET_WINDOW_RX_INVALID
};

struct net_window_sender_slot {
    struct frame frame;
    size_t chunk_index;
    uint8_t in_use;
    uint8_t acked;
};

struct net_window_sender {
    size_t window_size;
    size_t total_chunks;
    size_t base_index;
    size_t next_index;
    struct net_window_sender_slot slots[NET_WINDOW_MAX_SIZE];
};

struct net_window_receiver_slot {
    uint8_t data[NET_FRAGMENT_DATA_BYTES];
    size_t length;
    size_t chunk_index;
    uint8_t received;
};

struct net_window_receiver {
    uint8_t type;
    uint8_t file_id;
    uint8_t base_sequence;
    uint8_t started;
    uint32_t total_bytes;
    uint32_t delivered_bytes;
    size_t total_chunks;
    size_t window_size;
    size_t base_index;
    struct net_window_receiver_slot slots[NET_WINDOW_MAX_SIZE];
};

/*
 * Verifica se o tamanho de janela cabe no protocolo atual.
 *
 * Entrada:
 * - window_size: quantidade maxima de frames DATA pendentes.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - 1 quando a janela esta no intervalo definido pelo protocolo.
 * - 0 quando a janela e invalida.
 */
int net_window_size_is_valid(size_t window_size);

/*
 * Calcula a sequencia de um chunk dentro de uma mensagem.
 *
 * Entrada:
 * - base_sequence: sequencia do frame de inicio da transferencia.
 * - chunk_index: indice do chunk DATA, com primeiro chunk em 0.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - sequencia de 6 bits usada no frame DATA daquele chunk.
 */
uint8_t net_window_sequence_for_index(uint8_t base_sequence,
                                      size_t chunk_index);

/*
 * Inicializa o estado de envio com janela deslizante.
 *
 * Entrada:
 * - window_size: tamanho da janela; use 1 para stop-and-wait.
 * - total_chunks: quantidade total de frames DATA da mensagem.
 *
 * Saida:
 * - sender: estado zerado para controlar ACK/NACK dos chunks.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se argumentos forem invalidos.
 */
int net_window_sender_init(struct net_window_sender *sender,
                           size_t window_size, size_t total_chunks);

/*
 * Informa se ainda ha espaco para enfileirar novo frame DATA.
 *
 * Entrada:
 * - sender: estado de envio.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - 1 quando ha chunk restante e janela disponivel.
 * - 0 quando a janela esta cheia ou a mensagem ja foi toda enfileirada.
 */
int net_window_sender_can_queue(const struct net_window_sender *sender);

/*
 * Retorna o proximo indice de chunk a ser construido.
 *
 * Entrada:
 * - sender: estado de envio.
 *
 * Saida:
 * - chunk_index: recebe o indice a montar.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se nao houver espaco na janela ou os argumentos forem invalidos.
 */
int net_window_sender_next_index(const struct net_window_sender *sender,
                                 size_t *chunk_index);

/*
 * Guarda uma copia de um frame DATA pendente de ACK.
 *
 * Entrada:
 * - sender: estado de envio.
 * - frame: frame DATA ja montado.
 * - chunk_index: indice do chunk representado por frame.
 *
 * Saida:
 * - sender: passa a rastrear o frame para ACK, NACK e retransmissao.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se o frame nao puder entrar na janela atual.
 */
int net_window_sender_queue(struct net_window_sender *sender,
                            const struct frame *frame,
                            size_t chunk_index);

/*
 * Marca frames pendentes como confirmados ate a sequencia recebida e desliza a
 * janela.
 *
 * Entrada:
 * - sender: estado de envio.
 * - sequence: sequencia recebida em um ACK cumulativo.
 *
 * Saida:
 * - sender: marca o chunk indicado e todos os anteriores como ACKed.
 * - chunk_index: recebe o indice cumulativamente confirmado quando encontrado.
 *
 * Retorno:
 * - 1 se a sequencia pertencia a janela atual.
 * - 0 se a sequencia nao estava pendente.
 * - -1 se os argumentos forem invalidos.
 */
int net_window_sender_ack(struct net_window_sender *sender,
                          uint8_t sequence, size_t *chunk_index);

/*
 * Localiza o frame pendente indicado por um NACK cumulativo.
 *
 * Entrada:
 * - sender: estado de envio.
 * - sequence: sequencia recebida em um NACK. Todos os chunks anteriores sao
 *   considerados confirmados.
 *
 * Saida:
 * - sender: marca como ACKed todos os chunks anteriores ao NACK.
 * - frame: aponta para o frame que deve ser retransmitido.
 * - chunk_index: recebe o indice do chunk quando encontrado.
 *
 * Retorno:
 * - 1 se a sequencia pertencia a um frame pendente.
 * - 0 se a sequencia nao estava pendente.
 * - -1 se os argumentos forem invalidos.
 */
int net_window_sender_nack(struct net_window_sender *sender,
                           uint8_t sequence, const struct frame **frame,
                           size_t *chunk_index);

/*
 * Verifica se uma sequencia ainda esta pendente na janela de envio.
 *
 * Entrada:
 * - sender: estado de envio.
 * - sequence: sequencia de ACK/NACK recebida.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - 1 quando a sequencia pertence a um frame pendente nao confirmado.
 * - 0 caso contrario.
 */
int net_window_sender_has_sequence(const struct net_window_sender *sender,
                                   uint8_t sequence);

/*
 * Conta quantos frames pendentes ainda precisam de ACK.
 *
 * Entrada:
 * - sender: estado de envio.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - quantidade de frames pendentes nao confirmados.
 */
size_t net_window_sender_unacked_count(const struct net_window_sender *sender);

/*
 * Retorna um frame pendente por posicao logica para retransmissao.
 *
 * Entrada:
 * - sender: estado de envio.
 * - offset: posicao entre os frames pendentes nao confirmados.
 *
 * Saida:
 * - frame: aponta para o frame pendente.
 * - chunk_index: recebe o indice do chunk.
 *
 * Retorno:
 * - 1 quando offset encontrou um frame.
 * - 0 quando offset esta fora da quantidade pendente.
 * - -1 se os argumentos forem invalidos.
 */
int net_window_sender_pending_frame(const struct net_window_sender *sender,
                                    size_t offset, const struct frame **frame,
                                    size_t *chunk_index);

/*
 * Informa se todos os chunks ja foram confirmados.
 *
 * Entrada:
 * - sender: estado de envio.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - 1 quando nao ha mais chunks pendentes nem por enviar.
 * - 0 caso contrario.
 */
int net_window_sender_complete(const struct net_window_sender *sender);

/*
 * Inicializa o estado de recebimento com janela deslizante.
 *
 * Entrada:
 * - type: tipo logico esperado da mensagem inteira.
 * - window_size: tamanho da janela; use 1 para stop-and-wait.
 *
 * Saida:
 * - receiver: estado zerado para receber DATA fora de ordem dentro da janela.
 *
 * Retorno:
 * - nenhum. Se receiver for NULL, a funcao apenas retorna.
 */
void net_window_receiver_init(struct net_window_receiver *receiver,
                              uint8_t type, size_t window_size);

/*
 * Inicia o recebimento a partir de um frame de inicio da transferencia.
 *
 * Entrada:
 * - receiver: estado inicializado.
 * - frame: frame de inicio recebido e validado pelo codec.
 *
 * Saida:
 * - receiver: salva sequencia base, tipo, file_id e tamanho total.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se o inicio for invalido para o receptor.
 */
int net_window_receiver_begin(struct net_window_receiver *receiver,
                              const struct frame *frame);

/*
 * Aceita um frame DATA, guardando-o caso esteja dentro da janela.
 *
 * Entrada:
 * - receiver: estado de recebimento iniciado.
 * - frame: frame DATA recebido.
 *
 * Saida:
 * - receiver: guarda chunks novos dentro da janela.
 * - status: indica se o frame foi aceito, duplicado ou invalido.
 *
 * Retorno:
 * - 0 quando status foi preenchido.
 * - -1 se os argumentos forem invalidos.
 */
int net_window_receiver_accept(struct net_window_receiver *receiver,
                               const struct frame *frame,
                               enum net_window_receive_status *status);

/*
 * Retorna a sequencia DATA esperada na base da janela de recebimento.
 *
 * Entrada:
 * - receiver: estado de recebimento iniciado.
 *
 * Saida:
 * - sequence: recebe a sequencia do primeiro chunk ainda nao entregue.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se receiver/sequence forem invalidos ou a mensagem ja estiver completa.
 */
int net_window_receiver_expected_sequence(
    const struct net_window_receiver *receiver, uint8_t *sequence);

/*
 * Verifica se uma sequencia DATA ja foi entregue em ordem.
 *
 * Entrada:
 * - receiver: estado de recebimento iniciado.
 * - sequence: sequencia a consultar.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - 1 se a sequencia pertence a um chunk ja entregue.
 * - 0 caso contrario.
 */
int net_window_receiver_sequence_delivered(
    const struct net_window_receiver *receiver, uint8_t sequence);

/*
 * Consulta o proximo chunk contiguo pronto para entrega.
 *
 * Entrada:
 * - receiver: estado de recebimento.
 *
 * Saida:
 * - data: aponta para os bytes do chunk dentro do receiver.
 * - length: recebe o tamanho do chunk.
 * - chunk_index: recebe o indice contiguo pronto.
 *
 * Retorno:
 * - 1 quando existe chunk pronto.
 * - 0 quando ainda falta o chunk da base da janela.
 * - -1 se os argumentos forem invalidos.
 */
int net_window_receiver_peek_ready(const struct net_window_receiver *receiver,
                                   const uint8_t **data, size_t *length,
                                   size_t *chunk_index);

/*
 * Confirma a entrega do chunk contiguo da base e desliza a janela.
 *
 * Entrada:
 * - receiver: estado de recebimento.
 *
 * Saida:
 * - receiver: remove o chunk entregue e avanca a base.
 *
 * Retorno:
 * - 1 quando um chunk foi removido.
 * - 0 quando nao havia chunk pronto.
 * - -1 se receiver for invalido.
 */
int net_window_receiver_pop_ready(struct net_window_receiver *receiver);

/*
 * Informa se a mensagem inteira ja foi entregue em ordem.
 *
 * Entrada:
 * - receiver: estado de recebimento.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - 1 quando todos os chunks foram entregues.
 * - 0 caso contrario.
 */
int net_window_receiver_complete(const struct net_window_receiver *receiver);

/*
 * Retorna a sequencia base da mensagem recebida.
 *
 * Entrada:
 * - receiver: estado de recebimento.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - sequencia do frame de inicio da transferencia.
 */
uint8_t net_window_receiver_sequence(
    const struct net_window_receiver *receiver);

/*
 * Retorna o identificador de arquivo da mensagem recebida.
 *
 * Entrada:
 * - receiver: estado de recebimento.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - file_id salvo no aviso de transferencia, ou 0 quando nao houver inicio.
 */
uint8_t net_window_receiver_file_id(
    const struct net_window_receiver *receiver);

/*
 * Retorna a quantidade total de chunks DATA da mensagem.
 *
 * Entrada:
 * - receiver: estado de recebimento.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - quantidade de chunks calculada a partir do tamanho total.
 */
size_t net_window_receiver_total_chunks(
    const struct net_window_receiver *receiver);

/*
 * Retorna a quantidade de bytes ja entregue em ordem.
 *
 * Entrada:
 * - receiver: estado de recebimento.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - bytes contiguos entregues ao chamador.
 */
uint32_t net_window_receiver_delivered_bytes(
    const struct net_window_receiver *receiver);

#endif
