/*
 * NXP Frame Engine - Internal Header
 *
 * Frame construction and parsing for all NXP frame types.
 * Frames are the units of data within a packet payload.
 */
#ifndef NXP_FRAME_INTERNAL_H
#define NXP_FRAME_INTERNAL_H

#include "nxp/nxp_types.h"
#include "nxp/nxp_error.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * STREAM frame type encoding (0x08-0x0F):
 * Bit 2 (0x04): OFF bit - Offset field present
 * Bit 1 (0x02): LEN bit - Length field present
 * Bit 0 (0x01): FIN bit - Final frame for stream
 */
#define NXP_FRAME_STREAM_BASE  0x08
#define NXP_FRAME_STREAM_OFF   0x04
#define NXP_FRAME_STREAM_LEN   0x02
#define NXP_FRAME_STREAM_FIN   0x01
#define NXP_FRAME_STREAM_MASK  0xF8  /* Top 5 bits must be 00001 */

/* ── Parsed Frame Structures ── */

typedef struct nxp_frame_padding {
    uint64_t count;    /* Number of padding bytes (including the type byte) */
} nxp_frame_padding;

typedef struct nxp_frame_ping {
    /* No fields - just the type byte */
    uint8_t _unused;
} nxp_frame_ping;

/* ACK range: [start, end] inclusive of packet numbers */
typedef struct nxp_ack_range {
    uint64_t gap;       /* Number of ack'd packets before this range minus 1 */
    uint64_t ack_range; /* Number of contiguous packets in this range minus 1 */
} nxp_ack_range;

#define NXP_ACK_MAX_RANGES 64

typedef struct nxp_frame_ack {
    uint64_t       largest_acked;
    uint64_t       ack_delay;
    uint64_t       first_ack_range;  /* Range count minus 1 for largest acked */
    uint32_t       range_count;
    nxp_ack_range  ranges[NXP_ACK_MAX_RANGES];

    /* ECN counters (only when frame type is ACK_ECN) */
    bool           has_ecn;
    uint64_t       ect0_count;
    uint64_t       ect1_count;
    uint64_t       ecn_ce_count;
} nxp_frame_ack;

typedef struct nxp_frame_reset_stream {
    uint64_t stream_id;
    uint64_t error_code;
    uint64_t final_size;
} nxp_frame_reset_stream;

typedef struct nxp_frame_stop_sending {
    uint64_t stream_id;
    uint64_t error_code;
} nxp_frame_stop_sending;

typedef struct nxp_frame_crypto {
    uint64_t       offset;
    uint64_t       length;
    const uint8_t *data;    /* Points into source buffer */
} nxp_frame_crypto;

typedef struct nxp_frame_new_token {
    uint64_t       token_len;
    const uint8_t *token;   /* Points into source buffer */
} nxp_frame_new_token;

typedef struct nxp_frame_stream {
    uint64_t       stream_id;
    uint64_t       offset;
    uint64_t       length;
    bool           has_offset;
    bool           has_length;
    bool           fin;
    const uint8_t *data;    /* Points into source buffer */
} nxp_frame_stream;

typedef struct nxp_frame_max_data {
    uint64_t max_data;
} nxp_frame_max_data;

typedef struct nxp_frame_max_stream_data {
    uint64_t stream_id;
    uint64_t max_stream_data;
} nxp_frame_max_stream_data;

typedef struct nxp_frame_max_streams {
    uint64_t max_streams;
    bool     is_bidi;       /* true=BIDI, false=UNI */
} nxp_frame_max_streams;

typedef struct nxp_frame_data_blocked {
    uint64_t max_data;
} nxp_frame_data_blocked;

typedef struct nxp_frame_stream_data_blocked {
    uint64_t stream_id;
    uint64_t max_stream_data;
} nxp_frame_stream_data_blocked;

typedef struct nxp_frame_streams_blocked {
    uint64_t max_streams;
    bool     is_bidi;
} nxp_frame_streams_blocked;

typedef struct nxp_frame_new_connection_id {
    uint64_t    seq_num;
    uint64_t    retire_prior_to;
    nxp_conn_id cid;
    uint8_t     stateless_reset_token[16];
} nxp_frame_new_connection_id;

typedef struct nxp_frame_retire_connection_id {
    uint64_t seq_num;
} nxp_frame_retire_connection_id;

typedef struct nxp_frame_path_challenge {
    uint8_t data[8];
} nxp_frame_path_challenge;

typedef struct nxp_frame_path_response {
    uint8_t data[8];
} nxp_frame_path_response;

typedef struct nxp_frame_connection_close {
    uint64_t       error_code;
    uint64_t       frame_type;  /* Only for transport-level close (0x1C) */
    uint64_t       reason_len;
    const uint8_t *reason;      /* Points into source buffer */
    bool           is_app;      /* true = app-level (0x1D), false = transport (0x1C) */
} nxp_frame_connection_close;

typedef struct nxp_frame_handshake_done {
    uint8_t _unused;
} nxp_frame_handshake_done;

/* NXP extension frames */

typedef struct nxp_frame_heartbeat {
    uint64_t timestamp_us;  /* Sender's monotonic clock */
} nxp_frame_heartbeat;

typedef struct nxp_frame_stream_priority {
    uint64_t stream_id;
    uint8_t  priority;      /* 0-255, lower = higher priority */
} nxp_frame_stream_priority;

typedef struct nxp_frame_datagram {
    uint64_t       length;
    const uint8_t *data;    /* Points into source buffer */
} nxp_frame_datagram;

/* ── Tagged union for parsed frames ── */

typedef struct nxp_frame {
    nxp_frame_type type;
    union {
        nxp_frame_padding              padding;
        nxp_frame_ping                 ping;
        nxp_frame_ack                  ack;
        nxp_frame_reset_stream         reset_stream;
        nxp_frame_stop_sending         stop_sending;
        nxp_frame_crypto               crypto;
        nxp_frame_new_token            new_token;
        nxp_frame_stream               stream;
        nxp_frame_max_data             max_data;
        nxp_frame_max_stream_data      max_stream_data;
        nxp_frame_max_streams          max_streams;
        nxp_frame_data_blocked         data_blocked;
        nxp_frame_stream_data_blocked  stream_data_blocked;
        nxp_frame_streams_blocked      streams_blocked;
        nxp_frame_new_connection_id    new_connection_id;
        nxp_frame_retire_connection_id retire_connection_id;
        nxp_frame_path_challenge       path_challenge;
        nxp_frame_path_response        path_response;
        nxp_frame_connection_close     connection_close;
        nxp_frame_handshake_done       handshake_done;
        nxp_frame_heartbeat            heartbeat;
        nxp_frame_stream_priority      stream_priority;
        nxp_frame_datagram             datagram;
    };
} nxp_frame;

/* ── Encoding Functions ── */

/* Each returns bytes written, or 0 on error */

[[nodiscard]] size_t nxp_frame_encode_padding(uint64_t count, uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_ping(uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_ack(const nxp_frame_ack *f, uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_reset_stream(const nxp_frame_reset_stream *f, uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_stop_sending(const nxp_frame_stop_sending *f, uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_crypto(const nxp_frame_crypto *f, uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_new_token(const nxp_frame_new_token *f, uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_stream(const nxp_frame_stream *f, uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_max_data(uint64_t max_data, uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_max_stream_data(const nxp_frame_max_stream_data *f, uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_max_streams(const nxp_frame_max_streams *f, uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_data_blocked(uint64_t max_data, uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_stream_data_blocked(const nxp_frame_stream_data_blocked *f, uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_streams_blocked(const nxp_frame_streams_blocked *f, uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_new_connection_id(const nxp_frame_new_connection_id *f, uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_retire_connection_id(uint64_t seq_num, uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_path_challenge(const uint8_t data[8], uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_path_response(const uint8_t data[8], uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_connection_close(const nxp_frame_connection_close *f, uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_handshake_done(uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_heartbeat(uint64_t timestamp_us, uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_stream_priority(const nxp_frame_stream_priority *f, uint8_t *buf, size_t buf_len);
[[nodiscard]] size_t nxp_frame_encode_datagram(const nxp_frame_datagram *f, uint8_t *buf, size_t buf_len);

/* ── Decoding ── */

/*
 * Decode one frame from buf.
 * Returns bytes consumed, or 0 on error.
 * The frame struct is filled with parsed data.
 * Pointer fields (data, reason, token) point into the source buffer.
 */
[[nodiscard]] size_t nxp_frame_decode(
    const uint8_t *buf, size_t buf_len,
    nxp_frame *out
);

/*
 * Check if a frame type is ack-eliciting
 * (i.e., the receiver must send an ACK in response).
 */
[[nodiscard]] static inline bool nxp_frame_is_ack_eliciting(nxp_frame_type type) {
    return type != NXP_FRAME_PADDING &&
           type != NXP_FRAME_ACK &&
           type != NXP_FRAME_ACK_ECN &&
           type != NXP_FRAME_CONNECTION_CLOSE &&
           type != NXP_FRAME_CONNECTION_CLOSE_APP;
}

/*
 * Check if a byte is a STREAM frame type (0x08-0x0F).
 */
[[nodiscard]] static inline bool nxp_frame_is_stream(uint8_t type_byte) {
    return (type_byte & NXP_FRAME_STREAM_MASK) == NXP_FRAME_STREAM_BASE;
}

#endif /* NXP_FRAME_INTERNAL_H */
