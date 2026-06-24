#ifndef APP_FILE_TRANSFER_H
#define APP_FILE_TRANSFER_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define GAME_FILE_TRANSFER_DEFAULT_OUTPUT_DIR "received"
#define GAME_FILE_TRANSFER_MAX_PATH_LEN 512U

struct game_file_transfer_metadata {
    char cell;
    uint8_t frame_type;
    uint8_t file_id;
    const char *file_name;
    const char *source_path;
};

typedef int (*game_file_transfer_opener)(const char *path, void *user_data);

/*
 * Resolve os metadados do arquivo associado a uma pastilha ou encontro do mapa.
 *
 * Entrada:
 * - cell: simbolo da celula, esperado no intervalo '1'..'6' ou em R/B/G/Y.
 *
 * Saida:
 * - metadata: recebe tipo de frame, file_id, nome e caminho de origem.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se cell nao representar uma transferencia de arquivo ou metadata for
 *   NULL.
 */
int game_file_transfer_metadata_for_cell(
    char cell, struct game_file_transfer_metadata *metadata);

/*
 * Resolve os metadados do arquivo descrito no inicio de uma mensagem recebida.
 *
 * Entrada:
 * - frame_type: tipo logico do arquivo, como FRAME_TYPE_FILE_TXT/JPG/MP4.
 * - file_id: identificador de arquivo informado no aviso de transferencia.
 *
 * Saida:
 * - metadata: recebe tipo de frame, file_id, nome e caminho de origem.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se tipo e identificador nao forem compativeis.
 */
int game_file_transfer_metadata_for_message(
    uint8_t frame_type, uint8_t file_id,
    struct game_file_transfer_metadata *metadata);

/*
 * Monta o caminho local onde o cliente deve gravar o arquivo recebido.
 *
 * Entrada:
 * - metadata: metadados do arquivo.
 * - output_dir: diretorio destino. Se NULL, usa o diretorio padrao.
 * - out_len: tamanho do buffer out.
 *
 * Saida:
 * - out: recebe caminho no formato diretorio/nome-do-arquivo.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se argumentos forem invalidos ou o caminho nao couber.
 */
int game_file_transfer_output_path(
    const struct game_file_transfer_metadata *metadata,
    const char *output_dir, char *out, size_t out_len);

/*
 * Define a funcao usada para abrir arquivos recebidos.
 *
 * Entrada:
 * - opener: callback chamado com o caminho gravado. Se NULL, desabilita a
 *   abertura automatica.
 * - user_data: contexto repassado ao callback.
 *
 * Saida:
 * - estado global do opener da sessao e atualizado.
 *
 * Retorno:
 * - nenhum.
 */
void game_file_transfer_set_opener(game_file_transfer_opener opener,
                                   void *user_data);

/*
 * Restaura o opener padrao, que usa chamada de sistema para abrir o arquivo.
 *
 * Entrada:
 * - nenhuma.
 *
 * Saida:
 * - estado global do opener volta ao padrao.
 *
 * Retorno:
 * - nenhum.
 */
void game_file_transfer_reset_opener(void);

/*
 * Grava bytes recebidos pelo cliente e abre o arquivo gravado.
 *
 * Entrada:
 * - metadata: metadados do arquivo recebido.
 * - data: bytes reconstituidos.
 * - data_len: quantidade de bytes reconstituidos.
 * - output_dir: diretorio destino. Se NULL, usa o diretorio padrao.
 * - path_out_len: tamanho do buffer path_out.
 *
 * Saida:
 * - path_out: recebe o caminho gravado quando nao for NULL.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se argumentos forem invalidos, se a escrita falhar ou se o opener falhar.
 */
int game_file_transfer_store_and_open_received(
    const struct game_file_transfer_metadata *metadata,
    const uint8_t *data, size_t data_len,
    const char *output_dir, char *path_out, size_t path_out_len);

/*
 * Abre o arquivo de destino para escrita incremental no cliente.
 *
 * Entrada:
 * - metadata: metadados do arquivo recebido.
 * - output_dir: diretorio destino. Se NULL, usa o diretorio padrao.
 * - path_out_len: tamanho do buffer path_out.
 *
 * Saida:
 * - path_out: recebe o caminho que esta aberto para escrita.
 * - file_out: recebe FILE* aberto em modo binario.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se argumentos forem invalidos ou se diretorio/arquivo falhar.
 */
int game_file_transfer_open_received_file(
    const struct game_file_transfer_metadata *metadata,
    const char *output_dir, char *path_out, size_t path_out_len,
    FILE **file_out);

/*
 * Chama o opener configurado para um arquivo ja gravado.
 *
 * Entrada:
 * - path: caminho local do arquivo recebido.
 *
 * Saida:
 * - pode iniciar um processo externo pelo opener padrao ou injetado.
 *
 * Retorno:
 * - 0 em sucesso ou quando o opener estiver desabilitado.
 * - -1 se path/opener falharem.
 */
int game_file_transfer_open_received_path(const char *path);

#endif
