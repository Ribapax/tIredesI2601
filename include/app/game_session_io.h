#ifndef APP_GAME_SESSION_IO_H
#define APP_GAME_SESSION_IO_H

#include "net/frame.h"
#include "net/raw_eth.h"

#include <stddef.h>
#include <stdint.h>

int game_session_io_build_empty_frame(struct frame *frame, uint8_t type,
                                      uint8_t sequence);

int game_session_io_log_error_frame(const struct frame *frame,
                                    const char *context);

int game_session_io_format_error_frame(const struct frame *frame,
                                       char *out, size_t out_len);

int game_session_io_send_frame_to_peer(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    const struct frame *frame,
    const char *label);

int game_session_io_send_control_frame(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    uint8_t type, uint8_t sequence,
    const char *label);

int game_session_io_send_error_frame(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    uint8_t sequence, uint8_t code,
    uint8_t related_type, uint8_t file_id,
    const char *label);

int game_session_io_receive_peer_frame(
    const struct raw_eth_socket *sock,
    const unsigned char peer_mac[NET_MAC_LEN],
    int timeout_ms, struct frame *out);

#endif
