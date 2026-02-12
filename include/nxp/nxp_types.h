/*
 * NEXUS Protocol (NXP) - Core Type Definitions
 * Copyright (c) 2026 NXP Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef NXP_TYPES_H
#define NXP_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Protocol version: "NXP\x01" */
#define NXP_VERSION_1        ((uint32_t)0x4E585001)
#define NXP_VERSION_CURRENT  NXP_VERSION_1

/* Limits */
#define NXP_MAX_CID_LEN          20
#define NXP_MAX_UDP_PAYLOAD       1472   /* 1500 MTU - 20 IP - 8 UDP */
#define NXP_PACKET_BUF_SIZE       1536   /* Slightly larger, aligned */
#define NXP_AEAD_TAG_LEN          16
#define NXP_MAX_STREAMS_DEFAULT   256
#define NXP_IDLE_TIMEOUT_DEFAULT  30000  /* 30 seconds in ms */

/* Connection ID */
typedef struct nxp_conn_id {
    uint8_t  data[NXP_MAX_CID_LEN];
    uint8_t  len;
} nxp_conn_id;

/* Network address */
typedef struct nxp_addr {
    union {
        struct {
            uint8_t  family;           /* AF_INET or AF_INET6 */
            uint16_t port;
            union {
                uint8_t  v4[4];
                uint8_t  v6[16];
            } ip;
        };
        uint8_t raw[28];               /* Enough for sockaddr_in6 */
    };
} nxp_addr;

/* Stream types */
typedef enum nxp_stream_type {
    NXP_STREAM_RELIABLE = 0,           /* Ordered, reliable (like TCP) */
    NXP_STREAM_FAST     = 1,           /* Unordered, unreliable (raw UDP-like) */
    NXP_STREAM_MEDIA    = 2,           /* Partially reliable (drop old) */
    NXP_STREAM_FILE     = 3,           /* Reliable, ordered, bulk optimized */
} nxp_stream_type;

/* Connection states */
typedef enum nxp_conn_state {
    NXP_CONN_IDLE = 0,
    NXP_CONN_HANDSHAKE_INITIAL,
    NXP_CONN_HANDSHAKE_IN_PROGRESS,
    NXP_CONN_ESTABLISHED,
    NXP_CONN_CLOSING,
    NXP_CONN_DRAINING,
    NXP_CONN_CLOSED,
} nxp_conn_state;

/* Stream states */
typedef enum nxp_stream_state {
    NXP_STREAM_IDLE = 0,
    NXP_STREAM_OPEN,
    NXP_STREAM_HALF_CLOSED_LOCAL,
    NXP_STREAM_HALF_CLOSED_REMOTE,
    NXP_STREAM_CLOSED,
    NXP_STREAM_RESET,
} nxp_stream_state;

/* Crypto levels (packet number spaces) */
typedef enum nxp_crypto_level {
    NXP_CRYPTO_INITIAL     = 0,
    NXP_CRYPTO_HANDSHAKE   = 1,
    NXP_CRYPTO_APPLICATION = 2,
} nxp_crypto_level;

/* Packet types (long header) */
typedef enum nxp_packet_type {
    NXP_PKT_INITIAL   = 0,
    NXP_PKT_HANDSHAKE = 1,
    NXP_PKT_RETRY     = 2,
    NXP_PKT_ZERO_RTT  = 3,
    NXP_PKT_SHORT     = 4,            /* Short header (1-RTT) */
} nxp_packet_type;

/* Frame types */
typedef enum nxp_frame_type {
    NXP_FRAME_PADDING             = 0x00,
    NXP_FRAME_PING                = 0x01,
    NXP_FRAME_ACK                 = 0x02,
    NXP_FRAME_ACK_ECN             = 0x03,
    NXP_FRAME_RESET_STREAM        = 0x04,
    NXP_FRAME_STOP_SENDING        = 0x05,
    NXP_FRAME_CRYPTO              = 0x06,
    NXP_FRAME_NEW_TOKEN           = 0x07,
    NXP_FRAME_STREAM              = 0x08, /* 0x08-0x0F (OFF/LEN/FIN bits) */
    NXP_FRAME_MAX_DATA            = 0x10,
    NXP_FRAME_MAX_STREAM_DATA     = 0x11,
    NXP_FRAME_MAX_STREAMS_BIDI    = 0x12,
    NXP_FRAME_MAX_STREAMS_UNI     = 0x13,
    NXP_FRAME_DATA_BLOCKED        = 0x14,
    NXP_FRAME_STREAM_DATA_BLOCKED = 0x15,
    NXP_FRAME_STREAMS_BLOCKED_BIDI= 0x16,
    NXP_FRAME_STREAMS_BLOCKED_UNI = 0x17,
    NXP_FRAME_NEW_CONNECTION_ID   = 0x18,
    NXP_FRAME_RETIRE_CONNECTION_ID= 0x19,
    NXP_FRAME_PATH_CHALLENGE      = 0x1A,
    NXP_FRAME_PATH_RESPONSE       = 0x1B,
    NXP_FRAME_CONNECTION_CLOSE    = 0x1C,
    NXP_FRAME_CONNECTION_CLOSE_APP= 0x1D,
    NXP_FRAME_HANDSHAKE_DONE      = 0x1E,
    /* NXP extensions */
    NXP_FRAME_HEARTBEAT           = 0x20,
    NXP_FRAME_STREAM_PRIORITY     = 0x21,
    NXP_FRAME_DATAGRAM            = 0x22,
} nxp_frame_type;

/* Shutdown direction for streams */
typedef enum nxp_shutdown_dir {
    NXP_SHUTDOWN_READ  = 0,
    NXP_SHUTDOWN_WRITE = 1,
    NXP_SHUTDOWN_BOTH  = 2,
} nxp_shutdown_dir;

/* Connection statistics */
typedef struct nxp_conn_stats {
    uint64_t bytes_sent;
    uint64_t bytes_recv;
    uint64_t packets_sent;
    uint64_t packets_recv;
    uint64_t packets_lost;
    uint64_t rtt_min_us;
    uint64_t rtt_smoothed_us;
    uint64_t rtt_var_us;
    uint64_t cwnd;
    uint64_t bytes_in_flight;
    uint64_t streams_opened;
    uint64_t handshake_time_us;
} nxp_conn_stats;

/* Opaque handles for the public API */
typedef struct nxp_conn     nxp_conn;
typedef struct nxp_stream   nxp_stream;
typedef struct nxp_listener nxp_listener;
typedef struct nxp_config   nxp_config;

/* Callback types */
typedef void (*nxp_conn_cb)(nxp_conn *conn, void *user_data);
typedef void (*nxp_stream_cb)(nxp_stream *stream, void *user_data);
typedef void (*nxp_stream_accept_cb)(nxp_conn *conn, nxp_stream *stream, void *user_data);
typedef void (*nxp_listener_cb)(nxp_listener *listener, nxp_conn *conn, void *user_data);

#endif /* NXP_TYPES_H */
