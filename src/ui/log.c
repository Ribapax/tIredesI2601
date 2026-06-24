#define _POSIX_C_SOURCE 200809L

#include "ui/log.h"

#include <stdarg.h>
#include <time.h>
#include <unistd.h>

static const char *ui_log_color(enum ui_log_kind kind)
{
    switch (kind) {
    case UI_LOG_TX:
        return "\033[36m";
    case UI_LOG_RX:
        return "\033[34m";
    case UI_LOG_ACK:
        return "\033[32m";
    case UI_LOG_NACK:
        return "\033[31m";
    case UI_LOG_TIMEOUT:
        return "\033[33m";
    case UI_LOG_VIEW:
        return "\033[35m";
    case UI_LOG_FILE:
        return "\033[95m";
    case UI_LOG_GAME:
        return "\033[1m";
    case UI_LOG_WARN:
        return "\033[93m";
    case UI_LOG_ERROR:
        return "\033[1;31m";
    default:
        return "";
    }
}

void ui_log(FILE *stream, enum ui_log_kind kind, const char *fmt, ...)
{
    FILE *out = stream == NULL ? stdout : stream;
    int color = isatty(fileno(out));
    struct timespec now;
    struct tm local;
    char timestamp[32];
    va_list args;

    if (fmt == NULL) {
        return;
    }

    if (color) {
        fputs(ui_log_color(kind), out);
    }

    if (clock_gettime(CLOCK_REALTIME, &now) == 0 &&
        localtime_r(&now.tv_sec, &local) != NULL &&
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S",
                 &local) > 0U) {
        fprintf(out, "[%s.%03ld] ", timestamp, now.tv_nsec / 1000000L);
    }

    va_start(args, fmt);
    (void)vfprintf(out, fmt, args);
    va_end(args);

    if (color) {
        fputs("\033[0m", out);
    }
}
