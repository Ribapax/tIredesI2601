#define _POSIX_C_SOURCE 200809L

#include "app/config.h"
#include "app/game_session.h"
#include "app/game_session_view_codec.h"
#include "game/map.h"
#include "game/visibility.h"
#include "net/diag.h"
#include "net/fault_injection.h"
#include "net/frame.h"
#include "net/handshake.h"
#include "net/raw_eth.h"
#include "ui/log.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define APP_CLIENT_LOG_DIR "logs"
#define APP_CLIENT_LOG_PATH APP_CLIENT_LOG_DIR "/client.log"

struct cli_options {
    enum app_role role_override;
    const char *iface;
    const char *map_path;
    const char *config_path;
};

struct client_output {
    FILE *ui;
    int close_ui;
};

static void usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "Uso: %s [--config <txt>] [--server|--client] [--iface <interface>] "
            "[--map <csv>]\n"
            "\n"
            "Ao iniciar, o programa carrega %s, mostra as configuracoes e "
            "permite editar/salvar antes de conectar.\n"
            "Sem --server/--client, usa o papel inicial salvo na configuracao.\n"
            "Sem --iface, usa interface_name da configuracao ou pergunta ao "
            "iniciar.\n"
            "Servidor usa o mapa configurado ou --map para montar a primeira "
            "visualizacao apos o handshake.\n"
            "Handshake: timeout=%dms retries=%d. Jogo: timeout inicial=%dms, "
            "dobra a cada %d erros seguidos ate %dms. EtherType fixo=0x%04x.\n",
            program, APP_CONFIG_DEFAULT_PATH,
            HANDSHAKE_TIMEOUT_MS, HANDSHAKE_MAX_RETRIES,
            GAME_SESSION_BASE_TIMEOUT_MS, GAME_SESSION_BACKOFF_ERROR_STEP,
            GAME_SESSION_MAX_TIMEOUT_MS,
            RAW_ETH_DEFAULT_ETHERTYPE);
}

static void strip_newline(char *text)
{
    size_t len;

    if (text == NULL) {
        return;
    }

    len = strlen(text);
    if (len > 0U && text[len - 1U] == '\n') {
        text[len - 1U] = '\0';
    }
}

static int parse_args(int argc, char **argv, struct cli_options *opts)
{
    memset(opts, 0, sizeof(*opts));

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--server") == 0) {
            opts->role_override = APP_ROLE_SERVER;
        } else if (strcmp(argv[i], "--client") == 0) {
            opts->role_override = APP_ROLE_CLIENT;
        } else if (strcmp(argv[i], "--iface") == 0 && i + 1 < argc) {
            opts->iface = argv[++i];
        } else if (strcmp(argv[i], "--map") == 0 && i + 1 < argc) {
            opts->map_path = argv[++i];
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            opts->config_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            usage(stdout, argv[0]);
            return 1;
        } else {
            usage(stderr, argv[0]);
            return -1;
        }
    }

    return 0;
}

static enum game_visibility_mode visibility_mode_from_config(
    enum app_game_mode mode)
{
    switch (mode) {
    case APP_GAME_MODE_CLEAR:
        return GAME_VISIBILITY_MODE_CLEAR;
    case APP_GAME_MODE_EXPLORE:
        return GAME_VISIBILITY_MODE_EXPLORE;
    case APP_GAME_MODE_FLASHLIGHT:
    default:
        return GAME_VISIBILITY_MODE_FLASHLIGHT;
    }
}

static struct client_output open_client_output(void)
{
    struct client_output output;
    int stdout_copy;

    output.ui = fopen("/dev/tty", "w");
    output.close_ui = output.ui != NULL;
    if (output.ui == NULL) {
        stdout_copy = dup(STDOUT_FILENO);
        if (stdout_copy >= 0) {
            output.ui = fdopen(stdout_copy, "w");
            output.close_ui = output.ui != NULL;
            if (output.ui == NULL) {
                (void)close(stdout_copy);
            }
        }
        if (output.ui == NULL) {
            output.ui = stdout;
            output.close_ui = 0;
        }
    }

    return output;
}

static void close_client_output(struct client_output *output)
{
    if (output != NULL && output->close_ui && output->ui != NULL) {
        (void)fclose(output->ui);
        output->ui = NULL;
        output->close_ui = 0;
    }
}

static int ensure_client_log_dir(FILE *ui)
{
    struct stat info;

    if (mkdir(APP_CLIENT_LOG_DIR, 0775) != 0 && errno != EEXIST) {
        fprintf(ui, "erro: nao foi possivel criar %s: %s\n",
                APP_CLIENT_LOG_DIR, strerror(errno));
        fflush(ui);
        return -1;
    }
    if (stat(APP_CLIENT_LOG_DIR, &info) != 0 || !S_ISDIR(info.st_mode)) {
        fprintf(ui, "erro: %s nao e um diretorio de logs valido\n",
                APP_CLIENT_LOG_DIR);
        fflush(ui);
        return -1;
    }

    return 0;
}

static int redirect_client_logs(FILE *ui)
{
    if (ensure_client_log_dir(ui) != 0) {
        return -1;
    }

    fflush(stdout);
    fflush(stderr);

    if (freopen(APP_CLIENT_LOG_PATH, "a", stdout) == NULL) {
        fprintf(ui, "erro: nao foi possivel abrir %s para stdout: %s\n",
                APP_CLIENT_LOG_PATH, strerror(errno));
        fflush(ui);
        return -1;
    }
    if (freopen(APP_CLIENT_LOG_PATH, "a", stderr) == NULL) {
        fprintf(ui, "erro: nao foi possivel abrir %s para stderr: %s\n",
                APP_CLIENT_LOG_PATH, strerror(errno));
        fflush(ui);
        return -1;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    ui_log(stdout, UI_LOG_GAME, "cliente iniciado log=%s\n",
           APP_CLIENT_LOG_PATH);
    return 0;
}

static void show_client_waiting(FILE *ui)
{
    fprintf(ui, "\033[2J\033[Hpacman, aguardando conexao...\n");
    fflush(ui);
}

static int apply_cli_overrides(struct app_runtime_config *cfg,
                               const struct cli_options *opts,
                               char *err, size_t err_len)
{
    if (opts->role_override != APP_ROLE_UNSET) {
        cfg->default_role = opts->role_override;
    }
    if (opts->map_path != NULL &&
        app_config_set_map_path(cfg, opts->map_path, err, err_len) != 0) {
        return -1;
    }
    if (opts->iface != NULL &&
        app_config_set_interface_name(cfg, opts->iface, err, err_len) != 0) {
        return -1;
    }

    return app_config_validate(cfg, err, err_len);
}

static int prompt_role(enum app_role *role)
{
    char answer[32];

    printf("Escolha o papel da maquina:\n");
    printf("  1) servidor\n");
    printf("  2) cliente\n");
    printf("> ");
    fflush(stdout);

    if (fgets(answer, sizeof(answer), stdin) == NULL) {
        return -1;
    }
    strip_newline(answer);

    if (strcmp(answer, "1") == 0 ||
        app_role_parse(answer, role) == 0) {
        if (strcmp(answer, "1") == 0) {
            *role = APP_ROLE_SERVER;
        }
        return 0;
    }
    if (strcmp(answer, "2") == 0) {
        *role = APP_ROLE_CLIENT;
        return 0;
    }

    return -1;
}

static int prompt_iface(char *iface, size_t iface_len)
{
    printf("Interface cabeada: ");
    fflush(stdout);

    if (fgets(iface, iface_len, stdin) == NULL) {
        return -1;
    }
    strip_newline(iface);

    return iface[0] == '\0' ? -1 : 0;
}

static int prompt_use_or_edit(void)
{
    char answer[32];

    printf("Usar estas configuracoes? [S/e] ");
    fflush(stdout);

    if (fgets(answer, sizeof(answer), stdin) == NULL) {
        return -1;
    }
    strip_newline(answer);

    if (answer[0] == '\0' || strcmp(answer, "s") == 0 ||
        strcmp(answer, "S") == 0 || strcmp(answer, "sim") == 0 ||
        strcmp(answer, "SIM") == 0) {
        return 0;
    }
    if (strcmp(answer, "e") == 0 || strcmp(answer, "E") == 0 ||
        strcmp(answer, "editar") == 0 || strcmp(answer, "n") == 0 ||
        strcmp(answer, "N") == 0 || strcmp(answer, "nao") == 0 ||
        strcmp(answer, "NAO") == 0) {
        return 1;
    }

    return -1;
}

static int prompt_save_config(void)
{
    char answer[32];

    printf("Salvar alteracoes no arquivo de configuracao? [S/n] ");
    fflush(stdout);

    if (fgets(answer, sizeof(answer), stdin) == NULL) {
        return -1;
    }
    strip_newline(answer);

    if (answer[0] == '\0' || strcmp(answer, "s") == 0 ||
        strcmp(answer, "S") == 0 || strcmp(answer, "sim") == 0 ||
        strcmp(answer, "SIM") == 0) {
        return 1;
    }
    if (strcmp(answer, "n") == 0 || strcmp(answer, "N") == 0 ||
        strcmp(answer, "nao") == 0 || strcmp(answer, "NAO") == 0) {
        return 0;
    }

    return -1;
}

static int prompt_optional(const char *label, const char *current,
                           char *answer, size_t answer_len)
{
    printf("%s [%s]: ", label, current);
    fflush(stdout);

    if (fgets(answer, answer_len, stdin) == NULL) {
        return -1;
    }
    strip_newline(answer);
    return 0;
}

static int prompt_edit_config(struct app_runtime_config *cfg)
{
    struct app_runtime_config next = *cfg;
    char answer[APP_CONFIG_MAX_PATH_LEN + 64U];
    char current[64];
    char err[256];
    enum app_game_mode mode;
    enum app_role role;
    int number;
    char file_window_label[96];

    printf("Edicao da configuracao. Pressione Enter para manter o valor atual.\n");

    if (prompt_optional("Modo de jogo (claro/lanterna/expo)",
                        app_game_mode_name(next.game_mode),
                        answer, sizeof(answer)) != 0) {
        return -1;
    }
    if (answer[0] != '\0') {
        if (app_game_mode_parse(answer, &mode) != 0) {
            fprintf(stderr, "erro: modo de jogo invalido\n");
            return -1;
        }
        next.game_mode = mode;
    }

    if (prompt_optional("Papel inicial (servidor/cliente)",
                        app_role_name(next.default_role),
                        answer, sizeof(answer)) != 0) {
        return -1;
    }
    if (answer[0] != '\0') {
        if (app_role_parse(answer, &role) != 0) {
            fprintf(stderr, "erro: papel inicial invalido\n");
            return -1;
        }
        next.default_role = role;
    }

    (void)snprintf(file_window_label, sizeof(file_window_label),
                   "Janela de transferencia de arquivo (%d..%d)",
                   APP_CONFIG_MIN_FILE_WINDOW_SIZE,
                   APP_CONFIG_MAX_FILE_WINDOW_SIZE);
    (void)snprintf(current, sizeof(current), "%d", next.file_window_size);
    if (prompt_optional(file_window_label, current,
                        answer, sizeof(answer)) != 0) {
        return -1;
    }
    if (answer[0] != '\0') {
        if (net_parse_int_range(answer, APP_CONFIG_MIN_FILE_WINDOW_SIZE,
                                APP_CONFIG_MAX_FILE_WINDOW_SIZE,
                                &number) != 0) {
            fprintf(stderr,
                    "erro: janela de transferencia de arquivo invalida\n");
            return -1;
        }
        next.file_window_size = number;
    }

    (void)snprintf(current, sizeof(current), "%d", next.visualization_size);
    if (prompt_optional("Tamanho da visualizacao", current,
                        answer, sizeof(answer)) != 0) {
        return -1;
    }
    if (answer[0] != '\0') {
        if (net_parse_int_range(answer, APP_CONFIG_MIN_VISUALIZATION_SIZE,
                                APP_CONFIG_MAX_VISUALIZATION_SIZE,
                                &number) != 0) {
            fprintf(stderr, "erro: tamanho da visualizacao invalido\n");
            return -1;
        }
        next.visualization_size = number;
    }

    (void)snprintf(current, sizeof(current), "%d",
                   next.moves_per_visibility_increase);
    if (prompt_optional("Jogadas para aumentar visualizacao", current,
                        answer, sizeof(answer)) != 0) {
        return -1;
    }
    if (answer[0] != '\0') {
        if (net_parse_int_range(answer, APP_CONFIG_MIN_MOVES_PER_INCREASE,
                                APP_CONFIG_MAX_MOVES_PER_INCREASE,
                                &number) != 0) {
            fprintf(stderr, "erro: quantidade de jogadas invalida\n");
            return -1;
        }
        next.moves_per_visibility_increase = number;
    }

    if (prompt_optional("Caminho do mapa", next.map_path,
                        answer, sizeof(answer)) != 0) {
        return -1;
    }
    if (answer[0] != '\0' &&
        app_config_set_map_path(&next, answer, err, sizeof(err)) != 0) {
        fprintf(stderr, "erro: %s\n", err);
        return -1;
    }

    if (prompt_optional("Interface cabeada", next.interface_name,
                        answer, sizeof(answer)) != 0) {
        return -1;
    }
    if (answer[0] != '\0') {
        if (app_config_set_interface_name(&next, answer,
                                          err, sizeof(err)) != 0) {
            fprintf(stderr, "erro: %s\n", err);
            return -1;
        }
    }

    if (next.bitflip_percent_set) {
        (void)snprintf(current, sizeof(current), "%d",
                       next.bitflip_percent);
    } else {
        (void)snprintf(current, sizeof(current), "%s", "desabilitado");
    }
    if (prompt_optional("Bit flip de teste em % (off desliga)",
                        current, answer, sizeof(answer)) != 0) {
        return -1;
    }
    if (answer[0] != '\0') {
        if (strcmp(answer, "off") == 0 || strcmp(answer, "OFF") == 0 ||
            strcmp(answer, "desligado") == 0 ||
            strcmp(answer, "desabilitado") == 0) {
            next.bitflip_percent_set = 0;
            next.bitflip_seed_set = 0;
        } else {
            if (net_parse_int_range(answer, APP_CONFIG_MIN_BITFLIP_PERCENT,
                                    APP_CONFIG_MAX_BITFLIP_PERCENT,
                                    &number) != 0) {
                fprintf(stderr, "erro: percentual de bit flip invalido\n");
                return -1;
            }
            next.bitflip_percent_set = 1;
            next.bitflip_percent = number;
        }
    }

    if (next.bitflip_percent_set) {
        if (next.bitflip_seed_set) {
            (void)snprintf(current, sizeof(current), "%u",
                           next.bitflip_seed);
        } else {
            (void)snprintf(current, sizeof(current), "%s", "aleatoria");
        }
        if (prompt_optional("Seed do bit flip (off usa aleatoria)",
                            current, answer, sizeof(answer)) != 0) {
            return -1;
        }
        if (answer[0] != '\0') {
            if (strcmp(answer, "off") == 0 || strcmp(answer, "OFF") == 0 ||
                strcmp(answer, "aleatoria") == 0 ||
                strcmp(answer, "random") == 0) {
                next.bitflip_seed_set = 0;
            } else {
                if (net_parse_int_range(answer, 0, 2147483647,
                                        &number) != 0) {
                    fprintf(stderr, "erro: seed de bit flip invalida\n");
                    return -1;
                }
                next.bitflip_seed_set = 1;
                next.bitflip_seed = (unsigned int)number;
            }
        }
    }

    if (app_config_validate(&next, err, sizeof(err)) != 0) {
        fprintf(stderr, "erro: %s\n", err);
        return -1;
    }

    *cfg = next;
    app_config_print(cfg, stdout);
    return 0;
}

int main(int argc, char **argv)
{
    struct cli_options opts;
    struct app_runtime_config cfg;
    const char *config_path;
    char iface_buf[128];
    char err[256];
    int parse_result;
    int config_choice;

    parse_result = parse_args(argc, argv, &opts);
    if (parse_result > 0) {
        return 0;
    }
    if (parse_result < 0) {
        return 2;
    }

    config_path = opts.config_path == NULL ?
                  APP_CONFIG_DEFAULT_PATH : opts.config_path;
    if (app_config_load(&cfg, config_path, err, sizeof(err)) != 0) {
        fprintf(stderr, "aviso: %s\n", err);
        fprintf(stderr, "aviso: usando configuracao padrao em memoria\n");
        app_config_set_defaults(&cfg);
    }

    if (apply_cli_overrides(&cfg, &opts, err, sizeof(err)) != 0) {
        fprintf(stderr, "erro: %s\n", err);
        return 2;
    }

    printf("Arquivo de configuracao: %s\n", config_path);
    app_config_print(&cfg, stdout);
    printf("Protocolo VIEW: compactada=%u bytes\n",
           (unsigned int)GAME_SESSION_VIEW_PACKED_BYTES);

    config_choice = prompt_use_or_edit();
    if (config_choice < 0) {
        fprintf(stderr, "erro: resposta invalida\n");
        return 2;
    }
    if (config_choice > 0) {
        int save_config;

        if (prompt_edit_config(&cfg) != 0) {
            return 2;
        }

        save_config = prompt_save_config();
        if (save_config < 0) {
            fprintf(stderr, "erro: resposta invalida\n");
            return 2;
        }
        if (save_config > 0 &&
            app_config_save(&cfg, config_path, err, sizeof(err)) != 0) {
            fprintf(stderr, "erro: %s\n", err);
            return 1;
        }
    }

    if (cfg.default_role == APP_ROLE_UNSET &&
        prompt_role(&cfg.default_role) != 0) {
        fprintf(stderr, "erro: papel invalido\n");
        return 2;
    }

    if (cfg.interface_name[0] == '\0') {
        if (prompt_iface(iface_buf, sizeof(iface_buf)) != 0) {
            fprintf(stderr, "erro: interface invalida\n");
            return 2;
        }
        if (app_config_set_interface_name(&cfg, iface_buf,
                                          err, sizeof(err)) != 0) {
            fprintf(stderr, "erro: %s\n", err);
            return 2;
        }
    }

    if (cfg.bitflip_percent_set) {
        net_fault_configure_bitflip((unsigned int)cfg.bitflip_percent,
                                    cfg.bitflip_seed_set,
                                    cfg.bitflip_seed);
    }

    if (cfg.default_role == APP_ROLE_SERVER) {
        struct game_map map;
        struct frame init;
        struct handshake_server_session session;
        int rc;

        if (game_map_load(&map, cfg.map_path, err, sizeof(err)) != 0) {
            fprintf(stderr, "erro: %s\n", err);
            return 1;
        }
        frame_init(&init, FRAME_TYPE_INIT, HANDSHAKE_INIT_SEQUENCE);
        if (frame_set_data(&init, NULL, 0U) != 0) {
            fprintf(stderr, "erro: falha ao montar INIT\n");
            return 1;
        }
        if (handshake_establish_server(cfg.interface_name,
                                       RAW_ETH_DEFAULT_ETHERTYPE, &init,
                                       &session) != 0) {
            return 1;
        }
        rc = game_session_run_server(&session, &map,
                                     visibility_mode_from_config(cfg.game_mode),
                                     (size_t)cfg.visualization_size,
                                     cfg.moves_per_visibility_increase,
                                     (size_t)cfg.file_window_size);
        handshake_server_session_close(&session);
        return rc;
    }
    if (cfg.default_role == APP_ROLE_CLIENT) {
        struct client_output output = open_client_output();
        struct handshake_client_session session;
        int rc;

        game_session_set_ui_stream(output.ui);
        if (redirect_client_logs(output.ui) != 0) {
            close_client_output(&output);
            return 1;
        }
        show_client_waiting(output.ui);

        if (handshake_establish_client(cfg.interface_name,
                                       RAW_ETH_DEFAULT_ETHERTYPE,
                                       &session) != 0) {
            fprintf(output.ui, "\033[2J\033[Hpacman, falha na conexao.\n");
            fflush(output.ui);
            close_client_output(&output);
            return 1;
        }
        rc = game_session_run_client(&session,
                                     (size_t)cfg.file_window_size);
        handshake_client_session_close(&session);
        close_client_output(&output);
        return rc;
    }

    return 2;
}
