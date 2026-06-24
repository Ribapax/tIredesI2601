#define _POSIX_C_SOURCE 200809L

#include "ui/game_view.h"

#include "game/map.h"
#include "game/visibility.h"
#include "net/frame.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#define UI_GAME_VIEW_MASK_BLOCK "\342\226\210"
#define UI_GAME_VIEW_ESCAPE '\033'
#define UI_GAME_VIEW_ESCAPE_READ_TIMEOUT_DS 1
#define UI_GAME_VIEW_COLOR_RESET "\033[0m"
#define UI_GAME_VIEW_COLOR_WALL "\033[1;34m"
#define UI_GAME_VIEW_COLOR_PACMAN "\033[1;33m"
#define UI_GAME_VIEW_COLOR_FILE "\033[1;35m"
#define UI_GAME_VIEW_COLOR_MASK "\033[2;37m"
#define UI_GAME_VIEW_COLOR_GHOST_RED "\033[1;31m"
#define UI_GAME_VIEW_COLOR_GHOST_BLUE "\033[1;36m"
#define UI_GAME_VIEW_COLOR_GHOST_GREEN "\033[1;32m"
#define UI_GAME_VIEW_COLOR_GHOST_YELLOW "\033[1;93m"

static FILE *ui_game_view_stream;

void ui_game_view_set_stream(FILE *stream)
{
    ui_game_view_stream = stream;
}

int ui_game_view_read_key(char *out, size_t out_len)
{
    struct termios original;
    struct termios raw;
    char key;
    char first_key;
    size_t out_pos = 0U;

    if (out == NULL || out_len < 2U) {
        return -1;
    }

    if (!isatty(STDIN_FILENO)) {
        if (fgets(out, out_len, stdin) == NULL) {
            return 0;
        }
        return 1;
    }

    if (tcgetattr(STDIN_FILENO, &original) != 0) {
        return -1;
    }

    raw = original;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        return -1;
    }

    for (;;) {
        ssize_t got = read(STDIN_FILENO, &key, 1U);

        if (got < 0 && errno == EINTR) {
            continue;
        }

        if (got < 0) {
            (void)tcsetattr(STDIN_FILENO, TCSANOW, &original);
            return -1;
        }
        if (got == 0) {
            (void)tcsetattr(STDIN_FILENO, TCSANOW, &original);
            return 0;
        }

        first_key = key;
        out[out_pos++] = key;
        if (key == UI_GAME_VIEW_ESCAPE && out_len > 2U) {
            struct termios timed_raw = raw;

            timed_raw.c_cc[VMIN] = 0;
            timed_raw.c_cc[VTIME] = UI_GAME_VIEW_ESCAPE_READ_TIMEOUT_DS;
            if (tcsetattr(STDIN_FILENO, TCSANOW, &timed_raw) != 0) {
                (void)tcsetattr(STDIN_FILENO, TCSANOW, &original);
                return -1;
            }
            while (out_pos + 1U < out_len) {
                got = read(STDIN_FILENO, &key, 1U);
                if (got < 0 && errno == EINTR) {
                    continue;
                }
                if (got < 0) {
                    (void)tcsetattr(STDIN_FILENO, TCSANOW, &original);
                    return -1;
                }
                if (got == 0) {
                    break;
                }
                out[out_pos++] = key;
            }
        }

        (void)tcsetattr(STDIN_FILENO, TCSANOW, &original);
        out[out_pos] = '\0';
        if (isprint((unsigned char)first_key)) {
            printf("%c\n", first_key);
        } else {
            putchar('\n');
        }
        fflush(stdout);
        return 1;
    }
}

static const char *ui_game_view_cell_color(uint8_t cell)
{
    switch (cell) {
    case (uint8_t)'X':
        return UI_GAME_VIEW_COLOR_WALL;
    case (uint8_t)'P':
        return UI_GAME_VIEW_COLOR_PACMAN;
    case (uint8_t)GAME_GHOST_RED:
        return UI_GAME_VIEW_COLOR_GHOST_RED;
    case (uint8_t)GAME_GHOST_BLUE:
        return UI_GAME_VIEW_COLOR_GHOST_BLUE;
    case (uint8_t)GAME_GHOST_GREEN:
        return UI_GAME_VIEW_COLOR_GHOST_GREEN;
    case (uint8_t)GAME_GHOST_YELLOW:
        return UI_GAME_VIEW_COLOR_GHOST_YELLOW;
    case (uint8_t)GAME_VISIBILITY_MASKED_CELL:
        return UI_GAME_VIEW_COLOR_MASK;
    default:
        if (cell >= (uint8_t)'1' && cell <= (uint8_t)'6') {
            return UI_GAME_VIEW_COLOR_FILE;
        }
        return NULL;
    }
}

static void ui_game_view_render_cell(FILE *stream, uint8_t cell, int use_color)
{
    const char *color = use_color ? ui_game_view_cell_color(cell) : NULL;

    if (color != NULL) {
        fputs(color, stream);
    }
    if (cell == (uint8_t)GAME_VISIBILITY_MASKED_CELL) {
        fputs(UI_GAME_VIEW_MASK_BLOCK, stream);
        fputs(UI_GAME_VIEW_MASK_BLOCK, stream);
        if (color != NULL) {
            fputs(UI_GAME_VIEW_COLOR_RESET, stream);
        }
        return;
    }

    if (cell == (uint8_t)'0') {
        fputc(' ', stream);
    } else {
        fputc((int)cell, stream);
    }
    fputc(' ', stream);
    if (color != NULL) {
        fputs(UI_GAME_VIEW_COLOR_RESET, stream);
    }
}

static void ui_game_view_write_frame(FILE *stream, const uint8_t *data,
                                     size_t side, const char *status,
                                     int terminal_output)
{
    int has_status = status != NULL && status[0] != '\0';

    if (terminal_output) {
        fputs("\033[?25l", stream);
    }
    fputs("\033[H", stream);
    for (size_t row = 0U; row < side; row++) {
        for (size_t col = 0U; col < side; col++) {
            ui_game_view_render_cell(stream, data[(row * side) + col],
                                     terminal_output);
        }
        if (has_status && row == 0U) {
            fprintf(stream, "  %s", status);
        }
        fputs("\033[K\n", stream);
    }
    fputs("\033[K\n\033[J", stream);
    if (terminal_output) {
        fputs("\033[?25h", stream);
    }
}

void ui_game_view_render_with_status(const uint8_t *data, size_t side,
                                     const char *status)
{
    FILE *stream = ui_game_view_stream == NULL ? stdout : ui_game_view_stream;
    int terminal_output = isatty(fileno(stream));
    char *frame = NULL;
    size_t frame_len = 0U;
    FILE *buffer = open_memstream(&frame, &frame_len);

    if (buffer == NULL) {
        ui_game_view_write_frame(stream, data, side, status, terminal_output);
        fflush(stream);
        return;
    }

    ui_game_view_write_frame(buffer, data, side, status, terminal_output);
    if (fclose(buffer) == 0 && frame != NULL && frame_len > 0U) {
        (void)fwrite(frame, 1U, frame_len, stream);
    }
    free(frame);
    fflush(stream);
}

void ui_game_view_render(const uint8_t *data, size_t side)
{
    ui_game_view_render_with_status(data, side, NULL);
}

int ui_game_view_frame_type_from_input(const char *text, uint8_t *out)
{
    char command;

    if (text == NULL || out == NULL) {
        return -1;
    }

    while (isspace((unsigned char)*text)) {
        text++;
    }
    if (text[0] == UI_GAME_VIEW_ESCAPE &&
        (text[1] == '[' || text[1] == 'O')) {
        switch (text[2]) {
        case 'C':
            *out = FRAME_TYPE_MOVE_RIGHT;
            return 0;
        case 'D':
            *out = FRAME_TYPE_MOVE_LEFT;
            return 0;
        case 'A':
            *out = FRAME_TYPE_MOVE_UP;
            return 0;
        case 'B':
            *out = FRAME_TYPE_MOVE_DOWN;
            return 0;
        default:
            return -1;
        }
    }

    command = (char)tolower((unsigned char)*text);

    switch (command) {
    case 'd':
        *out = FRAME_TYPE_MOVE_RIGHT;
        return 0;
    case 'a':
        *out = FRAME_TYPE_MOVE_LEFT;
        return 0;
    case 'w':
        *out = FRAME_TYPE_MOVE_UP;
        return 0;
    case 's':
        *out = FRAME_TYPE_MOVE_DOWN;
        return 0;
    case 'q':
        *out = FRAME_TYPE_END;
        return 0;
    default:
        return -1;
    }
}
