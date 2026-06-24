#define _GNU_SOURCE

#include "net/diag.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/if_ether.h>
#include <linux/if_packet.h>

static void usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "Uso: %s --iface <interface>\n"
            "\n"
            "Abre um raw socket AF_PACKET/SOCK_RAW, faz bind na interface e\n"
            "imprime ifindex e MAC local. Requer root ou CAP_NET_RAW.\n",
            program);
}

int main(int argc, char **argv)
{
    const char *iface = NULL;
    unsigned int ifindex = 0;
    unsigned char mac[NET_MAC_LEN];
    char mac_text[NET_MAC_TEXT_LEN];
    char err[256];
    int fd;
    struct sockaddr_ll addr;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--iface") == 0 && i + 1 < argc) {
            iface = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            usage(stdout, argv[0]);
            return 0;
        } else {
            usage(stderr, argv[0]);
            return 2;
        }
    }

    if (iface == NULL) {
        usage(stderr, argv[0]);
        return 2;
    }

    if (net_get_ifindex(iface, &ifindex, err, sizeof(err)) != 0) {
        fprintf(stderr, "erro: %s\n", err);
        return 1;
    }
    if (ifindex > (unsigned int)INT_MAX) {
        fprintf(stderr, "erro: ifindex fora do intervalo suportado\n");
        return 1;
    }

    if (net_get_iface_mac(iface, mac, err, sizeof(err)) != 0) {
        fprintf(stderr, "erro: %s\n", err);
        return 1;
    }
    net_format_mac(mac, mac_text);

    printf("iface=%s\n", iface);
    printf("ifindex=%u\n", ifindex);
    printf("mac=%s\n", mac_text);

    fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd < 0) {
        fprintf(stderr, "raw_socket=erro: %s\n", strerror(errno));
        if (errno == EPERM || errno == EACCES) {
            fprintf(stderr,
                    "permissao: execute como root ou aplique CAP_NET_RAW ao binario.\n");
        }
        return 1;
    }
    printf("raw_socket=ok\n");

    memset(&addr, 0, sizeof(addr));
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(ETH_P_ALL);
    addr.sll_ifindex = (int)ifindex;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "bind=erro: %s\n", strerror(errno));
        (void)close(fd);
        return 1;
    }

    printf("bind=ok\n");
    (void)close(fd);
    return 0;
}
