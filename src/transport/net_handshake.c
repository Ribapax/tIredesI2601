#define _POSIX_C_SOURCE 200809L

#include "net/handshake.h"

#include "net/diag.h"
#include "net/fault_injection.h"
#include "net/frame_codec.h"
#include "net/raw_eth.h"
#include "ui/log.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static long long monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return ((long long)ts.tv_sec * 1000LL) + ((long long)ts.tv_nsec / 1000000LL);
}

int handshake_build_ack_frame(struct frame *frame, uint8_t sequence)
{
    if (frame == NULL) {
        return -1;
    }

    frame_init(frame, FRAME_TYPE_ACK, sequence);
    return frame_set_data(frame, NULL, 0U);
}

bool handshake_is_ack_for(const struct frame *frame, uint8_t sequence)
{
    if (!frame_has_valid_shape(frame)) {
        return false;
    }

    return frame->type == FRAME_TYPE_ACK &&
           frame->sequence == sequence &&
           frame->length == 0U;
}

void handshake_server_session_close(struct handshake_server_session *session)
{
    if (session == NULL) {
        return;
    }

    raw_eth_close(&session->sock);
}

void handshake_client_session_close(struct handshake_client_session *session)
{
    if (session == NULL) {
        return;
    }

    raw_eth_close(&session->sock);
}

static int encode_frame(const struct frame *frame, uint8_t *out,
                        size_t out_len, size_t *wire_len)
{
    size_t encoded = frame_encode(frame, out, out_len);

    if (encoded == 0U) {
        return -1;
    }

    *wire_len = encoded;
    return 0;
}

static int wait_for_ack(const struct raw_eth_socket *sock, uint8_t sequence,
                        unsigned char peer_mac[NET_MAC_LEN])
{
    long long deadline = monotonic_ms() + HANDSHAKE_TIMEOUT_MS;

    for (;;) {
        long long now = monotonic_ms();
        int remaining;
        unsigned char src_mac[NET_MAC_LEN];
        unsigned char dst_mac[NET_MAC_LEN];
        uint8_t payload[RAW_ETH_MAX_PAYLOAD_LEN];
        size_t payload_len = 0U;
        char err[256];
        int rc;
        struct frame received;
        char src_text[NET_MAC_TEXT_LEN];

        if (now >= deadline) {
            return 0;
        }

        remaining = (int)(deadline - now);
        rc = raw_eth_recv(sock, remaining, src_mac, dst_mac, payload,
                          sizeof(payload), &payload_len, err, sizeof(err));
        if (rc < 0) {
            ui_log(stderr, UI_LOG_ERROR, "erro: %s\n", err);
            return -1;
        }
        if (rc == 0) {
            return 0;
        }

        net_format_mac(src_mac, src_text);
        if (!frame_decode(&received, payload, payload_len)) {
            ui_log(stdout, UI_LOG_NACK,
                   "rx ignorado src=%s motivo=frame-invalido\n", src_text);
            continue;
        }
        if (!handshake_is_ack_for(&received, sequence)) {
            ui_log(stdout, UI_LOG_WARN,
                   "rx ignorado src=%s tipo=%s seq=%u\n",
                   src_text, frame_type_name(received.type),
                   (unsigned int)received.sequence);
            continue;
        }

        memcpy(peer_mac, src_mac, NET_MAC_LEN);
        ui_log(stdout, UI_LOG_ACK, "rx ack src=%s seq=%u\n", src_text,
               (unsigned int)sequence);
        return 1;
    }
}

int handshake_establish_server(const char *iface, uint16_t ethertype,
                               const struct frame *init_frame,
                               struct handshake_server_session *session)
{
    unsigned char broadcast[NET_MAC_LEN];
    char err[256];
    char local_text[NET_MAC_TEXT_LEN];
    char dst_text[NET_MAC_TEXT_LEN];
    uint8_t payload[FRAME_MAX_WIRE_LEN];
    size_t payload_len = 0U;

    if (session == NULL) {
        ui_log(stderr, UI_LOG_ERROR, "erro: sessao de handshake ausente\n");
        return 1;
    }

    memset(session, 0, sizeof(*session));
    session->sock.fd = -1;

    if (!frame_has_valid_shape(init_frame) ||
        init_frame->type != FRAME_TYPE_INIT ||
        init_frame->length != 0U) {
        ui_log(stderr, UI_LOG_ERROR,
               "erro: frame INIT de handshake deve ser vazio\n");
        return 1;
    }

    if (raw_eth_open(&session->sock, iface, ethertype, err, sizeof(err)) != 0) {
        ui_log(stderr, UI_LOG_ERROR, "erro: %s\n", err);
        if (errno == EPERM || errno == EACCES) {
            ui_log(stderr, UI_LOG_WARN,
                   "permissao: execute como root ou use CAP_NET_RAW.\n");
        }
        return 1;
    }

    raw_eth_broadcast_mac(broadcast);
    net_format_mac(session->sock.local_mac, local_text);
    net_format_mac(broadcast, dst_text);
    ui_log(stdout, UI_LOG_GAME,
           "modo=servidor iface=%s local=%s ethertype=0x%04x\n",
           iface, local_text, (unsigned int)ethertype);

    if (encode_frame(init_frame, payload, sizeof(payload), &payload_len) != 0) {
        ui_log(stderr, UI_LOG_ERROR, "erro: falha ao montar frame INIT\n");
        handshake_server_session_close(session);
        return 1;
    }

    for (int attempt = 1; attempt <= HANDSHAKE_MAX_RETRIES; attempt++) {
        uint8_t tx_payload[FRAME_MAX_WIRE_LEN];
        int sent;
        int ack;

        memcpy(tx_payload, payload, payload_len);
        (void)net_fault_maybe_flip_bit(tx_payload, payload_len,
                                       "handshake-init");

        sent = raw_eth_send(&session->sock, broadcast, tx_payload, payload_len,
                            err, sizeof(err));
        if (sent < 0) {
            ui_log(stderr, UI_LOG_ERROR, "erro: %s\n", err);
            handshake_server_session_close(session);
            return 1;
        }

        ui_log(stdout, UI_LOG_TX,
               "tx init tentativa=%d/%d dst=%s payload=%zu ethernet=%d timeout_ms=%d\n",
               attempt, HANDSHAKE_MAX_RETRIES, dst_text, payload_len, sent,
               HANDSHAKE_TIMEOUT_MS);

        ack = wait_for_ack(&session->sock, init_frame->sequence,
                           session->peer_mac);
        if (ack < 0) {
            handshake_server_session_close(session);
            return 1;
        }
        if (ack == 1) {
            char peer_text[NET_MAC_TEXT_LEN];

            net_format_mac(session->peer_mac, peer_text);
            ui_log(stdout, UI_LOG_ACK,
                   "handshake confirmado peer=%s init=ack\n",
                   peer_text);
            ui_log(stdout, UI_LOG_GAME,
                   "proximos-envios=unicast dst=%s sem-ack-do-ack\n",
                   peer_text);
            return 0;
        }

        ui_log(stdout, UI_LOG_TIMEOUT,
               "timeout tentativa=%d sem ACK em %d ms\n",
               attempt, HANDSHAKE_TIMEOUT_MS);
    }

    ui_log(stderr, UI_LOG_ERROR,
           "erro: handshake falhou apos %d tentativas\n",
           HANDSHAKE_MAX_RETRIES);
    handshake_server_session_close(session);
    return 1;
}

int handshake_establish_client(const char *iface, uint16_t ethertype,
                               struct handshake_client_session *session)
{
    char err[256];
    char local_text[NET_MAC_TEXT_LEN];

    if (session == NULL) {
        ui_log(stderr, UI_LOG_ERROR, "erro: sessao de handshake ausente\n");
        return 1;
    }

    memset(session, 0, sizeof(*session));
    session->sock.fd = -1;

    if (raw_eth_open(&session->sock, iface, ethertype, err, sizeof(err)) != 0) {
        ui_log(stderr, UI_LOG_ERROR, "erro: %s\n", err);
        if (errno == EPERM || errno == EACCES) {
            ui_log(stderr, UI_LOG_WARN,
                   "permissao: execute como root ou use CAP_NET_RAW.\n");
        }
        return 1;
    }

    net_format_mac(session->sock.local_mac, local_text);
    ui_log(stdout, UI_LOG_GAME,
           "modo=cliente iface=%s local=%s ethertype=0x%04x\n",
           iface, local_text, (unsigned int)ethertype);
    ui_log(stdout, UI_LOG_GAME,
           "aguardando INIT tipo=3 via broadcast/unicast\n");

    for (;;) {
        unsigned char src_mac[NET_MAC_LEN];
        unsigned char dst_mac[NET_MAC_LEN];
        uint8_t payload[RAW_ETH_MAX_PAYLOAD_LEN];
        size_t payload_len = 0U;
        struct frame received;
        int rc;
        char src_text[NET_MAC_TEXT_LEN];

        rc = raw_eth_recv(&session->sock, 1000, src_mac, dst_mac, payload,
                          sizeof(payload), &payload_len, err, sizeof(err));
        if (rc < 0) {
            ui_log(stderr, UI_LOG_ERROR, "erro: %s\n", err);
            handshake_client_session_close(session);
            return 1;
        }
        if (rc == 0) {
            continue;
        }

        net_format_mac(src_mac, src_text);
        if (!frame_decode(&received, payload, payload_len)) {
            ui_log(stdout, UI_LOG_NACK,
                   "rx ignorado src=%s motivo=frame-invalido\n", src_text);
            continue;
        }
        if (received.type != FRAME_TYPE_INIT || received.length != 0U) {
            ui_log(stdout, UI_LOG_WARN,
                   "rx ignorado src=%s tipo=%s seq=%u\n",
                   src_text, frame_type_name(received.type),
                   (unsigned int)received.sequence);
            continue;
        }

        ui_log(stdout, UI_LOG_RX,
               "rx init src=%s seq=%u payload=%u bytes\n",
               src_text, (unsigned int)received.sequence,
               (unsigned int)received.length);

        {
            struct frame ack;
            uint8_t ack_payload[FRAME_MAX_WIRE_LEN];
            size_t ack_payload_len = 0U;
            int sent;

            if (handshake_build_ack_frame(&ack, received.sequence) != 0 ||
                encode_frame(&ack, ack_payload, sizeof(ack_payload),
                             &ack_payload_len) != 0) {
                ui_log(stderr, UI_LOG_ERROR, "erro: falha ao montar ACK\n");
                handshake_client_session_close(session);
                return 1;
            }

            (void)net_fault_maybe_flip_bit(ack_payload, ack_payload_len,
                                           "handshake-ack");

            sent = raw_eth_send(&session->sock, src_mac, ack_payload,
                                ack_payload_len, err, sizeof(err));
            if (sent < 0) {
                ui_log(stderr, UI_LOG_ERROR, "erro: %s\n", err);
                handshake_client_session_close(session);
                return 1;
            }

            ui_log(stdout, UI_LOG_TX,
                   "tx ack dst=%s seq=%u payload=%zu ethernet=%d\n",
                   src_text, (unsigned int)ack.sequence,
                   ack_payload_len, sent);
        }

        memcpy(session->peer_mac, src_mac, NET_MAC_LEN);
        session->init_frame = received;
        return 0;
    }
}

int handshake_run_server(const char *iface, uint16_t ethertype,
                         const struct frame *init_frame)
{
    struct handshake_server_session session;
    int rc = handshake_establish_server(iface, ethertype, init_frame, &session);

    if (rc == 0) {
        handshake_server_session_close(&session);
    }
    return rc;
}

int handshake_run_client(const char *iface, uint16_t ethertype)
{
    struct handshake_client_session session;
    int rc = handshake_establish_client(iface, ethertype, &session);

    if (rc == 0) {
        handshake_client_session_close(&session);
    }
    return rc;
}
