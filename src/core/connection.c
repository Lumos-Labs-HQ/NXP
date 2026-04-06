/*
 * NXP Connection Engine - Sans-I/O Implementation
 *
 * Phase 4+5: Full connection lifecycle, packet processing, stream
 * multiplexing, datagram generation, and AEAD packet protection.
 * Never touches sockets - consumes raw bytes via nxp_conn_recv(),
 * produces bytes via nxp_conn_send().
 *
 * If crypto keys are not set (conn->crypto.available == false),
 * operates in plaintext mode for backward compatibility with Phase 4 tests.
 */
#include "connection_internal.h"
#include "handshake_internal.h"
#include "packet_internal.h"
#include "frame_internal.h"
#include "hash_map.h"
#include "util/varint.h"
#include "util/flight_recorder.h"
#include "util/error_tracker.h"
#include "logging/nxp_log.h"
#include "congestion/bbr_internal.h"
#include "crypto/secure_mem.h"

#include <stdlib.h>
#include <string.h>


/* Validate connection state transition */
static bool is_valid_conn_state_transition(nxp_conn_state from, nxp_conn_state to) {
    /* Valid transitions:
     * IDLE -> HANDSHAKE_INITIAL
     * HANDSHAKE_INITIAL -> HANDSHAKE_IN_PROGRESS
     * HANDSHAKE_IN_PROGRESS -> ESTABLISHED
     * ESTABLISHED -> CLOSING
     * ESTABLISHED -> DRAINING
     * CLOSING -> DRAINING
     * CLOSING -> CLOSED
     * DRAINING -> CLOSED
     * Any -> CLOSED (error cases)
     */
    if (to == NXP_CONN_CLOSED) return true; /* Always allow close */
    
    switch (from) {
    case NXP_CONN_IDLE:
        return to == NXP_CONN_HANDSHAKE_INITIAL || to == NXP_CONN_ESTABLISHED;
    case NXP_CONN_HANDSHAKE_INITIAL:
        return to == NXP_CONN_HANDSHAKE_IN_PROGRESS || to == NXP_CONN_ESTABLISHED || to == NXP_CONN_HANDSHAKE_INITIAL;
    case NXP_CONN_HANDSHAKE_IN_PROGRESS:
        return to == NXP_CONN_ESTABLISHED;
    case NXP_CONN_ESTABLISHED:
        return to == NXP_CONN_CLOSING || to == NXP_CONN_DRAINING || to == NXP_CONN_ESTABLISHED;
    case NXP_CONN_CLOSING:
        return to == NXP_CONN_DRAINING || to == NXP_CONN_CLOSED;
    case NXP_CONN_DRAINING:
        return to == NXP_CONN_CLOSED;
    case NXP_CONN_CLOSED:
        return false; /* No transitions from closed */
    }
    return false;
}

static inline void set_conn_state(nxp_conn *conn, nxp_conn_state new_state) {
    if (!is_valid_conn_state_transition(conn->state, new_state)) {
        /* Invalid transition - log and ignore */
        NXP_FLIGHT_ERROR(NXP_ERR_INVALID_ARGUMENT, "invalid state transition");
        return;
    }
    NXP_FLIGHT_CONN_STATE(conn->state, new_state);
    conn->state = new_state;
}

/* ── Helpers ───────────────────────────────────────────── */

/* Callback context for ACK processing */
typedef struct ack_ctx {
    nxp_conn *conn;
} ack_ctx;

/* When a sent packet is ACKed, update its stream data + CC */
static void on_packet_acked(void *ctx, const nxp_sent_pkt *pkt) {
    ack_ctx *ac = (ack_ctx *)ctx;
    nxp_conn *conn = ac->conn;

    conn->stats.packets_sent++;  /* Track acknowledged */

    for (uint8_t i = 0; i < pkt->frame_count; i++) {
        const nxp_stream_frame_record *rec = &pkt->frames[i];
        nxp_stream_s *s = (nxp_stream_s *)nxp_hash_map_get(conn->streams, rec->stream_id);
        if (s != nullptr) {
            nxp_stream_on_ack(s, rec->offset, rec->len, rec->fin);

            /* Phase 8: Notify writable if stream was blocked */
            if (s->blocked && s->on_writable != nullptr) {
                s->blocked = false;
                s->on_writable(s->writable_user_data, rec->stream_id);
            }
        }
    }

    /* Phase 7: Update delivery rate and notify CC */
    if (conn->cc_ops != nullptr && conn->cc_state != nullptr) {
        uint64_t now_us = pkt->sent_time + conn->ack.latest_rtt;
        nxp_dr_on_packet_acked(&conn->delivery_rate, &pkt->dr_state,
                                pkt->sent_bytes, now_us);

        nxp_cc_ack_info info = {
            .pkt_num           = pkt->pkt_num,
            .acked_bytes       = pkt->sent_bytes,
            .sent_time         = pkt->sent_time,
            .now_us            = now_us,
            .rtt_us            = conn->ack.latest_rtt,
            .min_rtt_us        = conn->ack.min_rtt,
            .bytes_in_flight   = conn->ack.bytes_in_flight,
            .delivery_rate_bps = conn->delivery_rate.rate_sample_bps,
            .round_count       = conn->delivery_rate.round_count,
            .is_app_limited    = conn->delivery_rate.rate_is_app_limited,
            .round_start       = conn->delivery_rate.round_start,
        };
        conn->cc_ops->on_ack(conn->cc_state, &info);

        /* Update pacer rate */
        nxp_pacer_set_rate(&conn->pacer,
                            conn->cc_ops->get_pacing_rate(conn->cc_state));
    }
}

/* When a sent packet is declared lost, rewind its stream data + notify CC */
static void on_packet_lost(void *ctx, const nxp_sent_pkt *pkt) {
    ack_ctx *ac = (ack_ctx *)ctx;
    nxp_conn *conn = ac->conn;

    conn->stats.packets_lost++;

    for (uint8_t i = 0; i < pkt->frame_count; i++) {
        const nxp_stream_frame_record *rec = &pkt->frames[i];
        nxp_stream_s *s = (nxp_stream_s *)nxp_hash_map_get(conn->streams, rec->stream_id);
        if (s != nullptr) {
            nxp_stream_on_loss(s, rec->offset, rec->len, rec->fin);
            /* Re-add to scheduler for retransmission */
            nxp_sched_add(conn, s);
        }
    }

    /* Phase 7: Notify CC of loss */
    if (conn->cc_ops != nullptr && conn->cc_state != nullptr) {
        nxp_cc_loss_info info = {
            .lost_bytes      = pkt->sent_bytes,
            .now_us          = pkt->sent_time, /* approximation */
            .bytes_in_flight = conn->ack.bytes_in_flight,
        };
        conn->cc_ops->on_loss(conn->cc_state, &info);
    }
}

/* ── Stream ID Helpers ────────────────────────────────── */

/*
 * NXP Stream ID encoding (lower 4 bits):
 *   Bit 0: Initiator (0=client, 1=server)
 *   Bit 1: Direction (0=bidi, 1=uni)
 *   Bits 2-3: Stream type (00=Reliable, 01=Fast, 10=Media, 11=File)
 *
 * The upper bits are the stream sequence number << 4.
 */
static uint64_t make_stream_id(uint64_t seq, nxp_stream_type type,
                                bool unidirectional, bool is_server) {
    uint64_t id = seq << 4;
    id |= ((uint64_t)type & 0x3) << 2;
    if (unidirectional) id |= 0x2;
    if (is_server)      id |= 0x1;
    return id;
}

static bool stream_id_is_local(uint64_t stream_id, bool is_server) {
    return ((stream_id & 0x1) != 0) == is_server;
}

/* ── Create / Destroy ─────────────────────────────────── */

nxp_conn *nxp_conn_create(const nxp_conn_config *config, bool is_server) {
    nxp_conn *conn = (nxp_conn *)calloc(1, sizeof(nxp_conn));
    if (conn == nullptr) return nullptr;

    conn->scid      = config->scid;
    conn->state     = NXP_CONN_IDLE;
    conn->is_server = is_server;
    conn->peer_addr = config->peer_addr;

    /* ACK state */
    nxp_ack_init(&conn->ack);

    /* Streams hash map */
    conn->streams = nxp_hash_map_create(64);
    if (conn->streams == nullptr) {
        nxp_ack_cleanup(&conn->ack);
        free(conn);
        return nullptr;
    }

    /* Stream ID counters: client starts at 0, server starts at 1 (low bit) */
    conn->next_bidi_id = 0;
    conn->next_uni_id  = 0;

    /* Stream limits */
    conn->local_max_streams_bidi = config->max_streams_bidi;
    conn->local_max_streams_uni  = config->max_streams_uni;
    conn->peer_max_streams_bidi  = config->max_streams_bidi;
    conn->peer_max_streams_uni   = config->max_streams_uni;

    /* Connection-level flow control */
    uint64_t local_max = config->initial_max_data;
    if (local_max == 0) local_max = NXP_DEFAULT_MAX_DATA;
    nxp_flow_init(&conn->conn_flow, local_max, local_max);

    /* Per-stream default */
    conn->initial_max_stream_data = config->initial_max_stream_data;
    if (conn->initial_max_stream_data == 0) {
        conn->initial_max_stream_data = NXP_DEFAULT_MAX_STREAM_DATA;
    }

    /* Idle timeout */
    conn->idle_timeout_us = config->idle_timeout_us;
    if (conn->idle_timeout_us == 0) {
        conn->idle_timeout_us = NXP_DEFAULT_IDLE_TIMEOUT;
    }

    /* Scheduler starts empty */
    conn->sched_head = nullptr;

    /* Phase 6: Migration state */
    nxp_migration_init(&conn->migration);

    /* Phase 7: Congestion control (BBR) */
    conn->cc_ops = &nxp_cc_bbr;
    conn->cc_state = conn->cc_ops->create();
    nxp_pacer_init(&conn->pacer);
    nxp_dr_init(&conn->delivery_rate);

    /* Phase 8: Heartbeat (disabled by default, user can enable) */
    nxp_heartbeat_init(&conn->heartbeat, 0);

    /* Phase 8: Auto-reconnect (disabled by default) */
    conn->auto_reconnect = false;
    conn->max_reconnect_attempts = 3;

    return conn;
}

/* Destroy callback for hash map iteration */
static bool destroy_stream_cb(uint64_t key, void *value, void *user_data) {
    (void)key;
    (void)user_data;
    nxp_stream_destroy((nxp_stream_s *)value);
    return true;
}

void nxp_conn_destroy(nxp_conn *conn) {
    if (conn == nullptr) return;

    /* Destroy all streams */
    nxp_hash_map_foreach(conn->streams, destroy_stream_cb, nullptr);
    nxp_hash_map_destroy(conn->streams);

    /* ACK cleanup */
    nxp_ack_cleanup(&conn->ack);

    /* Handshake cleanup (Phase 5) */
    if (conn->handshake != nullptr) {
        nxp_handshake_destroy(conn->handshake);
        conn->handshake = nullptr;
    }

    /* Wipe crypto keys (secure: compiler cannot optimize away) */
    nxp_secure_zero(&conn->crypto, sizeof(conn->crypto));
    nxp_secure_zero(&conn->zero_rtt_crypto, sizeof(conn->zero_rtt_crypto));

    /* Phase 7: CC cleanup */
    if (conn->cc_ops != nullptr && conn->cc_state != nullptr) {
        conn->cc_ops->destroy(conn->cc_state);
        conn->cc_state = nullptr;
    }

    free(conn);
}

/* ── Force Established (Phase 4 testing) ──────────────── */

void nxp_conn_set_established(nxp_conn *conn, const nxp_conn_id *dcid) {
    conn->dcid  = *dcid;
    set_conn_state(conn, NXP_CONN_ESTABLISHED);
}

/* ── Process Incoming Datagram ────────────────────────── */

static nxp_result process_frames(nxp_conn *conn, const uint8_t *payload,
                                  size_t payload_len, uint64_t now_us) {
    size_t pos = 0;
    bool has_ack_eliciting = false;

    while (pos < payload_len) {
        nxp_frame frame;
        size_t consumed = nxp_frame_decode(payload + pos, payload_len - pos, &frame);
        if (consumed == 0) {
            NXP_LOG_ERROR_ONLY(NXP_ERR_INVALID_FRAME, "frame decode failed");
            return NXP_ERROR(NXP_ERR_INVALID_FRAME);
        }
        pos += consumed;

        if (nxp_frame_is_ack_eliciting(frame.type)) {
            has_ack_eliciting = true;
        }

        switch (frame.type) {
        case NXP_FRAME_PADDING:
            break;

        case NXP_FRAME_PING:
            /* Just triggers an ACK */
            break;

        case NXP_FRAME_ACK:
        case NXP_FRAME_ACK_ECN: {
            ack_ctx ac = { .conn = conn };
            nxp_ack_on_ack_recv(&conn->ack, &frame.ack, now_us,
                                on_packet_acked, on_packet_lost, &ac);
            /* Update stats */
            conn->stats.rtt_smoothed_us = conn->ack.smoothed_rtt;
            conn->stats.rtt_var_us      = conn->ack.rtt_var;
            if (conn->ack.min_rtt != UINT64_MAX) {
                conn->stats.rtt_min_us = conn->ack.min_rtt;
            }
            conn->stats.bytes_in_flight = conn->ack.bytes_in_flight;
            break;
        }

        case NXP_FRAME_STREAM: {
            /* Look up or create the stream */
            nxp_stream_s *s = (nxp_stream_s *)nxp_hash_map_get(
                conn->streams, frame.stream.stream_id);

            if (s == nullptr) {
                /* Remote-initiated stream - create it */
                if (stream_id_is_local(frame.stream.stream_id, conn->is_server)) {
                    /* We initiated it but it doesn't exist? Protocol error */
                    return NXP_ERROR(NXP_ERR_INVALID_FRAME);
                }
                s = nxp_stream_create(frame.stream.stream_id,
                                       NXP_STREAM_RELIABLE,
                                       conn->initial_max_stream_data,
                                       conn->initial_max_stream_data);
                if (s == nullptr) return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);
                (void)nxp_hash_map_put(conn->streams, frame.stream.stream_id, s);
                conn->stats.streams_opened++;
            }

            nxp_result r = nxp_stream_on_recv(s, &frame.stream);
            if (r.code != NXP_OK) return r;
            break;
        }

        case NXP_FRAME_MAX_DATA:
            nxp_flow_set_peer_max(&conn->conn_flow, frame.max_data.max_data);
            break;

        case NXP_FRAME_MAX_STREAM_DATA: {
            nxp_stream_s *s = (nxp_stream_s *)nxp_hash_map_get(
                conn->streams, frame.max_stream_data.stream_id);
            if (s != nullptr) {
                nxp_flow_set_peer_max(&s->flow, frame.max_stream_data.max_stream_data);
                /* Stream may be able to send more data now */
                if (nxp_stream_unsent(s) > 0) {
                    nxp_sched_add(conn, s);
                }
            }
            break;
        }

        case NXP_FRAME_MAX_STREAMS_BIDI:
            conn->peer_max_streams_bidi = (uint32_t)frame.max_streams.max_streams;
            break;

        case NXP_FRAME_MAX_STREAMS_UNI:
            conn->peer_max_streams_uni = (uint32_t)frame.max_streams.max_streams;
            break;

        case NXP_FRAME_CONNECTION_CLOSE:
        case NXP_FRAME_CONNECTION_CLOSE_APP:
            set_conn_state(conn, NXP_CONN_DRAINING);
            break;

        case NXP_FRAME_HANDSHAKE_DONE:
            /* Client receives HANDSHAKE_DONE from server in 1-RTT */
            if (conn->handshake != nullptr) {
                nxp_result hr = nxp_handshake_on_handshake_done(conn->handshake);
                if (hr.code == NXP_OK) {
                    set_conn_state(conn, NXP_CONN_ESTABLISHED);
                    if (conn->handshake->has_peer_params) {
                        const nxp_transport_params *tp = &conn->handshake->peer_params;
                        if (tp->initial_max_data > 0)
                            nxp_flow_set_peer_max(&conn->conn_flow, tp->initial_max_data);
                        if (tp->max_streams_bidi > 0)
                            conn->peer_max_streams_bidi = tp->max_streams_bidi;
                        if (tp->max_streams_uni > 0)
                            conn->peer_max_streams_uni = tp->max_streams_uni;
                    }
                }
            }
            break;

        /* Phase 6: Migration frames */
        case NXP_FRAME_PATH_CHALLENGE:
            nxp_migration_on_path_challenge(&conn->migration,
                                             frame.path_challenge.data);
            break;

        case NXP_FRAME_PATH_RESPONSE:
            if (nxp_migration_on_path_response(&conn->migration,
                                                frame.path_response.data)) {
                /* Path validated - update peer address */
                conn->peer_addr = conn->migration.current_path.addr;
                /* Rotate DCID if we have an unused CID */
                const nxp_cid_entry *new_cid =
                    nxp_migration_get_unused_cid(&conn->migration);
                if (new_cid != nullptr) {
                    conn->dcid = new_cid->cid;
                    nxp_migration_use_cid(&conn->migration, new_cid->seq_num);
                }
            }
            break;

        case NXP_FRAME_NEW_CONNECTION_ID:
            (void)nxp_migration_on_new_connection_id(
                &conn->migration,
                &frame.new_connection_id.cid,
                frame.new_connection_id.seq_num,
                frame.new_connection_id.retire_prior_to,
                frame.new_connection_id.stateless_reset_token);
            break;

        case NXP_FRAME_RETIRE_CONNECTION_ID:
            (void)nxp_migration_on_retire_connection_id(
                &conn->migration,
                frame.retire_connection_id.seq_num);
            break;

        case NXP_FRAME_NEW_TOKEN:
            /* Client: save session ticket from server */
            if (!conn->is_server && conn->handshake != nullptr) {
                if (frame.new_token.token_len <= sizeof(conn->handshake->session_ticket)) {
                    memcpy(conn->handshake->session_ticket,
                           frame.new_token.token,
                           (size_t)frame.new_token.token_len);
                    conn->handshake->session_ticket_len = (size_t)frame.new_token.token_len;
                    conn->handshake->has_session_ticket = true;
                }
            }
            break;

        /* Phase 8: Heartbeat frame */
        case NXP_FRAME_HEARTBEAT:
            nxp_heartbeat_on_recv(&conn->heartbeat,
                                   frame.heartbeat.timestamp_us, now_us);
            break;

        default:
            /* Unknown frame - skip (already consumed bytes) */
            break;
        }
    }

    (void)has_ack_eliciting;
    return NXP_SUCCESS;
}

/* ── Handshake Packet Handling (Phase 5) ─────────────── */

static nxp_result recv_handshake_packet(nxp_conn *conn, const uint8_t *data,
                                         size_t len, uint64_t now_us)
{
    if (conn->handshake == nullptr) {
        return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    }

    /* Make a mutable copy for header protection removal */
    uint8_t pkt_buf[NXP_PACKET_BUF_SIZE];
    if (len > sizeof(pkt_buf)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    memcpy(pkt_buf, data, len);

    /* Parse long header */
    nxp_pkt_long_header lhdr;
    nxp_result r = nxp_pkt_decode_long_header(pkt_buf, len, &lhdr);
    if (r.code != NXP_OK) return r;

    /* Server: learn client's CID from Initial packet SCID */
    if (conn->is_server && lhdr.type == NXP_PKT_INITIAL && conn->dcid.len == 0) {
        conn->dcid = lhdr.scid;
    }

    /* Determine crypto level and keys */
    nxp_crypto_level level;
    nxp_crypto_state *keys;

    if (lhdr.type == NXP_PKT_INITIAL) {
        level = NXP_CRYPTO_INITIAL;
        keys = &conn->handshake->initial_keys;
    } else if (lhdr.type == NXP_PKT_HANDSHAKE) {
        level = NXP_CRYPTO_HANDSHAKE;
        keys = &conn->handshake->handshake_keys;
    } else {
        NXP_LOG_ERROR_ONLY(NXP_ERR_INVALID_PACKET, "unknown packet type");
        return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    }

    if (!keys->available) {
        NXP_LOG_ERROR_ONLY(NXP_ERR_CRYPTO_FAIL, "crypto keys not available");
        return NXP_ERROR(NXP_ERR_CRYPTO_FAIL);
    }

    /* Remove header protection */
    uint8_t pn_len = nxp_hp_unprotect(keys->algo, keys->recv.hp_key,
                                       keys->recv.key_len,
                                       pkt_buf, len, lhdr.pkt_num_offset);
    if (pn_len == 0) {
        NXP_LOG_ERROR_ONLY(NXP_ERR_CRYPTO_FAIL, "header protection removal failed");
        return NXP_ERROR(NXP_ERR_CRYPTO_FAIL);
    }

    /* Re-read the packet number from the now-unprotected header */
    uint64_t trunc_pn = 0;
    for (uint8_t i = 0; i < pn_len; i++) {
        trunc_pn = (trunc_pn << 8) | pkt_buf[lhdr.pkt_num_offset + i];
    }
    uint64_t full_pn = nxp_pkt_decode_pkt_num(0, trunc_pn, pn_len);

    /* Decrypt payload */
    size_t hdr_len = lhdr.pkt_num_offset + pn_len;
    const uint8_t *ct = pkt_buf + hdr_len;
    size_t ct_len = len - hdr_len;

    uint8_t nonce[NXP_AEAD_IV_LEN];
    nxp_crypto_make_nonce(keys->recv.iv, full_pn, nonce);

    uint8_t pt_buf[NXP_PACKET_BUF_SIZE];
    ssize_t pt_len = nxp_aead_decrypt(keys->algo,
                                       keys->recv.key, keys->recv.key_len,
                                       nonce, pkt_buf, hdr_len,
                                       ct, ct_len, pt_buf);
    if (pt_len < 0) {
        NXP_TRACK_ERROR(NXP_ERR_CRYPTO_FAIL, "AEAD decrypt failed - possible attack");
        return NXP_ERROR(NXP_ERR_CRYPTO_FAIL);
    }

    /* Process frames - look for CRYPTO frames */
    size_t pos = 0;
    while (pos < (size_t)pt_len) {
        nxp_frame frame;
        size_t consumed = nxp_frame_decode(pt_buf + pos, (size_t)pt_len - pos, &frame);
        if (consumed == 0) {
            NXP_LOG_ERROR_ONLY(NXP_ERR_INVALID_FRAME, "handshake frame decode failed");
            return NXP_ERROR(NXP_ERR_INVALID_FRAME);
        }
        pos += consumed;

        if (frame.type == NXP_FRAME_CRYPTO) {
            r = nxp_handshake_recv_crypto(conn->handshake, level,
                                           frame.crypto.data, frame.crypto.length);
            if (r.code != NXP_OK) return r;

            /* Install app keys early if available (client needs them to
             * decrypt the server's HANDSHAKE_DONE in a 1-RTT packet) */
            if (conn->handshake->app_keys.available && !conn->crypto.available) {
                conn->crypto = conn->handshake->app_keys;
            }

            /* If handshake completed, transition to ESTABLISHED */
            if (nxp_handshake_is_complete(conn->handshake)) {
                set_conn_state(conn, NXP_CONN_ESTABLISHED);

                /* Apply peer transport params */
                if (conn->handshake->has_peer_params) {
                    const nxp_transport_params *tp = &conn->handshake->peer_params;
                    if (tp->initial_max_data > 0)
                        nxp_flow_set_peer_max(&conn->conn_flow, tp->initial_max_data);
                    if (tp->max_streams_bidi > 0)
                        conn->peer_max_streams_bidi = tp->max_streams_bidi;
                    if (tp->max_streams_uni > 0)
                        conn->peer_max_streams_uni = tp->max_streams_uni;
                }
            }
        } else if (frame.type == NXP_FRAME_HANDSHAKE_DONE) {
            r = nxp_handshake_on_handshake_done(conn->handshake);
            if (r.code != NXP_OK) return r;

            conn->crypto = conn->handshake->app_keys;
            set_conn_state(conn, NXP_CONN_ESTABLISHED);

            if (conn->handshake->has_peer_params) {
                const nxp_transport_params *tp = &conn->handshake->peer_params;
                if (tp->initial_max_data > 0)
                    nxp_flow_set_peer_max(&conn->conn_flow, tp->initial_max_data);
                if (tp->max_streams_bidi > 0)
                    conn->peer_max_streams_bidi = tp->max_streams_bidi;
                if (tp->max_streams_uni > 0)
                    conn->peer_max_streams_uni = tp->max_streams_uni;
            }
        }
        /* ACK frames in handshake packets are ignored for simplicity */
    }

    (void)now_us;
    (void)full_pn;
    return NXP_SUCCESS;
}

nxp_result nxp_conn_recv(nxp_conn *conn, const uint8_t *data,
                          size_t len, uint64_t now_us) {
    if (conn == nullptr || data == nullptr || len == 0) {
        NXP_LOG_ERROR_ONLY(NXP_ERR_INVALID_ARGUMENT, "conn_recv: invalid args");
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    if (conn->state == NXP_CONN_CLOSED || conn->state == NXP_CONN_DRAINING) {
        NXP_LOG_ERROR_ONLY(NXP_ERR_CONNECTION_CLOSED, "conn_recv: conn closed");
        return NXP_ERROR(NXP_ERR_CONNECTION_CLOSED);
    }

    NXP_FLIGHT_PACKET_RX(len, "peer");
    
    conn->last_activity_us = now_us;
    conn->stats.bytes_recv += len;
    conn->stats.packets_recv++;

    /* Check if long header (handshake packet) */
    if (nxp_pkt_is_long_header(data[0])) {
        return recv_handshake_packet(conn, data, len, now_us);
    }

    /* Short header (1-RTT) processing */
    uint8_t pkt_buf[NXP_PACKET_BUF_SIZE];
    if (len > sizeof(pkt_buf)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    memcpy(pkt_buf, data, len);

    /* Remove header protection if crypto is active */
    uint8_t actual_pn_len = 0;
    if (conn->crypto.available) {
        actual_pn_len = nxp_hp_unprotect(conn->crypto.algo,
                                          conn->crypto.recv.hp_key,
                                          conn->crypto.recv.key_len,
                                          pkt_buf, len,
                                          (size_t)1 + conn->scid.len);
        if (actual_pn_len == 0) return NXP_ERROR(NXP_ERR_CRYPTO_FAIL);
    }

    /* Decode short header (from unprotected buffer) */
    nxp_pkt_short_header shdr;
    nxp_result r = nxp_pkt_decode_short_header(pkt_buf, len, conn->scid.len, &shdr);
    if (r.code != NXP_OK) return NXP_ERROR(NXP_ERR_INVALID_PACKET);

    /* Reconstruct full packet number */
    uint64_t full_pn = nxp_pkt_decode_pkt_num(
        conn->ack.largest_recv_pn, shdr.pkt_num, shdr.pkt_num_len);

    /* Record received packet for ACK generation */
    nxp_ack_on_pkt_recv(&conn->ack, full_pn, now_us, true);

    const uint8_t *payload;
    size_t payload_len;
    static _Thread_local uint8_t decrypt_buf[NXP_PACKET_BUF_SIZE];
    bool did_decrypt = false;

    if (conn->crypto.available) {
        /* Decrypt payload with AEAD */
        size_t hdr_len = shdr.header_len;
        const uint8_t *ct = pkt_buf + hdr_len;
        size_t ct_len = len - hdr_len;

        uint8_t nonce[NXP_AEAD_IV_LEN];
        nxp_crypto_make_nonce(conn->crypto.recv.iv, full_pn, nonce);

        ssize_t pt_len = nxp_aead_decrypt(conn->crypto.algo,
                                           conn->crypto.recv.key,
                                           conn->crypto.recv.key_len,
                                           nonce,
                                           pkt_buf, hdr_len,
                                           ct, ct_len, decrypt_buf);
        if (pt_len < 0) return NXP_ERROR(NXP_ERR_CRYPTO_FAIL);

        payload = decrypt_buf;
        payload_len = (size_t)pt_len;
        did_decrypt = true;
    } else {
        /* Plaintext mode (Phase 4 compatibility) */
        payload = pkt_buf + shdr.header_len;
        payload_len = len - shdr.header_len;
    }

    nxp_result res = process_frames(conn, payload, payload_len, now_us);

    /* Phase 10: Securely wipe decrypted plaintext from thread-local buffer */
    if (did_decrypt) {
        nxp_secure_zero(decrypt_buf, payload_len);
    }

    return res;
}

/* ── Generate Handshake Packet (Phase 5) ─────────────── */

static ssize_t send_handshake_packet(nxp_conn *conn, uint8_t *out,
                                      size_t max_len, uint64_t now_us)
{
    nxp_handshake *hs = conn->handshake;
    if (hs == nullptr || !nxp_handshake_has_data(hs)) return 0;

    nxp_crypto_level level = nxp_handshake_send_level(hs);

    /* For HANDSHAKE_DONE: it goes in a 1-RTT short-header packet */
    if (hs->send_handshake_done && level == NXP_CRYPTO_APPLICATION) {
        if (!hs->app_keys.available) return 0;

        /* Build a 1-RTT packet with just HANDSHAKE_DONE frame + padding */
        uint8_t pn_len = 1;
        size_t hdr_size = (size_t)1 + conn->dcid.len + pn_len;
        size_t needed = hdr_size + 4 + NXP_AEAD_TAG_LEN; /* frame + padding + tag */
        if (max_len < needed) return 0;

        uint8_t frame_buf[8];
        size_t frame_len = nxp_frame_encode_handshake_done(frame_buf, sizeof(frame_buf));
        if (frame_len == 0) return 0;

        /* Pad with PADDING frames to ensure packet is large enough for HP sample.
         * HP needs 16-byte sample at pn_offset + 4. Minimum plaintext:
         * ct_len >= 20 - pn_len → frame_len >= 4 - pn_len */
        size_t min_frame_len = (size_t)(4 - pn_len) + 1;
        while (frame_len < min_frame_len) {
            frame_buf[frame_len++] = 0x00; /* PADDING */
        }

        nxp_pkt_short_header shdr = {
            .dcid = conn->dcid,
            .pkt_num = conn->next_pkt_num,
            .pkt_num_len = pn_len,
        };
        size_t hdr_written = nxp_pkt_encode_short_header(&shdr, conn->dcid.len,
                                                          out, max_len);
        if (hdr_written == 0) return -1;

        /* Encrypt */
        uint8_t nonce[NXP_AEAD_IV_LEN];
        nxp_crypto_make_nonce(hs->app_keys.send.iv, conn->next_pkt_num, nonce);

        ssize_t ct_len = nxp_aead_encrypt(hs->app_keys.algo,
                                           hs->app_keys.send.key,
                                           hs->app_keys.send.key_len,
                                           nonce, out, hdr_written,
                                           frame_buf, frame_len,
                                           out + hdr_written);
        if (ct_len < 0) return -1;
        size_t total = hdr_written + (size_t)ct_len;

        /* Apply header protection (pn_offset = hdr_written - pn_len) */
        (void)nxp_hp_protect(hs->app_keys.algo, hs->app_keys.send.hp_key,
                             hs->app_keys.send.key_len,
                             out, total, hdr_written - pn_len, pn_len);

        conn->next_pkt_num++;
        hs->send_handshake_done = false;
        (void)now_us;
        return (ssize_t)total;
    }

    /* CRYPTO frame data in Initial or Handshake long-header packet */
    nxp_crypto_state *keys;
    nxp_packet_type pkt_type;
    uint64_t *pn_counter;

    if (level == NXP_CRYPTO_INITIAL) {
        keys = &hs->initial_keys;
        pkt_type = NXP_PKT_INITIAL;
        pn_counter = &hs->initial_pn;
    } else {
        keys = &hs->handshake_keys;
        pkt_type = NXP_PKT_HANDSHAKE;
        pn_counter = &hs->handshake_pn;
    }

    if (!keys->available) return 0;

    /* Fill CRYPTO frame data */
    uint8_t crypto_data[512];
    uint64_t crypto_offset = 0;
    size_t crypto_len = nxp_handshake_fill_crypto(hs, crypto_data,
                                                    sizeof(crypto_data),
                                                    &crypto_offset);
    if (crypto_len == 0) return 0;

    /* Encode frames into a temp buffer */
    uint8_t frame_buf[1024];
    nxp_frame_crypto cf = {
        .offset = crypto_offset,
        .length = crypto_len,
        .data = crypto_data,
    };
    size_t frame_len = nxp_frame_encode_crypto(&cf, frame_buf, sizeof(frame_buf));
    if (frame_len == 0) return 0;

    /* Build long header */
    uint8_t pn_len = 1;
    nxp_pkt_long_header lhdr = {
        .type = pkt_type,
        .version = NXP_VERSION_CURRENT,
        .dcid = conn->dcid,
        .scid = conn->scid,
        .token = nullptr,
        .token_len = 0,
        .payload_len = frame_len + NXP_AEAD_TAG_LEN + pn_len,
        .pkt_num = *pn_counter,
        .pkt_num_len = pn_len,
    };

    size_t hdr_written = nxp_pkt_encode_long_header(&lhdr, out, max_len);
    if (hdr_written == 0) return 0;

    /* Encrypt frames */
    uint8_t nonce[NXP_AEAD_IV_LEN];
    nxp_crypto_make_nonce(keys->send.iv, *pn_counter, nonce);

    ssize_t ct_len = nxp_aead_encrypt(keys->algo,
                                       keys->send.key, keys->send.key_len,
                                       nonce, out, hdr_written,
                                       frame_buf, frame_len,
                                       out + hdr_written);
    if (ct_len < 0) return -1;
    size_t total = hdr_written + (size_t)ct_len;

    /* Apply header protection (pn_offset = hdr_written - pn_len) */
    (void)nxp_hp_protect(keys->algo, keys->send.hp_key, keys->send.key_len,
                         out, total, hdr_written - pn_len, pn_len);

    (*pn_counter)++;
    (void)now_us;
    return (ssize_t)total;
}

/* ── Generate Outgoing Datagram ───────────────────────── */

ssize_t nxp_conn_send(nxp_conn *conn, uint8_t *out, size_t max_len,
                       uint64_t now_us) {
    if (conn == nullptr || out == nullptr) return -1;

    /* If handshake is active, generate handshake packets first */
    if (conn->handshake != nullptr && nxp_handshake_has_data(conn->handshake)) {
        return send_handshake_packet(conn, out, max_len, now_us);
    }

    if (conn->state != NXP_CONN_ESTABLISHED &&
        conn->state != NXP_CONN_CLOSING) {
        return 0;
    }

    conn->last_activity_us = now_us;

    /* Phase 7: Pacing check - update tokens and see if we can send */
    nxp_pacer_update(&conn->pacer, now_us);
    if (!nxp_pacer_can_send(&conn->pacer, NXP_BBR_MAX_DATAGRAM_SIZE)) {
        /* Paced out - nothing to send right now.
         * Only gate data packets, not control frames (ACKs etc.) */
        bool has_control_frames = conn->ack.ack_needed ||
                                   conn->send_conn_close ||
                                   conn->send_ping ||
                                   nxp_flow_should_update(&conn->conn_flow) ||
                                   nxp_migration_has_pending(&conn->migration) ||
                                   nxp_heartbeat_has_pending(&conn->heartbeat);
        if (!has_control_frames) return 0;
    }

    /* Build the packet: short header + frames */

    /* Reserve space for the short header (we'll fill it in later) */
    uint8_t pn_len = nxp_pkt_num_len(conn->next_pkt_num, conn->ack.largest_recv_pn);
    size_t hdr_size = (size_t)1 + conn->dcid.len + pn_len;

    /* If crypto active, reserve space for AEAD tag */
    size_t tag_size = conn->crypto.available ? NXP_AEAD_TAG_LEN : 0;

    if (max_len < hdr_size + 1 + tag_size) return 0;

    /* Build frames into a temp buffer (will encrypt into `out` later) */
    uint8_t frame_buf[NXP_PACKET_BUF_SIZE];
    size_t frame_space = max_len - hdr_size - tag_size;
    if (frame_space > sizeof(frame_buf)) frame_space = sizeof(frame_buf);
    size_t frame_pos = 0;

    bool has_ack_eliciting = false;
    nxp_sent_pkt sent_meta;
    memset(&sent_meta, 0, sizeof(sent_meta));

    /* 1. ACK frame (if needed) */
    if (conn->ack.ack_needed || conn->ack.recv_range_count > 0) {
        nxp_frame_ack ack_frame;
        if (nxp_ack_build_frame(&conn->ack, &ack_frame, now_us)) {
            size_t n = nxp_frame_encode_ack(&ack_frame, frame_buf + frame_pos,
                                             frame_space - frame_pos);
            if (n > 0) {
                frame_pos += n;
                conn->ack.ack_needed = false;
            }
        }
    }

    /* 2. CONNECTION_CLOSE frame */
    if (conn->send_conn_close) {
        nxp_frame_connection_close close_frame = {
            .error_code = conn->close_error_code,
            .frame_type = 0,
            .reason_len = 0,
            .reason = nullptr,
            .is_app = true,
        };
        size_t n = nxp_frame_encode_connection_close(&close_frame,
                     frame_buf + frame_pos, frame_space - frame_pos);
        if (n > 0) {
            frame_pos += n;
            has_ack_eliciting = true;
            conn->send_conn_close = false;
            set_conn_state(conn, NXP_CONN_CLOSING);
        }
    }

    /* 3. PING frame (if requested) */
    if (conn->send_ping) {
        size_t n = nxp_frame_encode_ping(frame_buf + frame_pos,
                                          frame_space - frame_pos);
        if (n > 0) {
            frame_pos += n;
            has_ack_eliciting = true;
            conn->send_ping = false;
        }
    }

    /* 4. MAX_DATA frame (connection-level flow control update) */
    if (nxp_flow_should_update(&conn->conn_flow)) {
        uint64_t new_max = nxp_flow_get_update(&conn->conn_flow);
        size_t n = nxp_frame_encode_max_data(new_max,
                     frame_buf + frame_pos, frame_space - frame_pos);
        if (n > 0) {
            frame_pos += n;
            has_ack_eliciting = true;
        }
    }

    /* 5. Migration frames (Phase 6) */
    if (conn->migration.send_path_response) {
        size_t n = nxp_frame_encode_path_response(
            conn->migration.pending_response_data,
            frame_buf + frame_pos, frame_space - frame_pos);
        if (n > 0) {
            frame_pos += n;
            has_ack_eliciting = true;
            conn->migration.send_path_response = false;
        }
    }
    if (conn->migration.send_path_challenge) {
        size_t n = nxp_frame_encode_path_challenge(
            conn->migration.new_path.challenge_data,
            frame_buf + frame_pos, frame_space - frame_pos);
        if (n > 0) {
            frame_pos += n;
            has_ack_eliciting = true;
            conn->migration.send_path_challenge = false;
        }
    }
    while (conn->migration.pending_new_cid_count > 0) {
        uint32_t idx = conn->migration.pending_new_cid_count - 1;
        nxp_cid_entry *e = &conn->migration.pending_new_cids[idx];
        nxp_frame_new_connection_id ncid = {
            .seq_num = e->seq_num,
            .retire_prior_to = 0,
            .cid = e->cid,
        };
        memcpy(ncid.stateless_reset_token, e->stateless_reset_token, 16);
        size_t n = nxp_frame_encode_new_connection_id(
            &ncid, frame_buf + frame_pos, frame_space - frame_pos);
        if (n == 0) break;
        frame_pos += n;
        has_ack_eliciting = true;
        conn->migration.pending_new_cid_count--;
    }
    if (conn->migration.send_retire_cid) {
        size_t n = nxp_frame_encode_retire_connection_id(
            conn->migration.retire_seq,
            frame_buf + frame_pos, frame_space - frame_pos);
        if (n > 0) {
            frame_pos += n;
            has_ack_eliciting = true;
            conn->migration.send_retire_cid = false;
        }
    }

    /* 6. Heartbeat frames (Phase 8) */
    if (conn->heartbeat.send_echo) {
        size_t n = nxp_frame_encode_heartbeat(conn->heartbeat.echo_timestamp,
                     frame_buf + frame_pos, frame_space - frame_pos);
        if (n > 0) {
            frame_pos += n;
            has_ack_eliciting = true;
            conn->heartbeat.send_echo = false;
        }
    }
    if (conn->heartbeat.send_heartbeat) {
        size_t n = nxp_frame_encode_heartbeat(conn->heartbeat.pending_timestamp,
                     frame_buf + frame_pos, frame_space - frame_pos);
        if (n > 0) {
            frame_pos += n;
            has_ack_eliciting = true;
            conn->heartbeat.send_heartbeat = false;
        }
    }

    /* 7. STREAM frames from the scheduler (CC-gated) */
    /* Phase 7: Check cwnd before sending data */
    bool cc_allows = true;
    if (conn->cc_ops != nullptr && conn->cc_state != nullptr) {
        uint64_t cwnd = conn->cc_ops->get_cwnd(conn->cc_state);
        if (conn->ack.bytes_in_flight >= cwnd) {
            cc_allows = false; /* Cwnd exhausted */
        }
    }

    while (cc_allows && frame_pos < frame_space &&
           sent_meta.frame_count < NXP_MAX_STREAM_FRAMES_PER_PKT) {
        nxp_stream_s *s = nxp_sched_next(conn);
        if (s == nullptr) break;

        /* Check connection-level flow control */
        uint64_t unsent = nxp_stream_unsent(s);
        if (unsent > 0 && !nxp_flow_can_send(&conn->conn_flow, 1)) {
            break;
        }

        nxp_frame_stream sf;
        size_t avail = frame_space - frame_pos;
        /* Reserve ~10 bytes for STREAM frame overhead (type + id + offset + len) */
        size_t max_data = (avail > 20) ? avail - 20 : 0;

        /* Also limit by connection flow control */
        uint64_t fc_remain = 0;
        if (conn->conn_flow.peer_max_data > conn->conn_flow.data_sent) {
            fc_remain = conn->conn_flow.peer_max_data - conn->conn_flow.data_sent;
        }
        if ((uint64_t)max_data > fc_remain) max_data = (size_t)fc_remain;

        /* Phase 8: Per-stream rate limiting */
        if (s->rate_limit.enabled) {
            nxp_stream_rate_update(&s->rate_limit, now_us);
            uint64_t budget = nxp_stream_rate_budget(&s->rate_limit);
            if (budget == 0) {
                s->blocked = true;
                continue;
            }
            if ((uint64_t)max_data > budget) max_data = (size_t)budget;
        }

        if (!nxp_stream_fill_frame(s, &sf, max_data)) {
            continue;
        }

        size_t n = nxp_frame_encode_stream(&sf, frame_buf + frame_pos,
                                            frame_space - frame_pos);
        if (n == 0) {
            /* Couldn't encode - rewind the stream */
            nxp_stream_on_loss(s, sf.offset, (uint32_t)sf.length, sf.fin);
            break;
        }

        frame_pos += n;
        has_ack_eliciting = true;

        /* Track for flow control */
        nxp_flow_on_send(&conn->conn_flow, sf.length);
        nxp_flow_on_send(&s->flow, sf.length);

        /* Phase 8: Track per-stream rate limit */
        if (s->rate_limit.enabled) {
            nxp_stream_rate_on_send(&s->rate_limit, (uint32_t)sf.length);
        }

        /* Record stream frame in sent packet metadata */
        nxp_stream_frame_record *rec = &sent_meta.frames[sent_meta.frame_count];
        rec->stream_id = sf.stream_id;
        rec->offset    = sf.offset;
        rec->len       = (uint32_t)sf.length;
        rec->fin       = sf.fin;
        sent_meta.frame_count++;

        /* Re-add stream to scheduler if it still has data */
        if (nxp_stream_unsent(s) > 0 || (s->send.fin && !s->send.fin_sent)) {
            nxp_sched_add(conn, s);
        }
    }

    /* If we have nothing to send, return 0 */
    if (frame_pos == 0) return 0;

    /* Encode the short header */
    nxp_pkt_short_header shdr = {
        .dcid     = conn->dcid,
        .spin_bit = false,
        .key_phase = false,
        .pkt_num  = conn->next_pkt_num,
        .pkt_num_len = pn_len,
    };

    size_t hdr_written = nxp_pkt_encode_short_header(&shdr, conn->dcid.len,
                                                      out, max_len);
    if (hdr_written == 0) return -1;

    size_t total;

    if (conn->crypto.available) {
        /* Encrypt frames into the packet buffer after the header */
        uint8_t nonce[NXP_AEAD_IV_LEN];
        nxp_crypto_make_nonce(conn->crypto.send.iv, conn->next_pkt_num, nonce);

        ssize_t ct_len = nxp_aead_encrypt(conn->crypto.algo,
                                           conn->crypto.send.key,
                                           conn->crypto.send.key_len,
                                           nonce,
                                           out, hdr_written,
                                           frame_buf, frame_pos,
                                           out + hdr_written);
        if (ct_len < 0) return -1;
        total = hdr_written + (size_t)ct_len;

        /* Apply header protection (pn_offset = hdr_written - pn_len) */
        (void)nxp_hp_protect(conn->crypto.algo, conn->crypto.send.hp_key,
                             conn->crypto.send.key_len,
                             out, total, hdr_written - pn_len, pn_len);
    } else {
        /* Plaintext mode (Phase 4 compatibility) */
        memcpy(out + hdr_written, frame_buf, frame_pos);
        total = hdr_written + frame_pos;
    }

    /* Record sent packet in ACK tracker */
    sent_meta.pkt_num       = conn->next_pkt_num;
    sent_meta.sent_time     = now_us;
    sent_meta.sent_bytes    = (uint32_t)total;
    sent_meta.ack_eliciting = has_ack_eliciting;
    sent_meta.in_flight     = has_ack_eliciting;
    sent_meta.declared_lost = false;

    /* Phase 7: Take delivery rate snapshot for this packet */
    nxp_dr_on_packet_sent(&conn->delivery_rate, (uint32_t)total,
                           now_us, &sent_meta.dr_state);

    nxp_ack_on_pkt_sent(&conn->ack, &sent_meta);

    /* Phase 7: Notify CC of sent packet + update pacer */
    if (conn->cc_ops != nullptr && conn->cc_state != nullptr) {
        conn->cc_ops->on_sent(conn->cc_state, (uint32_t)total,
                               now_us, conn->ack.bytes_in_flight);
        nxp_pacer_on_send(&conn->pacer, (uint32_t)total);

        /* Mark app-limited if the scheduler has nothing left */
        if (nxp_sched_next(conn) == nullptr) {
            nxp_dr_set_app_limited(&conn->delivery_rate);
        }
    }

    /* Update stats */
    conn->stats.bytes_sent += total;
    conn->next_pkt_num++;

    NXP_FLIGHT_PACKET_TX(total, "peer");
    
    return (ssize_t)total;
}

/* ── Timeout ──────────────────────────────────────────── */

uint64_t nxp_conn_timeout(const nxp_conn *conn, uint64_t now_us) {
    if (conn->state == NXP_CONN_CLOSED) return UINT64_MAX;

    uint64_t timeout = UINT64_MAX;

    /* Loss detection timer */
    uint64_t loss_t = nxp_ack_loss_timeout(&conn->ack);
    if (loss_t < timeout) timeout = loss_t;

    /* Idle timeout */
    uint64_t idle_deadline = conn->last_activity_us + conn->idle_timeout_us;
    if (idle_deadline < timeout) timeout = idle_deadline;

    /* ACK delay timer */
    if (conn->ack.ack_needed && conn->ack.ack_delay_timer < timeout) {
        timeout = conn->ack.ack_delay_timer;
    }

    /* Phase 7: Pacing timer */
    if (conn->pacer.enabled) {
        uint64_t pacing_delay = nxp_pacer_next_send_time(&conn->pacer,
                                    NXP_BBR_MAX_DATAGRAM_SIZE);
        if (pacing_delay > 0) {
            uint64_t pace_deadline = now_us + pacing_delay;
            if (pace_deadline < timeout) timeout = pace_deadline;
        }
    }

    /* Phase 8: Heartbeat timer */
    uint64_t hb_t = nxp_heartbeat_next_timeout(&conn->heartbeat);
    if (hb_t < timeout) timeout = hb_t;

    return timeout;
}

void nxp_conn_on_timeout(nxp_conn *conn, uint64_t now_us) {
    /* Idle timeout check */
    if (now_us >= conn->last_activity_us + conn->idle_timeout_us) {
        set_conn_state(conn, NXP_CONN_CLOSED);
        return;
    }

    /* Loss detection timer */
    uint64_t loss_t = nxp_ack_loss_timeout(&conn->ack);
    if (loss_t != UINT64_MAX && now_us >= loss_t) {
        ack_ctx ac = { .conn = conn };
        nxp_ack_on_loss_timeout(&conn->ack, now_us, on_packet_lost, &ac);

        /* If PTO fired, send a probe */
        if (conn->ack.pto_count > 0) {
            conn->send_ping = true;
        }
    }

    /* Phase 8: Heartbeat timer check */
    nxp_heartbeat_check(&conn->heartbeat, now_us);
    if (nxp_heartbeat_is_dead(&conn->heartbeat)) {
        /* Connection is dead - close it */
        set_conn_state(conn, NXP_CONN_CLOSED);
    }
}

/* ── Stream Operations ────────────────────────────────── */

nxp_result nxp_conn_open_stream(nxp_conn *conn, uint64_t *stream_id_out,
                                 nxp_stream_type type, bool unidirectional) {
    /* Check stream limits */
    if (unidirectional) {
        if (conn->open_uni_count >= conn->peer_max_streams_uni) {
            return NXP_ERROR(NXP_ERR_STREAM_LIMIT);
        }
    } else {
        if (conn->open_bidi_count >= conn->peer_max_streams_bidi) {
            return NXP_ERROR(NXP_ERR_STREAM_LIMIT);
        }
    }

    uint64_t seq = unidirectional ? conn->next_uni_id : conn->next_bidi_id;
    uint64_t id = make_stream_id(seq, type, unidirectional, conn->is_server);

    nxp_stream_s *s = nxp_stream_create(id, type,
                                          conn->initial_max_stream_data,
                                          conn->initial_max_stream_data);
    if (s == nullptr) return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);

    (void)nxp_hash_map_put(conn->streams, id, s);

    if (unidirectional) {
        conn->next_uni_id++;
        conn->open_uni_count++;
    } else {
        conn->next_bidi_id++;
        conn->open_bidi_count++;
    }

    conn->stats.streams_opened++;
    *stream_id_out = id;
    return NXP_SUCCESS;
}

ssize_t nxp_conn_stream_send(nxp_conn *conn, uint64_t stream_id,
                              const uint8_t *data, size_t len, bool fin) {
    if (conn->state != NXP_CONN_ESTABLISHED) return -1;

    nxp_stream_s *s = (nxp_stream_s *)nxp_hash_map_get(conn->streams, stream_id);
    if (s == nullptr) return -1;

    ssize_t written = nxp_stream_write(s, data, len, fin);
    if (written > 0) {
        nxp_sched_add(conn, s);
    }

    return written;
}

ssize_t nxp_conn_stream_recv(nxp_conn *conn, uint64_t stream_id,
                              uint8_t *buf, size_t buf_len, bool *fin) {
    nxp_stream_s *s = (nxp_stream_s *)nxp_hash_map_get(conn->streams, stream_id);
    if (s == nullptr) {
        *fin = false;
        return -1;
    }

    return nxp_stream_read(s, buf, buf_len, fin);
}

/* ── State Query ──────────────────────────────────────── */

nxp_conn_state nxp_conn_get_state(const nxp_conn *conn) {
    return conn->state;
}

/* ── Close ────────────────────────────────────────────── */

nxp_result nxp_conn_initiate_close(nxp_conn *conn, uint64_t error_code) {
    if (conn->state == NXP_CONN_CLOSED ||
        conn->state == NXP_CONN_DRAINING) {
        return NXP_ERROR(NXP_ERR_CONNECTION_CLOSED);
    }

    conn->send_conn_close  = true;
    conn->close_error_code = error_code;
    return NXP_SUCCESS;
}

/* ── Handshake (Phase 5) ─────────────────────────────── */

nxp_result nxp_conn_start_handshake(nxp_conn *conn,
                                     const nxp_conn_id *peer_cid) {
    if (conn->state != NXP_CONN_IDLE) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    nxp_handshake *hs = nxp_handshake_create(conn->is_server);
    if (hs == nullptr) return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);

    /* Set local transport parameters from connection config */
    nxp_transport_params tp = {
        .initial_max_data = conn->conn_flow.local_max_data,
        .initial_max_stream_data = conn->initial_max_stream_data,
        .max_streams_bidi = conn->local_max_streams_bidi,
        .max_streams_uni = conn->local_max_streams_uni,
        .idle_timeout_us = conn->idle_timeout_us,
    };
    nxp_handshake_set_local_params(hs, &tp);

    nxp_result r;
    if (conn->is_server) {
        /* Server: peer_cid is our SCID (which client uses as DCID) */
        r = nxp_handshake_start_server(hs, peer_cid);
        set_conn_state(conn, NXP_CONN_HANDSHAKE_INITIAL);
    } else {
        /* Client: peer_cid is the server's DCID */
        conn->dcid = *peer_cid;
        r = nxp_handshake_start_client(hs, peer_cid);
        set_conn_state(conn, NXP_CONN_HANDSHAKE_INITIAL);
    }

    if (r.code != NXP_OK) {
        nxp_handshake_destroy(hs);
        set_conn_state(conn, NXP_CONN_IDLE);
        return r;
    }

    conn->handshake = hs;
    return NXP_SUCCESS;
}

/* ── Phase 8: Built-in Features ──────────────────────── */

void nxp_conn_set_heartbeat(nxp_conn *conn, uint64_t interval_us) {
    nxp_heartbeat_init(&conn->heartbeat, interval_us);
}

void nxp_conn_set_auto_reconnect(nxp_conn *conn, bool enable,
                                  uint32_t max_attempts) {
    conn->auto_reconnect = enable;
    conn->max_reconnect_attempts = max_attempts;
}

nxp_result nxp_conn_set_stream_rate(nxp_conn *conn, uint64_t stream_id,
                                     uint64_t rate_bps) {
    nxp_stream_s *s = (nxp_stream_s *)nxp_hash_map_get(conn->streams, stream_id);
    if (s == nullptr) return NXP_ERROR(NXP_ERR_STREAM_CLOSED);
    nxp_stream_rate_set(&s->rate_limit, rate_bps);
    return NXP_SUCCESS;
}

nxp_result nxp_conn_set_on_writable(nxp_conn *conn, uint64_t stream_id,
                                     void (*cb)(void *user_data, uint64_t stream_id),
                                     void *user_data) {
    nxp_stream_s *s = (nxp_stream_s *)nxp_hash_map_get(conn->streams, stream_id);
    if (s == nullptr) return NXP_ERROR(NXP_ERR_STREAM_CLOSED);
    s->on_writable = cb;
    s->writable_user_data = user_data;
    return NXP_SUCCESS;
}
