#ifndef NET_FRAME_CODEC_H
#define NET_FRAME_CODEC_H

#include "net/frame.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Serializa o frame para o formato de rede do protocolo.
 *
 * Entrada:
 * - frame: estrutura validada a serializar.
 * - out_len: tamanho do buffer out.
 *
 * Saida:
 * - out: recebe marcador, campos empacotados, payload e CRC-8 calculado.
 *
 * Retorno:
 * - quantidade de bytes escritos em out.
 * - 0 se frame/out forem invalidos ou o buffer for pequeno.
 */
size_t frame_encode(const struct frame *frame, uint8_t *out, size_t out_len);

/*
 * Decodifica um frame do formato de rede para struct frame.
 *
 * Entrada:
 * - wire: bytes recebidos da rede.
 * - wire_len: quantidade de bytes disponiveis. Pode incluir padding Ethernet
 *   depois do CRC; os bytes excedentes sao ignorados.
 *
 * Saida:
 * - frame: recebe os campos decodificados quando o buffer for valido.
 *
 * Retorno:
 * - true se o frame tiver marcador, tamanho, sequencia, tipo e CRC validos.
 * - false se wire/frame forem invalidos, se faltarem bytes ou se o CRC falhar.
 */
bool frame_decode(struct frame *frame, const uint8_t *wire, size_t wire_len);

#endif
