#include "app/config.h"

#include "game/map.h"
#include "game/visibility.h"
#include "net/window.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_CONFIG_DEFAULT_FILE_WINDOW_SIZE NET_WINDOW_MAX_SIZE
#define APP_CONFIG_DEFAULT_MOVES_PER_INCREASE 5
#define APP_CONFIG_MAX_LINE_LEN 512U

static void set_error(char *err, size_t err_len, const char *fmt, ...)
{
    va_list args;

    if (err == NULL || err_len == 0U) {
        return;
    }

    va_start(args, fmt);
    (void)vsnprintf(err, err_len, fmt, args);
    va_end(args);
}

static char *trim(char *text)
{
    char *end;

    if (text == NULL) {
        return NULL;
    }

    while (isspace((unsigned char)*text)) {
        text++;
    }
    if (*text == '\0') {
        return text;
    }

    end = text + strlen(text) - 1U;
    while (end > text && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    return text;
}

static int parse_int_range(const char *text, int min, int max, int *out)
{
    long value;
    char *end;

    if (text == NULL || out == NULL || min > max) {
        return -1;
    }

    errno = 0;
    value = strtol(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' ||
        value < (long)min || value > (long)max) {
        return -1;
    }

    *out = (int)value;
    return 0;
}

static int apply_key_value(struct app_runtime_config *cfg,
                           const char *key, const char *value,
                           size_t line_number, char *err, size_t err_len)
{
    enum app_game_mode mode;
    enum app_role role;
    int number;

    if (strcmp(key, "game_mode") == 0) {
        if (app_game_mode_parse(value, &mode) != 0) {
            set_error(err, err_len, "linha %zu: modo de jogo invalido",
                      line_number);
            return -1;
        }
        cfg->game_mode = mode;
        return 0;
    }

    if (strcmp(key, "default_role") == 0) {
        if (app_role_parse(value, &role) != 0) {
            set_error(err, err_len, "linha %zu: papel invalido", line_number);
            return -1;
        }
        cfg->default_role = role;
        return 0;
    }

    if (strcmp(key, "file_window_size") == 0) {
        if (parse_int_range(value, APP_CONFIG_MIN_FILE_WINDOW_SIZE,
                            APP_CONFIG_MAX_FILE_WINDOW_SIZE, &number) != 0) {
            set_error(err, err_len,
                      "linha %zu: janela de arquivo deve estar entre %d e %d",
                      line_number, APP_CONFIG_MIN_FILE_WINDOW_SIZE,
                      APP_CONFIG_MAX_FILE_WINDOW_SIZE);
            return -1;
        }
        cfg->file_window_size = number;
        return 0;
    }

    if (strcmp(key, "visualization_size") == 0) {
        if (parse_int_range(value, APP_CONFIG_MIN_VISUALIZATION_SIZE,
                            APP_CONFIG_MAX_VISUALIZATION_SIZE, &number) != 0) {
            set_error(err, err_len,
                      "linha %zu: tamanho da visualizacao invalido",
                      line_number);
            return -1;
        }
        cfg->visualization_size = number;
        return 0;
    }

    if (strcmp(key, "moves_per_visibility_increase") == 0) {
        if (parse_int_range(value, APP_CONFIG_MIN_MOVES_PER_INCREASE,
                            APP_CONFIG_MAX_MOVES_PER_INCREASE,
                            &number) != 0) {
            set_error(err, err_len,
                      "linha %zu: quantidade de jogadas invalida",
                      line_number);
            return -1;
        }
        cfg->moves_per_visibility_increase = number;
        return 0;
    }

    if (strcmp(key, "map_path") == 0) {
        return app_config_set_map_path(cfg, value, err, err_len);
    }

    if (strcmp(key, "interface_name") == 0) {
        return app_config_set_interface_name(cfg, value, err, err_len);
    }

    if (strcmp(key, "bitflip_percent") == 0) {
        if (parse_int_range(value, APP_CONFIG_MIN_BITFLIP_PERCENT,
                            APP_CONFIG_MAX_BITFLIP_PERCENT, &number) != 0) {
            set_error(err, err_len,
                      "linha %zu: percentual de bit flip invalido",
                      line_number);
            return -1;
        }
        cfg->bitflip_percent_set = 1;
        cfg->bitflip_percent = number;
        return 0;
    }

    if (strcmp(key, "bitflip_seed") == 0) {
        if (parse_int_range(value, 0, 2147483647, &number) != 0) {
            set_error(err, err_len,
                      "linha %zu: seed de bit flip invalida",
                      line_number);
            return -1;
        }
        cfg->bitflip_seed_set = 1;
        cfg->bitflip_seed = (unsigned int)number;
        return 0;
    }

    set_error(err, err_len, "linha %zu: chave desconhecida '%s'",
              line_number, key);
    return -1;
}

void app_config_set_defaults(struct app_runtime_config *cfg)
{
    if (cfg == NULL) {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->game_mode = APP_GAME_MODE_FLASHLIGHT;
    cfg->default_role = APP_ROLE_SERVER;
    cfg->file_window_size = APP_CONFIG_DEFAULT_FILE_WINDOW_SIZE;
    cfg->visualization_size = (int)GAME_INITIAL_VIEW_SIZE;
    cfg->moves_per_visibility_increase =
        APP_CONFIG_DEFAULT_MOVES_PER_INCREASE;
    (void)snprintf(cfg->map_path, sizeof(cfg->map_path), "%s",
                   GAME_DEFAULT_MAP_PATH);
}

int app_config_load(struct app_runtime_config *cfg, const char *path,
                    char *err, size_t err_len)
{
    const char *chosen_path = path == NULL ? APP_CONFIG_DEFAULT_PATH : path;
    FILE *file;
    char line[APP_CONFIG_MAX_LINE_LEN];
    size_t line_number = 0U;

    if (cfg == NULL) {
        set_error(err, err_len, "configuracao de saida ausente");
        return -1;
    }

    app_config_set_defaults(cfg);

    file = fopen(chosen_path, "r");
    if (file == NULL) {
        set_error(err, err_len, "nao abriu configuracao '%s': %s",
                  chosen_path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *content;
        char *separator;
        char *key;
        char *value;

        line_number++;
        line[strcspn(line, "\n")] = '\0';
        content = trim(line);

        if (content[0] == '\0' || content[0] == '#') {
            continue;
        }

        separator = strchr(content, '=');
        if (separator == NULL) {
            set_error(err, err_len, "linha %zu: esperado chave=valor",
                      line_number);
            (void)fclose(file);
            return -1;
        }

        *separator = '\0';
        key = trim(content);
        value = trim(separator + 1);
        if (key[0] == '\0') {
            set_error(err, err_len, "linha %zu: chave vazia", line_number);
            (void)fclose(file);
            return -1;
        }

        if (apply_key_value(cfg, key, value, line_number,
                            err, err_len) != 0) {
            (void)fclose(file);
            return -1;
        }
    }

    if (ferror(file)) {
        set_error(err, err_len, "erro lendo configuracao '%s'", chosen_path);
        (void)fclose(file);
        return -1;
    }
    (void)fclose(file);

    return app_config_validate(cfg, err, err_len);
}

int app_config_save(const struct app_runtime_config *cfg, const char *path,
                    char *err, size_t err_len)
{
    const char *chosen_path = path == NULL ? APP_CONFIG_DEFAULT_PATH : path;
    FILE *file;

    if (app_config_validate(cfg, err, err_len) != 0) {
        return -1;
    }

    file = fopen(chosen_path, "w");
    if (file == NULL) {
        set_error(err, err_len, "nao abriu configuracao '%s' para escrita: %s",
                  chosen_path, strerror(errno));
        return -1;
    }

    fprintf(file, "# Configuracao do PacMan Redes I.\n");
    fprintf(file, "# Formato: chave=valor.\n");
    fprintf(file, "game_mode=%s\n", app_game_mode_name(cfg->game_mode));
    fprintf(file, "default_role=%s\n", app_role_name(cfg->default_role));
    fprintf(file, "file_window_size=%d\n", cfg->file_window_size);
    fprintf(file, "visualization_size=%d\n", cfg->visualization_size);
    fprintf(file, "moves_per_visibility_increase=%d\n",
            cfg->moves_per_visibility_increase);
    fprintf(file, "map_path=%s\n", cfg->map_path);
    fprintf(file, "interface_name=%s\n", cfg->interface_name);
    if (cfg->bitflip_percent_set) {
        fprintf(file, "bitflip_percent=%d\n", cfg->bitflip_percent);
        if (cfg->bitflip_seed_set) {
            fprintf(file, "bitflip_seed=%u\n", cfg->bitflip_seed);
        }
    }

    if (ferror(file)) {
        set_error(err, err_len, "erro escrevendo configuracao '%s'",
                  chosen_path);
        (void)fclose(file);
        return -1;
    }
    if (fclose(file) != 0) {
        set_error(err, err_len, "erro fechando configuracao '%s': %s",
                  chosen_path, strerror(errno));
        return -1;
    }

    return 0;
}

void app_config_print(const struct app_runtime_config *cfg, FILE *stream)
{
    FILE *out = stream == NULL ? stdout : stream;

    if (cfg == NULL) {
        return;
    }

    fprintf(out, "Configuracoes atuais:\n");
    fprintf(out, "  modo de jogo: %s\n",
            app_game_mode_name(cfg->game_mode));
    fprintf(out, "  papel inicial: %s\n",
            app_role_name(cfg->default_role));
    fprintf(out, "  janela de transferencia de arquivo: %d\n",
            cfg->file_window_size);
    fprintf(out, "  diametro da mascara de visualizacao: %d\n",
            cfg->visualization_size);
    fprintf(out, "  jogadas para aumentar visualizacao: %d\n",
            cfg->moves_per_visibility_increase);
    fprintf(out, "  mapa: %s\n", cfg->map_path);
    fprintf(out, "  interface cabeada: %s\n",
            cfg->interface_name[0] == '\0' ?
                "(perguntar ao iniciar)" : cfg->interface_name);
    if (cfg->bitflip_percent_set) {
        fprintf(out, "  bit flip de teste: %d%%", cfg->bitflip_percent);
        if (cfg->bitflip_seed_set) {
            fprintf(out, " seed=%u", cfg->bitflip_seed);
        }
        fputc('\n', out);
    }
}

int app_config_validate(const struct app_runtime_config *cfg,
                        char *err, size_t err_len)
{
    if (cfg == NULL) {
        set_error(err, err_len, "configuracao ausente");
        return -1;
    }

    if (cfg->game_mode != APP_GAME_MODE_CLEAR &&
        cfg->game_mode != APP_GAME_MODE_FLASHLIGHT &&
        cfg->game_mode != APP_GAME_MODE_EXPLORE) {
        set_error(err, err_len, "modo de jogo invalido");
        return -1;
    }

    if (cfg->default_role != APP_ROLE_SERVER &&
        cfg->default_role != APP_ROLE_CLIENT) {
        set_error(err, err_len, "papel inicial invalido");
        return -1;
    }

    if (cfg->file_window_size < APP_CONFIG_MIN_FILE_WINDOW_SIZE ||
        cfg->file_window_size > APP_CONFIG_MAX_FILE_WINDOW_SIZE) {
        set_error(err, err_len,
                  "janela de transferencia de arquivo deve estar entre %d e %d",
                  APP_CONFIG_MIN_FILE_WINDOW_SIZE,
                  APP_CONFIG_MAX_FILE_WINDOW_SIZE);
        return -1;
    }

    if (cfg->visualization_size < APP_CONFIG_MIN_VISUALIZATION_SIZE ||
        cfg->visualization_size > APP_CONFIG_MAX_VISUALIZATION_SIZE) {
        set_error(err, err_len,
                  "tamanho da visualizacao deve estar entre %d e %d",
                  APP_CONFIG_MIN_VISUALIZATION_SIZE,
                  APP_CONFIG_MAX_VISUALIZATION_SIZE);
        return -1;
    }

    if (cfg->moves_per_visibility_increase <
            APP_CONFIG_MIN_MOVES_PER_INCREASE ||
        cfg->moves_per_visibility_increase >
            APP_CONFIG_MAX_MOVES_PER_INCREASE) {
        set_error(err, err_len, "jogadas para aumentar visualizacao invalida");
        return -1;
    }

    if (cfg->bitflip_percent_set &&
        (cfg->bitflip_percent < APP_CONFIG_MIN_BITFLIP_PERCENT ||
         cfg->bitflip_percent > APP_CONFIG_MAX_BITFLIP_PERCENT)) {
        set_error(err, err_len, "percentual de bit flip invalido");
        return -1;
    }

    if (cfg->map_path[0] == '\0') {
        set_error(err, err_len, "caminho do mapa ausente");
        return -1;
    }

    return 0;
}

int app_config_set_map_path(struct app_runtime_config *cfg, const char *path,
                            char *err, size_t err_len)
{
    if (cfg == NULL || path == NULL || path[0] == '\0') {
        set_error(err, err_len, "caminho do mapa ausente");
        return -1;
    }
    if (strlen(path) >= sizeof(cfg->map_path)) {
        set_error(err, err_len, "caminho do mapa muito longo");
        return -1;
    }

    (void)snprintf(cfg->map_path, sizeof(cfg->map_path), "%s", path);
    return 0;
}

int app_config_set_interface_name(struct app_runtime_config *cfg,
                                  const char *name,
                                  char *err, size_t err_len)
{
    if (cfg == NULL || name == NULL) {
        set_error(err, err_len, "nome da interface ausente");
        return -1;
    }
    if (strlen(name) >= sizeof(cfg->interface_name)) {
        set_error(err, err_len, "nome da interface muito longo");
        return -1;
    }

    (void)snprintf(cfg->interface_name, sizeof(cfg->interface_name), "%s",
                   name);
    return 0;
}

const char *app_game_mode_name(enum app_game_mode mode)
{
    switch (mode) {
    case APP_GAME_MODE_CLEAR:
        return "claro";
    case APP_GAME_MODE_FLASHLIGHT:
        return "lanterna";
    case APP_GAME_MODE_EXPLORE:
        return "expo";
    default:
        return "desconhecido";
    }
}

const char *app_role_name(enum app_role role)
{
    switch (role) {
    case APP_ROLE_SERVER:
        return "servidor";
    case APP_ROLE_CLIENT:
        return "cliente";
    case APP_ROLE_UNSET:
    default:
        return "indefinido";
    }
}

int app_game_mode_parse(const char *text, enum app_game_mode *out)
{
    if (text == NULL || out == NULL) {
        return -1;
    }

    if (strcmp(text, "claro") == 0) {
        *out = APP_GAME_MODE_CLEAR;
        return 0;
    }
    if (strcmp(text, "lanterna") == 0) {
        *out = APP_GAME_MODE_FLASHLIGHT;
        return 0;
    }
    if (strcmp(text, "expo") == 0) {
        *out = APP_GAME_MODE_EXPLORE;
        return 0;
    }

    return -1;
}

int app_role_parse(const char *text, enum app_role *out)
{
    if (text == NULL || out == NULL) {
        return -1;
    }

    if (strcmp(text, "servidor") == 0) {
        *out = APP_ROLE_SERVER;
        return 0;
    }
    if (strcmp(text, "cliente") == 0) {
        *out = APP_ROLE_CLIENT;
        return 0;
    }

    return -1;
}
