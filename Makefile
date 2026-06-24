SHELL := /bin/sh

CC ?= gcc
BUILD_DIR := build/dev
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin

CPPFLAGS += -Iinclude -MMD -MP
CFLAGS ?= -std=c11 -O2 -g
CFLAGS += -Wall -Wextra -Werror -pedantic
LDFLAGS ?=
LDLIBS ?=

COMMON_SRCS := \
	src/domain/map.c \
	src/domain/movement.c \
	src/domain/visibility.c \
	src/protocol/fragment.c \
	src/protocol/frame.c \
	src/protocol/frame_codec.c \
	src/reliable/window.c \
	src/transport/net_diag.c \
	src/transport/net_fault_injection.c \
	src/transport/net_handshake.c \
	src/transport/raw_eth.c \
	src/app/config.c \
	src/app/game_session.c \
	src/app/game_session_io.c \
	src/app/game_session_view_codec.c \
	src/file/file_transfer.c \
	src/ui/game_view.c \
	src/ui/log.c

PACMAN_SRCS := src/main.c $(COMMON_SRCS)
RAW_CAPABILITY_SRCS := src/tools/raw_socket_capability.c $(COMMON_SRCS)
RAW_FRAME_PING_SRCS := src/tools/raw_frame_ping.c $(COMMON_SRCS)

PACMAN_OBJS := $(PACMAN_SRCS:%.c=$(OBJ_DIR)/%.o)
RAW_CAPABILITY_OBJS := $(RAW_CAPABILITY_SRCS:%.c=$(OBJ_DIR)/%.o)
RAW_FRAME_PING_OBJS := $(RAW_FRAME_PING_SRCS:%.c=$(OBJ_DIR)/%.o)

PACMAN := $(BIN_DIR)/pacman
RAW_CAPABILITY := $(BIN_DIR)/raw-socket-capability
RAW_FRAME_PING := $(BIN_DIR)/raw-frame-ping

BINS := $(PACMAN) $(RAW_CAPABILITY) $(RAW_FRAME_PING)
DEPS := $(sort \
	$(PACMAN_OBJS:.o=.d) \
	$(RAW_CAPABILITY_OBJS:.o=.d) \
	$(RAW_FRAME_PING_OBJS:.o=.d))

.PHONY: all pacman raw-socket-capability raw-frame-ping

all: $(BINS)

pacman: $(PACMAN)

raw-socket-capability: $(RAW_CAPABILITY)

raw-frame-ping: $(RAW_FRAME_PING)

$(PACMAN): $(PACMAN_OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(RAW_CAPABILITY): $(RAW_CAPABILITY_OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(RAW_FRAME_PING): $(RAW_FRAME_PING_OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(OBJ_DIR)/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BIN_DIR):
	@mkdir -p "$@"

-include $(DEPS)
