#ifndef NET_FRAME_H
#define NET_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FRAME_MARKER 0x7eU
#define FRAME_MAX_DATA_LEN 31U
#define FRAME_MAX_SEQUENCE 63U
#define FRAME_MAX_TYPE 31U
#define FRAME_HEADER_BYTES 3U
#define FRAME_CRC_BYTES 1U
#define FRAME_MIN_WIRE_LEN (FRAME_HEADER_BYTES + FRAME_CRC_BYTES)
#define FRAME_MAX_WIRE_LEN (FRAME_MIN_WIRE_LEN + FRAME_MAX_DATA_LEN)

enum frame_type {
    FRAME_TYPE_ACK = 0,
    FRAME_TYPE_NACK = 1,
    FRAME_TYPE_VIEW = 2,
    FRAME_TYPE_INIT = 3,
    FRAME_TYPE_DATA = 4,

    /* Arquivo texto das pastilhas 1 e 2: 1.txt e 2.txt. */
    FRAME_TYPE_FILE_TXT = 5,

    /* Arquivo de imagem das pastilhas 3 e 4: 3.jpg e 4.jpg. */
    FRAME_TYPE_FILE_JPG = 6,

    /* Arquivo de video das pastilhas 5 e 6: 5.mp4 e 6.mp4. */
    FRAME_TYPE_FILE_MP4 = 7,

    FRAME_TYPE_MOVE_RIGHT = 10,
    FRAME_TYPE_MOVE_LEFT = 11,
    FRAME_TYPE_MOVE_UP = 12,
    FRAME_TYPE_MOVE_DOWN = 13,
    FRAME_TYPE_ERROR = 15,
    FRAME_TYPE_END = 16
};

enum frame_error_code {
    FRAME_ERROR_NONE = 0,
    FRAME_ERROR_INVALID_TRANSFER = 1,
    FRAME_ERROR_FILE_UNAVAILABLE = 2,
    FRAME_ERROR_FILE_READ = 3,
    FRAME_ERROR_FILE_WRITE = 4,
    FRAME_ERROR_STORAGE = 5,
    FRAME_ERROR_INTERNAL = 6
};

struct frame {
    uint8_t marker;
    uint8_t length;
    uint8_t sequence;
    uint8_t type;
    uint8_t data[FRAME_MAX_DATA_LEN];
    uint8_t crc;
};

/*
 * Inicializa uma estrutura de frame com marcador padrao e campos basicos.
 *
 * Entrada:
 * - type: tipo da mensagem do protocolo, normalmente um valor de enum frame_type.
 * - sequence: numero de sequencia, esperado no intervalo 0..63.
 *
 * Saida:
 * - frame: estrutura zerada e preenchida com marker, type e sequence.
 *
 * Retorno:
 * - nenhum. Se frame for NULL, a funcao apenas retorna.
 */
void frame_init(struct frame *frame, uint8_t type, uint8_t sequence);

/*
 * Copia dados para o payload do frame respeitando o limite de 5 bits.
 *
 * Entrada:
 * - frame: estrutura que recebera o payload.
 * - data: bytes a copiar; pode ser NULL quando length for 0.
 * - length: quantidade de bytes do payload, esperada no intervalo 0..31.
 *
 * Saida:
 * - frame: recebe data[0..length-1] e length quando os argumentos forem validos.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se frame/data forem invalidos ou length passar de FRAME_MAX_DATA_LEN.
 */
int frame_set_data(struct frame *frame, const uint8_t *data, size_t length);

/*
 * Verifica se um tipo de frame esta definido pelo protocolo.
 *
 * Entrada:
 * - type: valor numerico do campo tipo.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - true se type for um dos tipos conhecidos.
 * - false para valores reservados ou invalidos.
 */
bool frame_is_known_type(uint8_t type);

/*
 * Verifica se um tipo representa metadados de arquivo.
 *
 * Entrada:
 * - type: valor numerico do campo tipo.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - true para FRAME_TYPE_FILE_TXT, FRAME_TYPE_FILE_JPG ou FRAME_TYPE_FILE_MP4.
 * - false para qualquer outro tipo.
 */
bool frame_is_file_type(uint8_t type);

/*
 * Verifica se um tipo pode iniciar uma transferencia fragmentada.
 *
 * Entrada:
 * - type: valor numerico do campo tipo.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - true para VIEW e tipos de arquivo.
 * - false para ACK/NACK/INIT/DATA/tipos reservados/movimentos/ERROR/END.
 */
bool frame_is_transfer_type(uint8_t type);

/*
 * Verifica se um tipo representa comando de movimento do cliente.
 *
 * Entrada:
 * - type: valor numerico do campo tipo.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - true para direita, esquerda, cima ou baixo.
 * - false para qualquer outro tipo.
 */
bool frame_is_move_type(uint8_t type);

/*
 * Retorna um nome estavel e legivel para o tipo de frame.
 *
 * Entrada:
 * - type: valor numerico do campo tipo.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - ponteiro para string constante com o nome do tipo.
 * - "desconhecido" quando type nao estiver mapeado.
 */
const char *frame_type_name(uint8_t type);

/*
 * Retorna a extensao associada a um tipo de arquivo.
 *
 * Entrada:
 * - type: valor numerico do campo tipo.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - ".txt", ".jpg" ou ".mp4" para tipos de arquivo.
 * - string vazia para tipos que nao representam arquivo.
 */
const char *frame_file_extension(uint8_t type);

/*
 * Valida se a estrutura de frame respeita os limites do protocolo.
 *
 * Entrada:
 * - frame: ponteiro para a estrutura a validar.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - true se marker, length, sequence e type forem validos.
 * - false se frame for NULL ou algum campo sair do contrato do protocolo.
 */
bool frame_has_valid_shape(const struct frame *frame);

/*
 * Calcula quantos bytes o frame ocupa quando serializado no fio.
 *
 * Entrada:
 * - frame: ponteiro para a estrutura a medir.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - FRAME_MIN_WIRE_LEN + frame->length para frames validos.
 * - 0 quando frame for invalido.
 */
size_t frame_wire_size(const struct frame *frame);

#endif
