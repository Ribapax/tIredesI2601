#define _GNU_SOURCE

#include "net/diag.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <net/if.h>

static void set_error(char *err, size_t err_len, const char *fmt, ...)
{
    va_list args;

    if (err == NULL || err_len == 0) {
        return;
    }

    va_start(args, fmt);
    (void)vsnprintf(err, err_len, fmt, args);
    va_end(args);
}

int net_parse_mac(const char *text, unsigned char out[NET_MAC_LEN])
{
    unsigned int part[NET_MAC_LEN];
    char tail;

    if (text == NULL || out == NULL) {
        return -1;
    }

    if (sscanf(text, "%x:%x:%x:%x:%x:%x%c",
               &part[0], &part[1], &part[2],
               &part[3], &part[4], &part[5], &tail) != NET_MAC_LEN) {
        return -1;
    }

    for (size_t i = 0; i < NET_MAC_LEN; i++) {
        if (part[i] > 0xffU) {
            return -1;
        }
        out[i] = (unsigned char)part[i];
    }

    return 0;
}

void net_format_mac(const unsigned char mac[NET_MAC_LEN],
                    char out[NET_MAC_TEXT_LEN])
{
    if (mac == NULL || out == NULL) {
        return;
    }

    (void)snprintf(out, NET_MAC_TEXT_LEN,
                   "%02x:%02x:%02x:%02x:%02x:%02x",
                   (unsigned int)mac[0], (unsigned int)mac[1],
                   (unsigned int)mac[2], (unsigned int)mac[3],
                   (unsigned int)mac[4], (unsigned int)mac[5]);
}

int net_get_ifindex(const char *iface, unsigned int *ifindex,
                    char *err, size_t err_len)
{
    unsigned int value;

    if (iface == NULL || iface[0] == '\0' || ifindex == NULL) {
        set_error(err, err_len, "interface ausente");
        return -1;
    }

    value = if_nametoindex(iface);
    if (value == 0U) {
        set_error(err, err_len, "interface '%s' nao encontrada: %s",
                  iface, strerror(errno));
        return -1;
    }

    *ifindex = value;
    return 0;
}

int net_get_iface_mac(const char *iface, unsigned char mac[NET_MAC_LEN],
                      char *err, size_t err_len)
{
    int fd;
    struct ifreq ifr;

    if (iface == NULL || iface[0] == '\0' || mac == NULL) {
        set_error(err, err_len, "interface ausente");
        return -1;
    }

    if (strlen(iface) >= IFNAMSIZ) {
        set_error(err, err_len, "nome de interface muito longo: '%s'", iface);
        return -1;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        set_error(err, err_len, "socket AF_INET falhou: %s", strerror(errno));
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    (void)snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", iface);

    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
        set_error(err, err_len, "ioctl SIOCGIFHWADDR em '%s' falhou: %s",
                  iface, strerror(errno));
        (void)close(fd);
        return -1;
    }

    memcpy(mac, ifr.ifr_hwaddr.sa_data, NET_MAC_LEN);
    (void)close(fd);
    return 0;
}

int net_parse_int_range(const char *text, int min, int max, int *out)
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

int net_parse_u16_range(const char *text, unsigned int min,
                        unsigned int max, uint16_t *out)
{
    unsigned long value;
    char *end;

    if (text == NULL || out == NULL || min > max || max > UINT16_MAX) {
        return -1;
    }

    errno = 0;
    value = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' ||
        value < (unsigned long)min || value > (unsigned long)max) {
        return -1;
    }

    *out = (uint16_t)value;
    return 0;
}
