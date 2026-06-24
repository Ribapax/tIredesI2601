#ifndef NET_PROTOCOL_LIMITS_H
#define NET_PROTOCOL_LIMITS_H

#include "net/frame.h"

/*
 * Limites compartilhados do protocolo de transferencia.
 *
 * A sequencia do frame tem 6 bits. A janela maxima fica abaixo da metade do
 * espaco de sequencia para evitar ambiguidade entre frames novos e atrasados.
 */
#define NET_PROTOCOL_MIN_TRANSFER_WINDOW_SIZE 1U
#define NET_PROTOCOL_SEQUENCE_SPACE_SIZE (FRAME_MAX_SEQUENCE + 1U)
#define NET_PROTOCOL_SELECTIVE_REPEAT_MAX_WINDOW_SIZE \
    (NET_PROTOCOL_SEQUENCE_SPACE_SIZE / 2U)
#define NET_PROTOCOL_MAX_TRANSFER_WINDOW_SIZE 30U

#if NET_PROTOCOL_MAX_TRANSFER_WINDOW_SIZE >= \
    NET_PROTOCOL_SELECTIVE_REPEAT_MAX_WINDOW_SIZE
#error "selective-repeat transfer window must stay below half the sequence space"
#endif

#endif
