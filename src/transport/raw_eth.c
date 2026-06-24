#define _GNU_SOURCE

#include "net/raw_eth.h"

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <sys/socket.h>

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

static long long monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return ((long long)ts.tv_sec * 1000LL) + ((long long)ts.tv_nsec / 1000000LL);
}

void raw_eth_broadcast_mac(unsigned char out[NET_MAC_LEN])
{
    if (out == NULL) {
        return;
    }

    memset(out, 0xff, NET_MAC_LEN);
}

int raw_eth_is_broadcast(const unsigned char mac[NET_MAC_LEN])
{
    if (mac == NULL) {
        return 0;
    }

    for (size_t i = 0; i < NET_MAC_LEN; i++) {
        if (mac[i] != 0xffU) {
            return 0;
        }
    }

    return 1;
}

int raw_eth_open(struct raw_eth_socket *sock, const char *iface,
                 uint16_t ethertype, char *err, size_t err_len)
{
    struct sockaddr_ll addr;

    if (sock == NULL) {
        set_error(err, err_len, "socket de saida ausente");
        return -1;
    }

    memset(sock, 0, sizeof(*sock));
    sock->fd = -1;
    if (net_get_ifindex(iface, &sock->ifindex, err, err_len) != 0) {
        return -1;
    }
    if (net_get_iface_mac(iface, sock->local_mac, err, err_len) != 0) {
        return -1;
    }

    sock->fd = socket(AF_PACKET, SOCK_RAW, htons(ethertype));
    if (sock->fd < 0) {
        set_error(err, err_len, "raw socket falhou: %s", strerror(errno));
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(ethertype);
    addr.sll_ifindex = (int)sock->ifindex;

    if (bind(sock->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        set_error(err, err_len, "bind raw socket falhou: %s", strerror(errno));
        raw_eth_close(sock);
        return -1;
    }

    sock->ethertype = ethertype;
    return 0;
}

void raw_eth_close(struct raw_eth_socket *sock)
{
    if (sock == NULL || sock->fd < 0) {
        return;
    }

    (void)close(sock->fd);
    sock->fd = -1;
}

int raw_eth_send(const struct raw_eth_socket *sock,
                 const unsigned char dst_mac[NET_MAC_LEN],
                 const uint8_t *payload, size_t payload_len,
                 char *err, size_t err_len)
{
    unsigned char frame[ETH_FRAME_LEN];
    struct sockaddr_ll addr;
    size_t frame_len;
    size_t send_len;
    ssize_t sent;

    if (sock == NULL || sock->fd < 0 || dst_mac == NULL ||
        (payload_len > 0U && payload == NULL) ||
        payload_len > RAW_ETH_MAX_PAYLOAD_LEN) {
        set_error(err, err_len, "argumentos invalidos para envio Ethernet");
        return -1;
    }

    frame_len = ETH_HLEN + payload_len;
    send_len = frame_len < ETH_ZLEN ? ETH_ZLEN : frame_len;

    memset(frame, 0, sizeof(frame));
    memcpy(frame, dst_mac, NET_MAC_LEN);
    memcpy(frame + NET_MAC_LEN, sock->local_mac, NET_MAC_LEN);
    frame[12] = (unsigned char)((sock->ethertype >> 8) & 0xffU);
    frame[13] = (unsigned char)(sock->ethertype & 0xffU);
    if (payload_len > 0U) {
        memcpy(frame + ETH_HLEN, payload, payload_len);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sll_family = AF_PACKET;
    addr.sll_ifindex = (int)sock->ifindex;
    addr.sll_halen = NET_MAC_LEN;
    memcpy(addr.sll_addr, dst_mac, NET_MAC_LEN);

    sent = sendto(sock->fd, frame, send_len, 0,
                  (struct sockaddr *)&addr, sizeof(addr));
    if (sent < 0) {
        set_error(err, err_len, "sendto falhou: %s", strerror(errno));
        return -1;
    }
    if ((size_t)sent != send_len) {
        set_error(err, err_len, "sendto parcial: %zd de %zu bytes",
                  sent, send_len);
        return -1;
    }

    return (int)sent;
}

int raw_eth_recv(const struct raw_eth_socket *sock, int timeout_ms,
                 unsigned char src_mac[NET_MAC_LEN],
                 unsigned char dst_mac[NET_MAC_LEN],
                 uint8_t *payload, size_t payload_capacity,
                 size_t *payload_len, char *err, size_t err_len)
{
    long long deadline;

    if (sock == NULL || sock->fd < 0 || src_mac == NULL || dst_mac == NULL ||
        payload == NULL || payload_len == NULL || timeout_ms < 0) {
        set_error(err, err_len, "argumentos invalidos para recepcao Ethernet");
        return -1;
    }

    deadline = monotonic_ms() + (long long)timeout_ms;
    for (;;) {
        struct pollfd pfd;
        long long now = monotonic_ms();
        int remaining;
        int poll_rc;
        unsigned char frame[ETH_FRAME_LEN];
        struct sockaddr_ll from;
        socklen_t from_len = sizeof(from);
        ssize_t got;
        unsigned int ethertype;
        size_t received_payload_len;

        if (now >= deadline) {
            return 0;
        }

        remaining = (int)(deadline - now);
        pfd.fd = sock->fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        poll_rc = poll(&pfd, 1, remaining);
        if (poll_rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            set_error(err, err_len, "poll falhou: %s", strerror(errno));
            return -1;
        }
        if (poll_rc == 0) {
            return 0;
        }

        got = recvfrom(sock->fd, frame, sizeof(frame), MSG_DONTWAIT,
                       (struct sockaddr *)&from, &from_len);
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            set_error(err, err_len, "recvfrom falhou: %s", strerror(errno));
            return -1;
        }

        if (from.sll_pkttype == PACKET_OUTGOING || got < (ssize_t)ETH_HLEN) {
            continue;
        }

        ethertype = ((unsigned int)frame[12] << 8) | (unsigned int)frame[13];
        if (ethertype != sock->ethertype) {
            continue;
        }
        if (memcmp(frame, sock->local_mac, NET_MAC_LEN) != 0 &&
            !raw_eth_is_broadcast(frame)) {
            continue;
        }

        received_payload_len = (size_t)got - ETH_HLEN;
        if (received_payload_len > payload_capacity) {
            set_error(err, err_len, "payload recebido excede buffer");
            return -1;
        }

        memcpy(dst_mac, frame, NET_MAC_LEN);
        memcpy(src_mac, frame + NET_MAC_LEN, NET_MAC_LEN);
        memcpy(payload, frame + ETH_HLEN, received_payload_len);
        *payload_len = received_payload_len;
        return 1;
    }
}
