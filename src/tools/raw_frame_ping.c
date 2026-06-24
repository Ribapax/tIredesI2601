#define _GNU_SOURCE

#include "net/diag.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <linux/if_ether.h>
#include <linux/if_packet.h>

#define DIAG_ETHERTYPE 0x88B5U
#define DIAG_PAYLOAD_LEN 16U
#define DIAG_KIND_PING 1U
#define DIAG_KIND_PONG 2U

enum diag_mode {
    MODE_UNSET = 0,
    MODE_PING,
    MODE_LISTEN
};

struct config {
    const char *iface;
    unsigned char peer_mac[NET_MAC_LEN];
    bool has_peer_mac;
    enum diag_mode mode;
    int count;
    int timeout_ms;
    uint16_t ethertype;
    bool verbose;
};

struct diag_frame {
    unsigned int kind;
    unsigned int seq;
};

static void usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "Uso: %s (--ping|--listen) --iface <interface> --peer-mac <mac> [opcoes]\n"
            "\n"
            "Opcoes:\n"
            "  --count <n>        quantidade de pings; em --listen, 0 = infinito\n"
            "  --timeout-ms <n>   timeout de recepcao em ms (padrao: 1000)\n"
            "  --ethertype <hex>  EtherType de diagnostico (padrao: 0x88B5)\n"
            "  --verbose          imprime frames ignorados\n"
            "\n"
            "Exemplo listener: %s --listen --iface enp3s0 --peer-mac aa:bb:cc:dd:ee:ff --count 4\n"
            "Exemplo ping:     %s --ping --iface enp3s0 --peer-mac 11:22:33:44:55:66 --count 4\n",
            program, program, program);
}

static int parse_args(int argc, char **argv, struct config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->mode = MODE_UNSET;
    cfg->count = -1;
    cfg->timeout_ms = 1000;
    cfg->ethertype = (uint16_t)DIAG_ETHERTYPE;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--iface") == 0 && i + 1 < argc) {
            cfg->iface = argv[++i];
        } else if (strcmp(argv[i], "--peer-mac") == 0 && i + 1 < argc) {
            if (net_parse_mac(argv[++i], cfg->peer_mac) != 0) {
                fprintf(stderr, "erro: MAC invalido\n");
                return -1;
            }
            cfg->has_peer_mac = true;
        } else if (strcmp(argv[i], "--ping") == 0) {
            cfg->mode = MODE_PING;
        } else if (strcmp(argv[i], "--listen") == 0) {
            cfg->mode = MODE_LISTEN;
        } else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            if (net_parse_int_range(argv[++i], 0, INT_MAX, &cfg->count) != 0) {
                fprintf(stderr, "erro: --count invalido\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--timeout-ms") == 0 && i + 1 < argc) {
            if (net_parse_int_range(argv[++i], 1, 60000, &cfg->timeout_ms) != 0) {
                fprintf(stderr, "erro: --timeout-ms invalido\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--ethertype") == 0 && i + 1 < argc) {
            if (net_parse_u16_range(argv[++i], 0x0600U, 0xffffU,
                                       &cfg->ethertype) != 0) {
                fprintf(stderr, "erro: --ethertype invalido\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--verbose") == 0) {
            cfg->verbose = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout, argv[0]);
            return 1;
        } else {
            usage(stderr, argv[0]);
            return -1;
        }
    }

    if (cfg->mode == MODE_UNSET || cfg->iface == NULL || !cfg->has_peer_mac) {
        usage(stderr, argv[0]);
        return -1;
    }

    if (cfg->count < 0) {
        cfg->count = (cfg->mode == MODE_PING) ? 4 : 0;
    }

    return 0;
}

static int set_socket_timeout(int fd, int timeout_ms)
{
    struct timeval tv;

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (suseconds_t)(timeout_ms % 1000) * 1000;

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt SO_RCVTIMEO");
        return -1;
    }

    return 0;
}

static int open_bound_socket(const struct config *cfg, unsigned int *ifindex_out,
                             unsigned char local_mac[NET_MAC_LEN])
{
    char err[256];
    unsigned int ifindex = 0;
    int fd;
    struct sockaddr_ll addr;

    if (net_get_ifindex(cfg->iface, &ifindex, err, sizeof(err)) != 0) {
        fprintf(stderr, "erro: %s\n", err);
        return -1;
    }
    if (ifindex > (unsigned int)INT_MAX) {
        fprintf(stderr, "erro: ifindex fora do intervalo suportado\n");
        return -1;
    }
    if (net_get_iface_mac(cfg->iface, local_mac, err, sizeof(err)) != 0) {
        fprintf(stderr, "erro: %s\n", err);
        return -1;
    }

    fd = socket(AF_PACKET, SOCK_RAW, htons(cfg->ethertype));
    if (fd < 0) {
        fprintf(stderr, "erro: raw socket falhou: %s\n", strerror(errno));
        if (errno == EPERM || errno == EACCES) {
            fprintf(stderr,
                    "permissao: execute como root ou aplique CAP_NET_RAW ao binario.\n");
        }
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(cfg->ethertype);
    addr.sll_ifindex = (int)ifindex;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "erro: bind falhou: %s\n", strerror(errno));
        (void)close(fd);
        return -1;
    }

    if (set_socket_timeout(fd, cfg->timeout_ms) != 0) {
        (void)close(fd);
        return -1;
    }

    *ifindex_out = ifindex;
    return fd;
}

static int send_diag_frame(int fd, const struct config *cfg,
                           unsigned int ifindex,
                           const unsigned char local_mac[NET_MAC_LEN],
                           unsigned int kind, unsigned int seq)
{
    unsigned char frame[ETH_ZLEN];
    unsigned char *payload = frame + ETH_HLEN;
    struct sockaddr_ll addr;
    ssize_t sent;

    memset(frame, 0, sizeof(frame));
    memcpy(frame, cfg->peer_mac, NET_MAC_LEN);
    memcpy(frame + NET_MAC_LEN, local_mac, NET_MAC_LEN);
    frame[12] = (unsigned char)((cfg->ethertype >> 8) & 0xffU);
    frame[13] = (unsigned char)(cfg->ethertype & 0xffU);

    payload[0] = 'P';
    payload[1] = 'M';
    payload[2] = 'R';
    payload[3] = 'P';
    payload[4] = 1U;
    payload[5] = (unsigned char)kind;
    payload[6] = (unsigned char)((seq >> 8) & 0xffU);
    payload[7] = (unsigned char)(seq & 0xffU);
    payload[8] = 'R';
    payload[9] = 'E';
    payload[10] = 'D';
    payload[11] = 'E';
    payload[12] = 'S';
    payload[13] = 'I';
    payload[14] = 0U;
    payload[15] = 0U;

    memset(&addr, 0, sizeof(addr));
    addr.sll_family = AF_PACKET;
    addr.sll_ifindex = (int)ifindex;
    addr.sll_halen = NET_MAC_LEN;
    memcpy(addr.sll_addr, cfg->peer_mac, NET_MAC_LEN);

    sent = sendto(fd, frame, sizeof(frame), 0,
                  (struct sockaddr *)&addr, sizeof(addr));
    if (sent < 0) {
        fprintf(stderr, "erro: sendto falhou: %s\n", strerror(errno));
        return -1;
    }
    if ((size_t)sent != sizeof(frame)) {
        fprintf(stderr, "erro: sendto parcial: %zd de %zu bytes\n",
                sent, sizeof(frame));
        return -1;
    }

    return 0;
}

static bool is_broadcast_mac(const unsigned char mac[NET_MAC_LEN])
{
    for (size_t i = 0; i < NET_MAC_LEN; i++) {
        if (mac[i] != 0xffU) {
            return false;
        }
    }
    return true;
}

static int recv_diag_frame(int fd, const struct config *cfg,
                           const unsigned char local_mac[NET_MAC_LEN],
                           struct diag_frame *diag)
{
    unsigned char buf[ETH_FRAME_LEN];
    struct sockaddr_ll from;
    socklen_t from_len;

    for (;;) {
        ssize_t got;
        unsigned int ethertype;
        const unsigned char *payload;

        from_len = sizeof(from);
        got = recvfrom(fd, buf, sizeof(buf), 0,
                       (struct sockaddr *)&from, &from_len);
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "erro: recvfrom falhou: %s\n", strerror(errno));
            return -1;
        }

        if (from.sll_pkttype == PACKET_OUTGOING) {
            continue;
        }
        if (got < (ssize_t)(ETH_HLEN + DIAG_PAYLOAD_LEN)) {
            if (cfg->verbose) {
                fprintf(stderr, "ignorado: frame curto (%zd bytes)\n", got);
            }
            continue;
        }

        ethertype = ((unsigned int)buf[12] << 8) | (unsigned int)buf[13];
        if (ethertype != cfg->ethertype) {
            continue;
        }
        if (memcmp(buf + NET_MAC_LEN, cfg->peer_mac, NET_MAC_LEN) != 0) {
            if (cfg->verbose) {
                fprintf(stderr, "ignorado: MAC origem diferente do par\n");
            }
            continue;
        }
        if (memcmp(buf, local_mac, NET_MAC_LEN) != 0 && !is_broadcast_mac(buf)) {
            if (cfg->verbose) {
                fprintf(stderr, "ignorado: MAC destino nao e local\n");
            }
            continue;
        }

        payload = buf + ETH_HLEN;
        if (memcmp(payload, "PMRP", 4) != 0 || payload[4] != 1U) {
            if (cfg->verbose) {
                fprintf(stderr, "ignorado: payload diagnostico invalido\n");
            }
            continue;
        }

        diag->kind = payload[5];
        diag->seq = ((unsigned int)payload[6] << 8) | (unsigned int)payload[7];
        return 1;
    }
}

static int run_ping(int fd, const struct config *cfg, unsigned int ifindex,
                    const unsigned char local_mac[NET_MAC_LEN])
{
    int ok = 0;

    for (int i = 0; i < cfg->count; i++) {
        unsigned int seq = (unsigned int)i & 0xffffU;
        struct diag_frame diag;

        if (send_diag_frame(fd, cfg, ifindex, local_mac, DIAG_KIND_PING, seq) != 0) {
            return 1;
        }
        printf("tx ping seq=%u\n", seq);

        for (;;) {
            int rc = recv_diag_frame(fd, cfg, local_mac, &diag);
            if (rc < 0) {
                return 1;
            }
            if (rc == 0) {
                printf("timeout seq=%u\n", seq);
                break;
            }
            if (diag.kind == DIAG_KIND_PONG && diag.seq == seq) {
                printf("rx pong seq=%u\n", seq);
                ok++;
                break;
            }
            if (cfg->verbose) {
                fprintf(stderr, "ignorado: kind=%u seq=%u\n", diag.kind, diag.seq);
            }
        }
    }

    printf("resultado=%d/%d pong recebidos\n", ok, cfg->count);
    return ok == cfg->count ? 0 : 1;
}

static int run_listen(int fd, const struct config *cfg, unsigned int ifindex,
                      const unsigned char local_mac[NET_MAC_LEN])
{
    int handled = 0;

    printf("aguardando pings Ethernet; count=%d (0=infinito)\n", cfg->count);

    while (cfg->count == 0 || handled < cfg->count) {
        struct diag_frame diag;
        int rc = recv_diag_frame(fd, cfg, local_mac, &diag);

        if (rc < 0) {
            return 1;
        }
        if (rc == 0) {
            continue;
        }
        if (diag.kind != DIAG_KIND_PING) {
            if (cfg->verbose) {
                fprintf(stderr, "ignorado: kind=%u seq=%u\n", diag.kind, diag.seq);
            }
            continue;
        }

        printf("rx ping seq=%u\n", diag.seq);
        if (send_diag_frame(fd, cfg, ifindex, local_mac,
                            DIAG_KIND_PONG, diag.seq) != 0) {
            return 1;
        }
        printf("tx pong seq=%u\n", diag.seq);
        handled++;
    }

    printf("resultado=%d pings respondidos\n", handled);
    return 0;
}

int main(int argc, char **argv)
{
    struct config cfg;
    unsigned int ifindex = 0;
    unsigned char local_mac[NET_MAC_LEN];
    char local_mac_text[NET_MAC_TEXT_LEN];
    char peer_mac_text[NET_MAC_TEXT_LEN];
    int fd;
    int parse_result;
    int result;

    parse_result = parse_args(argc, argv, &cfg);
    if (parse_result > 0) {
        return 0;
    }
    if (parse_result < 0) {
        return 2;
    }

    fd = open_bound_socket(&cfg, &ifindex, local_mac);
    if (fd < 0) {
        return 1;
    }

    net_format_mac(local_mac, local_mac_text);
    net_format_mac(cfg.peer_mac, peer_mac_text);
    printf("iface=%s ifindex=%u local=%s peer=%s ethertype=0x%04x\n",
           cfg.iface, ifindex, local_mac_text, peer_mac_text,
           (unsigned int)cfg.ethertype);

    if (cfg.mode == MODE_PING) {
        result = run_ping(fd, &cfg, ifindex, local_mac);
    } else {
        result = run_listen(fd, &cfg, ifindex, local_mac);
    }

    (void)close(fd);
    return result;
}
