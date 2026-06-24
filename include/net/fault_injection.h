#ifndef NET_FAULT_INJECTION_H
#define NET_FAULT_INJECTION_H

#include <stddef.h>
#include <stdint.h>

/*
 * Configura a injecao de bit flip para testes de CRC/NACK.
 *
 * Entrada:
 * - percent: chance de corromper cada frame enviado, no intervalo 0..100.
 * - has_seed: 1 para usar seed fixa; 0 para gerar seed automaticamente.
 * - seed: valor usado quando has_seed for 1.
 *
 * Saida:
 * - estado interno do injetor passa a usar essa configuracao.
 * - stdout recebe um log quando percent for maior que zero.
 *
 * Retorno:
 * - nenhum.
 */
void net_fault_configure_bitflip(unsigned int percent, int has_seed,
                                 unsigned int seed);

/*
 * Injeta um bit flip aleatorio em um frame ja codificado, quando habilitado.
 *
 * Entrada:
 * - frame: bytes do frame ja serializado, incluindo CRC.
 * - frame_len: quantidade de bytes em frame.
 * - label: texto curto para log do pacote, pode ser NULL.
 *
 * Saida:
 * - frame: pode ter um unico bit invertido em uma posicao aleatoria.
 * - stdout: quando habilitado, registra seed e cada bit flip aplicado.
 *
 * Retorno:
 * - 1 quando aplicou bit flip.
 * - 0 quando a injecao esta desabilitada, nao sorteada ou entrada invalida.
 *
 * Configuracao:
 * - a injecao so existe depois de net_fault_configure_bitflip().
 */
int net_fault_maybe_flip_bit(uint8_t *frame, size_t frame_len,
                             const char *label);

#endif
