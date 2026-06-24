#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "net/protocol_limits.h"

#include <stddef.h>
#include <stdio.h>

#define APP_CONFIG_DEFAULT_PATH "config/game.txt"
#define APP_CONFIG_MAX_PATH_LEN 256U
#define APP_CONFIG_MAX_INTERFACE_LEN 64U
#define APP_CONFIG_MIN_FILE_WINDOW_SIZE \
    ((int)NET_PROTOCOL_MIN_TRANSFER_WINDOW_SIZE)
#define APP_CONFIG_MAX_FILE_WINDOW_SIZE \
    ((int)NET_PROTOCOL_MAX_TRANSFER_WINDOW_SIZE)
#define APP_CONFIG_MIN_VISUALIZATION_SIZE 1
#define APP_CONFIG_MAX_VISUALIZATION_SIZE 5
#define APP_CONFIG_MIN_MOVES_PER_INCREASE 1
#define APP_CONFIG_MAX_MOVES_PER_INCREASE 1000
#define APP_CONFIG_MIN_BITFLIP_PERCENT 0
#define APP_CONFIG_MAX_BITFLIP_PERCENT 100

enum app_game_mode {
    APP_GAME_MODE_CLEAR = 0,
    APP_GAME_MODE_FLASHLIGHT,
    APP_GAME_MODE_EXPLORE
};

enum app_role {
    APP_ROLE_UNSET = 0,
    APP_ROLE_SERVER,
    APP_ROLE_CLIENT
};

struct app_runtime_config {
    enum app_game_mode game_mode;
    enum app_role default_role;
    int file_window_size;
    int visualization_size;
    int moves_per_visibility_increase;
    int bitflip_percent_set;
    int bitflip_percent;
    int bitflip_seed_set;
    unsigned int bitflip_seed;
    char map_path[APP_CONFIG_MAX_PATH_LEN];
    char interface_name[APP_CONFIG_MAX_INTERFACE_LEN];
};

/*
 * Preenche a configuracao com os padroes do projeto.
 *
 * Entrada:
 * - nenhuma.
 *
 * Saida:
 * - cfg: recebe valores padrao para jogo, rede e mapa.
 *
 * Retorno:
 * - nenhum. Se cfg for NULL, a funcao apenas retorna.
 */
void app_config_set_defaults(struct app_runtime_config *cfg);

/*
 * Carrega um arquivo texto de configuracao no formato chave=valor.
 *
 * Entrada:
 * - path: caminho do arquivo. Se for NULL, usa APP_CONFIG_DEFAULT_PATH.
 * - err_len: tamanho do buffer err.
 *
 * Saida:
 * - cfg: recebe os padroes e os valores sobrescritos pelo arquivo.
 * - err: recebe mensagem curta em caso de erro.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se o arquivo nao abrir, tiver chave/valor invalido ou falhar validacao.
 */
int app_config_load(struct app_runtime_config *cfg, const char *path,
                    char *err, size_t err_len);

/*
 * Salva uma configuracao no formato texto chave=valor.
 *
 * Entrada:
 * - cfg: configuracao ja validada ou a validar antes da escrita.
 * - path: caminho do arquivo. Se for NULL, usa APP_CONFIG_DEFAULT_PATH.
 * - err_len: tamanho do buffer err.
 *
 * Saida:
 * - err: recebe mensagem curta em caso de erro.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se cfg for invalida ou se a escrita falhar.
 */
int app_config_save(const struct app_runtime_config *cfg, const char *path,
                    char *err, size_t err_len);

/*
 * Imprime a configuracao em formato legivel para o usuario.
 *
 * Entrada:
 * - cfg: configuracao a exibir.
 * - stream: destino da impressao. Se for NULL, usa stdout.
 *
 * Saida:
 * - stream: recebe as linhas formatadas.
 *
 * Retorno:
 * - nenhum. Se cfg for NULL, a funcao apenas retorna.
 */
void app_config_print(const struct app_runtime_config *cfg, FILE *stream);

/*
 * Valida se a configuracao respeita os limites atuais do protocolo.
 *
 * Entrada:
 * - cfg: configuracao a validar.
 * - err_len: tamanho do buffer err.
 *
 * Saida:
 * - err: recebe mensagem curta em caso de erro.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se algum campo for invalido.
 */
int app_config_validate(const struct app_runtime_config *cfg,
                        char *err, size_t err_len);

/*
 * Atualiza o caminho do mapa na configuracao.
 *
 * Entrada:
 * - cfg: configuracao a alterar.
 * - path: novo caminho do mapa.
 * - err_len: tamanho do buffer err.
 *
 * Saida:
 * - cfg->map_path: recebe path quando valido.
 * - err: recebe mensagem curta em caso de erro.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se cfg/path forem invalidos ou se o caminho for longo demais.
 */
int app_config_set_map_path(struct app_runtime_config *cfg, const char *path,
                            char *err, size_t err_len);

/*
 * Atualiza o nome da interface cabeada na configuracao.
 *
 * Entrada:
 * - cfg: configuracao a alterar.
 * - name: nome da interface, por exemplo "enp3s0"; pode ser vazio para
 *   perguntar ao iniciar.
 * - err_len: tamanho do buffer err.
 *
 * Saida:
 * - cfg->interface_name: recebe name quando valido.
 * - err: recebe mensagem curta em caso de erro.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se cfg/name forem invalidos ou se o nome for longo demais.
 */
int app_config_set_interface_name(struct app_runtime_config *cfg,
                                  const char *name,
                                  char *err, size_t err_len);

/*
 * Converte modo de jogo para texto salvo no arquivo.
 *
 * Entrada:
 * - mode: valor do enum app_game_mode.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - "claro", "lanterna", "expo" ou "desconhecido".
 */
const char *app_game_mode_name(enum app_game_mode mode);

/*
 * Converte papel da maquina para texto salvo no arquivo.
 *
 * Entrada:
 * - role: valor do enum app_role.
 *
 * Saida:
 * - nenhuma.
 *
 * Retorno:
 * - "servidor", "cliente" ou "indefinido".
 */
const char *app_role_name(enum app_role role);

/*
 * Converte texto para modo de jogo.
 *
 * Entrada:
 * - text: "claro", "lanterna" ou "expo".
 *
 * Saida:
 * - out: recebe o modo convertido.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se text/out forem invalidos ou o valor for desconhecido.
 */
int app_game_mode_parse(const char *text, enum app_game_mode *out);

/*
 * Converte texto para papel da maquina.
 *
 * Entrada:
 * - text: "servidor" ou "cliente".
 *
 * Saida:
 * - out: recebe o papel convertido.
 *
 * Retorno:
 * - 0 em sucesso.
 * - -1 se text/out forem invalidos ou o valor for desconhecido.
 */
int app_role_parse(const char *text, enum app_role *out);

#endif
