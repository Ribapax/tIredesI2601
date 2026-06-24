#ifndef UI_LOG_H
#define UI_LOG_H

#include <stdio.h>

enum ui_log_kind {
    UI_LOG_TX = 0,
    UI_LOG_RX,
    UI_LOG_ACK,
    UI_LOG_NACK,
    UI_LOG_TIMEOUT,
    UI_LOG_VIEW,
    UI_LOG_FILE,
    UI_LOG_GAME,
    UI_LOG_WARN,
    UI_LOG_ERROR
};

/*
 * Escreve uma linha de log com timestamp e cor quando o destino for terminal.
 *
 * Entrada:
 * - stream: destino do log. Se for NULL, usa stdout.
 * - kind: categoria usada para escolher a cor.
 * - fmt: formato printf da mensagem.
 *
 * Saida:
 * - stream: recebe a mensagem formatada com prefixo de tempo local
 *   `[YYYY-MM-DD HH:MM:SS.mmm]` e ANSI color somente em TTY.
 *
 * Retorno:
 * - nenhum.
 */
void ui_log(FILE *stream, enum ui_log_kind kind, const char *fmt, ...);

#endif
