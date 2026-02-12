/*
 * NXP Connection Engine - Internal Header
 *
 * Phase 4+5: Connection lifecycle, streams, ACK/loss, flow control,
 * scheduler, and cryptographic packet protection.
 * Uses Sans-I/O design: the connection never touches sockets directly.
 * All packet I/O goes through nxp_conn_recv() and nxp_conn_send().
 */
#ifndef NXP_CONNECTION_INTERNAL_H
#define NXP_CONNECTION_INTERNAL_H

#include "nxp/nxp_types.h"
#include "nxp/nxp_error.h"
#include "packet_internal.h"
#include "frame_internal.h"
#include "hash_map.h"
#include "crypto/crypto_internal.h"

/* Forward declaration for handshake */
typedef struct nxp_handshake nxp_handshake;

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#else
#include <sys/types.h>
#endif

/* ── Limits ────────────────────────────────────────────── */

#define NXP_MAX_SENT_PACKETS     1024
#define NXP_MAX_RECV_RANGES      64
#define NXP_STREAM_BUF_SIZE      (64u * 1024u)  /* 64 KB default send/recv buf */
#define NXP_MAX_STREAM_FRAMES_PER_PKT 8
#define NXP_DEFAULT_MAX_DATA     (1u * 1024u * 1024u)  /* 1 MB */
#define NXP_DEFAULT_MAX_STREAM_DATA (256u * 1024u)      /* 256 KB */
#define NXP_DEFAULT_IDLE_TIMEOUT (30ULL * 1000000ULL)   /* 30s in us */
#define NXP_ACK_DELAY_US         25000                  /* 25ms max ack delay */
#define NXP_LOSS_DELAY_FACTOR    9                      /* 9/8 * max(srtt, latest_rtt) */
#define NXP_PACKET_REORDER_THRESH 3

/* ── Stream Frame Record (for retransmission on loss) ─── */

typedef struct nxp_stream_frame_record {
    uint64_t stream_id;
    uint64_t offset;
    uint32_t len;
    bool     fin;
} nxp_stream_frame_record;

/* ── Sent Packet Metadata ──────────────────────────────── */

typedef struct nxp_sent_pkt {
    uint64_t pkt_num;
    uint64_t sent_time;
    uint32_t sent_bytes;        /* Size of entire packet */
    bool     ack_eliciting;
    bool     in_flight;
    bool     declared_lost;
    /* Stream data carried in this packet (for retransmission) */
    nxp_stream_frame_record frames[NXP_MAX_STREAM_FRAMES_PER_PKT];
    uint8_t  frame_count;
} nxp_sent_pkt;

/* ── Received Packet Number Range ──────────────────────── */

typedef struct nxp_recv_pn_range {
    uint64_t start;             /* Inclusive */
    uint64_t end;               /* Inclusive */
} nxp_recv_pn_range;

/* ── ACK + Loss Detection State ────────────────────────── */

typedef struct nxp_ack_state {
    /* Received packet tracking (for generating outbound ACKs) */
    nxp_recv_pn_range recv_ranges[NXP_MAX_RECV_RANGES];
    uint32_t          recv_range_count;
    uint64_t          largest_recv_pn;
    uint64_t          largest_recv_time;
    bool              ack_needed;
    uint64_t          ack_delay_timer;  /* Deadline for delayed ACK */

    /* RTT estimation */
    uint64_t smoothed_rtt;
    uint64_t rtt_var;
    uint64_t min_rtt;
    uint64_t latest_rtt;
    bool     has_rtt;

    /* Sent packet tracking */
    nxp_sent_pkt *sent;
    uint32_t      sent_count;
    uint32_t      sent_cap;
    uint64_t      bytes_in_flight;

    /* Loss detection */
    uint64_t loss_time;         /* When next time-based loss fires */
    uint32_t pto_count;
    uint64_t time_of_last_ack_eliciting;
} nxp_ack_state;

/* ── Flow Control ──────────────────────────────────────── */

typedef struct nxp_flow_ctrl {
    /* Sending side (limited by peer's MAX_DATA) */
    uint64_t peer_max_data;
    uint64_t data_sent;

    /* Receiving side (we advertise MAX_DATA to peer) */
    uint64_t local_max_data;
    uint64_t data_recv;
    uint64_t max_data_next;     /* Next MAX_DATA to send when window used */
    bool     send_max_data;     /* Need to send MAX_DATA frame */
} nxp_flow_ctrl;

/* ── Stream ────────────────────────────────────────────── */

typedef struct nxp_stream_s nxp_stream_s;

struct nxp_stream_s {
    uint64_t          id;
    nxp_stream_type   type;
    nxp_stream_state  state;
    uint8_t           priority;

    /* Send buffer */
    struct {
        uint8_t *data;
        size_t   cap;
        uint64_t write_offset;  /* Next app write position */
        uint64_t sent_offset;   /* Next offset to send on wire */
        uint64_t acked_offset;  /* Contiguous acked up to here */
        bool     fin;           /* App has queued FIN */
        bool     fin_sent;      /* FIN has been sent on wire */
        bool     fin_acked;     /* FIN has been acked */
    } send;

    /* Recv buffer */
    struct {
        uint8_t *data;
        size_t   cap;
        uint64_t read_offset;   /* Next offset delivered to app */
        uint64_t recv_offset;   /* Contiguous received up to here */
        bool     fin_received;
        uint64_t fin_offset;
    } recv;

    /* Per-stream flow control */
    nxp_flow_ctrl flow;

    /* Scheduler linkage (circular doubly-linked list) */
    nxp_stream_s *sched_next;
    nxp_stream_s *sched_prev;
    bool          scheduled;    /* In the scheduler queue */
};

/* ── Connection Config ─────────────────────────────────── */

typedef struct nxp_conn_config {
    nxp_conn_id scid;
    uint8_t     peer_dcid_len;
    nxp_addr    peer_addr;
    uint64_t    idle_timeout_us;
    uint64_t    initial_max_data;
    uint64_t    initial_max_stream_data;
    uint32_t    max_streams_bidi;
    uint32_t    max_streams_uni;
} nxp_conn_config;

/* ── Connection ────────────────────────────────────────── */

struct nxp_conn {
    nxp_conn_id    scid;
    nxp_conn_id    dcid;
    nxp_conn_state state;
    bool           is_server;

    nxp_addr       peer_addr;

    /* Packet numbering (application / 1-RTT space) */
    uint64_t       next_pkt_num;

    /* ACK + loss detection */
    nxp_ack_state  ack;

    /* Crypto state (Phase 5) */
    nxp_crypto_state crypto;      /* Application-level keys */
    nxp_handshake   *handshake;   /* Active handshake (nullptr when complete) */

    /* Streams */
    nxp_hash_map  *streams;
    uint64_t       next_bidi_id;    /* Next local bidi stream ID */
    uint64_t       next_uni_id;     /* Next local uni stream ID */
    uint32_t       local_max_streams_bidi;
    uint32_t       local_max_streams_uni;
    uint32_t       peer_max_streams_bidi;
    uint32_t       peer_max_streams_uni;
    uint32_t       open_bidi_count;
    uint32_t       open_uni_count;

    /* Connection-level flow control */
    nxp_flow_ctrl  conn_flow;

    /* Scheduler (circular doubly-linked list of streams with data) */
    nxp_stream_s  *sched_head;

    /* Config */
    uint64_t       initial_max_stream_data;
    uint64_t       idle_timeout_us;
    uint64_t       last_activity_us;

    /* Pending control frames */
    bool           send_ping;
    bool           send_conn_close;
    uint64_t       close_error_code;

    /* Stats */
    nxp_conn_stats stats;
};

/* ── ACK API ───────────────────────────────────────────── */

void nxp_ack_init(nxp_ack_state *ack);
void nxp_ack_cleanup(nxp_ack_state *ack);

/* Record that we received a packet */
void nxp_ack_on_pkt_recv(nxp_ack_state *ack, uint64_t pkt_num, uint64_t now_us,
                         bool ack_eliciting);

/* Record that we sent a packet */
void nxp_ack_on_pkt_sent(nxp_ack_state *ack, const nxp_sent_pkt *pkt);

/* Process an incoming ACK frame. Calls back for each newly acked packet. */
typedef void (*nxp_ack_cb)(void *ctx, const nxp_sent_pkt *pkt);
typedef void (*nxp_loss_cb)(void *ctx, const nxp_sent_pkt *pkt);

void nxp_ack_on_ack_recv(nxp_ack_state *ack, const nxp_frame_ack *frame,
                         uint64_t now_us,
                         nxp_ack_cb on_ack, nxp_loss_cb on_loss, void *ctx);

/* Build an ACK frame from our received-PN state */
bool nxp_ack_build_frame(const nxp_ack_state *ack, nxp_frame_ack *out,
                         uint64_t now_us);

/* Get the loss detection timeout (UINT64_MAX if none) */
uint64_t nxp_ack_loss_timeout(const nxp_ack_state *ack);

/* Fire the loss detection timer */
void nxp_ack_on_loss_timeout(nxp_ack_state *ack, uint64_t now_us,
                             nxp_loss_cb on_loss, void *ctx);

/* ── Flow Control API ──────────────────────────────────── */

void nxp_flow_init(nxp_flow_ctrl *fc, uint64_t local_max, uint64_t peer_max);

/* Check if we can send `len` bytes (within peer's limit) */
[[nodiscard]] bool nxp_flow_can_send(const nxp_flow_ctrl *fc, uint64_t len);

/* Record that we sent `len` bytes */
void nxp_flow_on_send(nxp_flow_ctrl *fc, uint64_t len);

/* Record that we received `len` bytes */
void nxp_flow_on_recv(nxp_flow_ctrl *fc, uint64_t len);

/* Record that the app consumed `len` bytes (opens recv window) */
void nxp_flow_on_consume(nxp_flow_ctrl *fc, uint64_t len);

/* Update peer's MAX_DATA */
void nxp_flow_set_peer_max(nxp_flow_ctrl *fc, uint64_t max_data);

/* Check if we need to send a MAX_DATA update */
[[nodiscard]] bool nxp_flow_should_update(const nxp_flow_ctrl *fc);

/* Get the MAX_DATA value to send and mark as sent */
uint64_t nxp_flow_get_update(nxp_flow_ctrl *fc);

/* ── Stream API ────────────────────────────────────────── */

nxp_stream_s *nxp_stream_create(uint64_t id, nxp_stream_type type,
                                uint64_t initial_max_send,
                                uint64_t initial_max_recv);
void nxp_stream_destroy(nxp_stream_s *s);

/* Application writes data to the stream */
[[nodiscard]] ssize_t nxp_stream_write(nxp_stream_s *s, const uint8_t *data,
                                       size_t len, bool fin);

/* Application reads data from the stream */
[[nodiscard]] ssize_t nxp_stream_read(nxp_stream_s *s, uint8_t *buf,
                                      size_t buf_len, bool *fin);

/* Get how much unsent data is available */
[[nodiscard]] uint64_t nxp_stream_unsent(const nxp_stream_s *s);

/* Fill a STREAM frame with the next chunk of data to send */
bool nxp_stream_fill_frame(nxp_stream_s *s, nxp_frame_stream *out,
                           size_t max_data_len);

/* Process an incoming STREAM frame */
nxp_result nxp_stream_on_recv(nxp_stream_s *s, const nxp_frame_stream *f);

/* Mark a range as acked (called when ACK received) */
void nxp_stream_on_ack(nxp_stream_s *s, uint64_t offset, uint32_t len, bool fin);

/* Mark a range as lost (rewind sent_offset for retransmission) */
void nxp_stream_on_loss(nxp_stream_s *s, uint64_t offset, uint32_t len, bool fin);

/* ── Scheduler API ─────────────────────────────────────── */

/* Add a stream to the send scheduler */
void nxp_sched_add(nxp_conn *conn, nxp_stream_s *s);

/* Remove a stream from the scheduler */
void nxp_sched_remove(nxp_conn *conn, nxp_stream_s *s);

/* Get next stream that has data to send (round-robin). Returns nullptr if none. */
nxp_stream_s *nxp_sched_next(nxp_conn *conn);

/* ── Connection API (Sans-I/O) ─────────────────────────── */

/* Create a connection. Returns nullptr on failure. */
[[nodiscard]] nxp_conn *nxp_conn_create(const nxp_conn_config *config,
                                        bool is_server);

/* Destroy a connection and all its streams. */
void nxp_conn_destroy(nxp_conn *conn);

/* Force connection into ESTABLISHED state with known peer CID.
 * Used for Phase 4 testing (null cipher, no handshake). */
void nxp_conn_set_established(nxp_conn *conn, const nxp_conn_id *dcid);

/* Start a 1-RTT handshake (Phase 5).
 * Client: pass server's DCID. Server: pass our SCID.
 * On completion, transitions to ESTABLISHED with crypto enabled. */
[[nodiscard]] nxp_result nxp_conn_start_handshake(nxp_conn *conn,
                                                    const nxp_conn_id *peer_cid);

/* Process an incoming UDP datagram. */
[[nodiscard]] nxp_result nxp_conn_recv(nxp_conn *conn, const uint8_t *data,
                                       size_t len, uint64_t now_us);

/* Generate an outgoing UDP datagram.
 * Returns bytes written (0 = nothing to send). */
[[nodiscard]] ssize_t nxp_conn_send(nxp_conn *conn, uint8_t *out,
                                    size_t max_len, uint64_t now_us);

/* Get the next timer deadline (UINT64_MAX if none). */
[[nodiscard]] uint64_t nxp_conn_timeout(const nxp_conn *conn, uint64_t now_us);

/* Process a timer event. */
void nxp_conn_on_timeout(nxp_conn *conn, uint64_t now_us);

/* Open a new stream. Returns the stream ID via out parameter. */
[[nodiscard]] nxp_result nxp_conn_open_stream(nxp_conn *conn,
                                              uint64_t *stream_id_out,
                                              nxp_stream_type type,
                                              bool unidirectional);

/* Write data to a stream. Returns bytes written (may be partial). */
[[nodiscard]] ssize_t nxp_conn_stream_send(nxp_conn *conn, uint64_t stream_id,
                                           const uint8_t *data, size_t len,
                                           bool fin);

/* Read data from a stream. Returns bytes read, 0 if no data, -1 on error. */
[[nodiscard]] ssize_t nxp_conn_stream_recv(nxp_conn *conn, uint64_t stream_id,
                                           uint8_t *buf, size_t buf_len,
                                           bool *fin);

/* Get connection state. */
[[nodiscard]] nxp_conn_state nxp_conn_get_state(const nxp_conn *conn);

/* Close the connection with an error code. */
nxp_result nxp_conn_close(nxp_conn *conn, uint64_t error_code);

#endif /* NXP_CONNECTION_INTERNAL_H */
