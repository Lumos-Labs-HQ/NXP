/*
 * NXP Server Listener - Implementation
 *
 * Phase 9: Multi-connection server with CID-based routing.
 *
 * Routing strategy:
 *   - Long header packets (Initial/Handshake): extract DCID from header
 *   - Short header packets (1-RTT): extract DCID from first N bytes
 *   - CID is hashed to a uint64_t key for hash map lookup
 *
 * New connection flow:
 *   1. Receive Initial packet with unknown DCID
 *   2. If require_retry: send Retry with token, wait for re-Initial
 *   3. Create nxp_conn in server mode with generated SCID
 *   4. Register SCID in conn_map for future routing
 *   5. Call on_new_conn callback
 */
#include "listener_internal.h"
#include "packet_internal.h"
#include "util/random.h"

#include <stdlib.h>
#include <string.h>

/* ── CID Hashing ───────────────────────────────────────── */

/*
 * FNV-1a hash of CID bytes → uint64_t key for hash map.
 */
static uint64_t cid_hash(const nxp_conn_id *cid) {
    uint64_t h = 14695981039346656037ULL; /* FNV offset basis */
    for (uint8_t i = 0; i < cid->len; i++) {
        h ^= cid->data[i];
        h *= 1099511628211ULL; /* FNV prime */
    }
    return h;
}

/* ── Create / Destroy ─────────────────────────────────── */

nxp_listener_s *nxp_listener_create(const nxp_listener_config *config) {
    nxp_listener_s *ls = (nxp_listener_s *)calloc(1, sizeof(*ls));
    if (ls == nullptr) return nullptr;

    ls->config = *config;

    /* Defaults */
    if (ls->config.max_connections == 0) {
        ls->config.max_connections = NXP_LISTENER_DEFAULT_MAX_CONNS;
    }

    /* CID → connection hash map */
    ls->conn_map = nxp_hash_map_create(64);
    if (ls->conn_map == nullptr) {
        free(ls);
        return nullptr;
    }

    /* Connection array (growable) */
    ls->conn_cap = 64;
    ls->conns = (nxp_conn **)calloc(ls->conn_cap, sizeof(nxp_conn *));
    if (ls->conns == nullptr) {
        nxp_hash_map_destroy(ls->conn_map);
        free(ls);
        return nullptr;
    }

    return ls;
}

void nxp_listener_destroy(nxp_listener_s *ls) {
    if (ls == nullptr) return;

    /* Destroy all connections */
    for (uint32_t i = 0; i < ls->conn_count; i++) {
        if (ls->conns[i] != nullptr) {
            nxp_conn_destroy(ls->conns[i]);
        }
    }
    free(ls->conns);
    nxp_hash_map_destroy(ls->conn_map);
    free(ls);
}

/* ── CID Generation ───────────────────────────────────── */

void nxp_listener_generate_cid(const nxp_listener_s *ls,
                                nxp_conn_id *cid_out) {
    memset(cid_out, 0, sizeof(*cid_out));
    cid_out->len = NXP_LISTENER_CID_LEN;

    /* Embed node ID prefix if configured */
    uint8_t prefix_len = ls->config.node_id_len;
    if (prefix_len > NXP_LISTENER_MAX_NODE_ID_LEN) {
        prefix_len = NXP_LISTENER_MAX_NODE_ID_LEN;
    }
    if (prefix_len > 0) {
        memcpy(cid_out->data, ls->config.node_id, prefix_len);
    }

    /* Fill remaining bytes with random data */
    uint8_t random_len = NXP_LISTENER_CID_LEN - prefix_len;
    if (random_len > 0) {
        (void)nxp_random_bytes(cid_out->data + prefix_len, random_len);
    }
}

bool nxp_listener_extract_node_id(const nxp_conn_id *cid,
                                   uint8_t node_id_len,
                                   uint8_t *node_id_out) {
    if (cid->len < node_id_len || node_id_len == 0) return false;
    memcpy(node_id_out, cid->data, node_id_len);
    return true;
}

/* ── Connection Management ────────────────────────────── */

/* Add a connection to the listener */
static nxp_result listener_add_conn(nxp_listener_s *ls, nxp_conn *conn) {
    if (ls->conn_count >= ls->config.max_connections) {
        return NXP_ERROR(NXP_ERR_STREAM_LIMIT);
    }

    /* Grow array if needed */
    if (ls->conn_count >= ls->conn_cap) {
        uint32_t new_cap = ls->conn_cap * 2;
        nxp_conn **new_arr = (nxp_conn **)realloc(
            ls->conns, (size_t)new_cap * sizeof(nxp_conn *));
        if (new_arr == nullptr) return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);
        ls->conns = new_arr;
        ls->conn_cap = new_cap;
    }

    /* Register the connection's SCID in the map */
    uint64_t key = cid_hash(&conn->scid);
    (void)nxp_hash_map_put(ls->conn_map, key, conn);

    /* Add to array */
    ls->conns[ls->conn_count] = conn;
    ls->conn_count++;

    return NXP_SUCCESS;
}

nxp_conn *nxp_listener_find_conn(const nxp_listener_s *ls,
                                  const nxp_conn_id *dcid) {
    uint64_t key = cid_hash(dcid);
    return (nxp_conn *)nxp_hash_map_get(ls->conn_map, key);
}

void nxp_listener_remove_conn(nxp_listener_s *ls, nxp_conn *conn) {
    /* Remove from hash map */
    uint64_t key = cid_hash(&conn->scid);
    (void)nxp_hash_map_remove(ls->conn_map, key);

    /* Remove from array (swap with last) */
    for (uint32_t i = 0; i < ls->conn_count; i++) {
        if (ls->conns[i] == conn) {
            ls->conns[i] = ls->conns[ls->conn_count - 1];
            ls->conns[ls->conn_count - 1] = nullptr;
            ls->conn_count--;
            break;
        }
    }

    /* Destroy the connection */
    nxp_conn_destroy(conn);
}

/* ── Accept New Connection ────────────────────────────── */

static nxp_result listener_accept(nxp_listener_s *ls,
                                   const nxp_pkt_long_header *hdr,
                                   const uint8_t *data, size_t len,
                                   const nxp_addr *from_addr,
                                   uint64_t now_us) {
    /* Build connection config from listener defaults */
    nxp_conn_config config;
    memset(&config, 0, sizeof(config));

    /* Generate a server CID with node ID prefix */
    nxp_listener_generate_cid(ls, &config.scid);
    config.peer_dcid_len = hdr->scid.len; /* Client's SCID = our DCID */
    config.peer_addr = *from_addr;
    config.idle_timeout_us = ls->config.idle_timeout_us;
    config.initial_max_data = ls->config.initial_max_data;
    config.initial_max_stream_data = ls->config.initial_max_stream_data;
    config.max_streams_bidi = ls->config.max_streams_bidi;
    config.max_streams_uni = ls->config.max_streams_uni;

    /* Create server-side connection */
    nxp_conn *conn = nxp_conn_create(&config, true);
    if (conn == nullptr) {
        ls->total_rejected++;
        return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);
    }

    /* Set the peer's DCID (client's SCID) */
    conn->dcid = hdr->scid;

    /* Add to listener */
    nxp_result r = listener_add_conn(ls, conn);
    if (nxp_result_is_err(r)) {
        nxp_conn_destroy(conn);
        ls->total_rejected++;
        return r;
    }

    ls->total_accepted++;

    /* Start handshake with client's initial DCID for key derivation.
     * Must happen BEFORE nxp_conn_recv so the handshake context
     * (and initial crypto keys) exist when the packet is processed. */
    nxp_result hs_r = nxp_conn_start_handshake(conn, &hdr->dcid);
    if (nxp_result_is_err(hs_r)) {
        nxp_listener_remove_conn(ls, conn);
        ls->total_rejected++;
        return hs_r;
    }

    /* Feed the initial datagram to the new connection */
    (void)nxp_conn_recv(conn, data, len, now_us);

    /* Notify the application */
    if (ls->config.on_new_conn != nullptr) {
        ls->config.on_new_conn(conn, ls->config.user_data);
    }

    return NXP_SUCCESS;
}

/* ── Receive ──────────────────────────────────────────── */

nxp_result nxp_listener_recv(nxp_listener_s *ls,
                              const uint8_t *data, size_t len,
                              const nxp_addr *from_addr,
                              uint64_t now_us) {
    if (ls == nullptr || data == nullptr || len == 0) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    /* Peek at first byte to determine header type */
    bool is_long = nxp_pkt_is_long_header(data[0]);

    if (is_long) {
        /* Parse long header to extract DCID */
        nxp_pkt_long_header hdr;
        nxp_result r = nxp_pkt_decode_long_header(data, len, &hdr);
        if (nxp_result_is_err(r)) return r;

        /* Look up connection by DCID */
        nxp_conn *conn = nxp_listener_find_conn(ls, &hdr.dcid);

        if (conn != nullptr) {
            /* Existing connection - forward datagram */
            return nxp_conn_recv(conn, data, len, now_us);
        }

        /* No existing connection. Only accept Initial packets. */
        if (hdr.type != NXP_PKT_INITIAL) {
            return NXP_ERROR(NXP_ERR_INVALID_PACKET);
        }

        /* Check version */
        if (hdr.version != NXP_VERSION_CURRENT) {
            return NXP_ERROR(NXP_ERR_VERSION_MISMATCH);
        }

        /* Stateless retry if configured */
        if (ls->config.require_retry && hdr.token_len == 0) {
            /* Client needs to present a retry token.
             * In a full implementation, we would generate a Retry packet here.
             * For now, just count and reject. The I/O layer is responsible for
             * sending the Retry packet using nxp_retry_token_create(). */
            ls->total_retried++;
            return NXP_ERROR(NXP_ERR_TOKEN_INVALID);
        }

        /* Accept the new connection */
        return listener_accept(ls, &hdr, data, len, from_addr, now_us);

    } else {
        /* Short header: extract DCID (fixed length, same as our CID) */
        if (len < 1 + NXP_LISTENER_CID_LEN) {
            return NXP_ERROR(NXP_ERR_INVALID_PACKET);
        }

        nxp_conn_id dcid;
        dcid.len = NXP_LISTENER_CID_LEN;
        memcpy(dcid.data, data + 1, NXP_LISTENER_CID_LEN);

        nxp_conn *conn = nxp_listener_find_conn(ls, &dcid);
        if (conn == nullptr) {
            /* Unknown connection */
            return NXP_ERROR(NXP_ERR_INVALID_PACKET);
        }

        return nxp_conn_recv(conn, data, len, now_us);
    }
}

/* ── Send ─────────────────────────────────────────────── */

ssize_t nxp_listener_send(nxp_listener_s *ls,
                           uint8_t *out, size_t max_len,
                           nxp_addr *peer_addr_out,
                           uint64_t now_us) {
    if (ls->conn_count == 0) return 0;

    /* Round-robin across connections */
    uint32_t checked = 0;
    while (checked < ls->conn_count) {
        uint32_t idx = ls->send_index % ls->conn_count;
        ls->send_index++;
        checked++;

        nxp_conn *conn = ls->conns[idx];
        if (conn == nullptr) continue;

        ssize_t written = nxp_conn_send(conn, out, max_len, now_us);
        if (written > 0) {
            *peer_addr_out = conn->peer_addr;
            return written;
        }
    }

    return 0; /* Nothing to send */
}

/* ── Timer ────────────────────────────────────────────── */

uint64_t nxp_listener_timeout(const nxp_listener_s *ls, uint64_t now_us) {
    uint64_t earliest = UINT64_MAX;

    for (uint32_t i = 0; i < ls->conn_count; i++) {
        if (ls->conns[i] == nullptr) continue;
        uint64_t t = nxp_conn_timeout(ls->conns[i], now_us);
        if (t < earliest) earliest = t;
    }

    return earliest;
}

void nxp_listener_on_timeout(nxp_listener_s *ls, uint64_t now_us) {
    /* Process timeouts and collect dead connections */
    uint32_t i = 0;
    while (i < ls->conn_count) {
        nxp_conn *conn = ls->conns[i];
        if (conn == nullptr) {
            i++;
            continue;
        }

        nxp_conn_on_timeout(conn, now_us);

        /* Remove closed/dead connections */
        nxp_conn_state st = nxp_conn_get_state(conn);
        if (st == NXP_CONN_CLOSED) {
            nxp_listener_remove_conn(ls, conn);
            /* Don't increment i: the last element was swapped in */
        } else {
            i++;
        }
    }
}
