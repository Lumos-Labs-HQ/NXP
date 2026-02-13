/*
 * NXP Session Export/Import - Implementation
 *
 * Phase 9: Serialize connection state for server migration.
 *
 * Only the core connection state is exported: CIDs, crypto keys,
 * packet numbers, flow control, and RTT estimates. Streams and
 * their buffers are not included - the receiving server handles
 * new stream creation on demand.
 */
#include "session_export.h"
#include "connection_internal.h"

#include <stdlib.h>
#include <string.h>

/* ── Wire Helpers ──────────────────────────────────────── */

static void put_u8(uint8_t **p, uint8_t v) {
    **p = v;
    (*p)++;
}

static void put_u32(uint8_t **p, uint32_t v) {
    memcpy(*p, &v, 4);
    *p += 4;
}

static void put_u64(uint8_t **p, uint64_t v) {
    memcpy(*p, &v, 8);
    *p += 8;
}

static void put_bytes(uint8_t **p, const uint8_t *src, size_t len) {
    memcpy(*p, src, len);
    *p += len;
}

static bool get_u8(const uint8_t **p, const uint8_t *end, uint8_t *out) {
    if (*p + 1 > end) return false;
    *out = **p;
    (*p)++;
    return true;
}

static bool get_u32(const uint8_t **p, const uint8_t *end, uint32_t *out) {
    if (*p + 4 > end) return false;
    memcpy(out, *p, 4);
    *p += 4;
    return true;
}

static bool get_u64(const uint8_t **p, const uint8_t *end, uint64_t *out) {
    if (*p + 8 > end) return false;
    memcpy(out, *p, 8);
    *p += 8;
    return true;
}

static bool get_bytes(const uint8_t **p, const uint8_t *end,
                       uint8_t *dst, size_t len) {
    if (*p + len > end) return false;
    memcpy(dst, *p, len);
    *p += len;
    return true;
}

/* ── Export Size ───────────────────────────────────────── */

size_t nxp_session_export_size(const nxp_conn *conn) {
    size_t size = 0;
    size += 4 + 4 + 4;          /* magic + version + total_len */
    size += 1 + conn->scid.len; /* scid_len + scid */
    size += 1 + conn->dcid.len; /* dcid_len + dcid */
    size += 1 + 1;              /* state + is_server */
    size += 8;                  /* next_pkt_num */
    size += sizeof(nxp_addr);   /* peer_addr */

    /* Crypto state: algo(1) + available(1) + send keys + recv keys */
    size += 1 + 1;  /* algo + available */
    size += 1 + NXP_AEAD_MAX_KEY_LEN + NXP_AEAD_IV_LEN + NXP_HP_KEY_LEN; /* send */
    size += 1 + NXP_AEAD_MAX_KEY_LEN + NXP_AEAD_IV_LEN + NXP_HP_KEY_LEN; /* recv */

    /* Flow control: 4x uint64 */
    size += 8 * 4;

    /* RTT estimates: 3x uint64 */
    size += 8 * 3;

    /* Idle timeout */
    size += 8;

    return size;
}

/* ── Export ─────────────────────────────────────────────── */

nxp_result nxp_session_export(const nxp_conn *conn,
                               uint8_t *buf, size_t buf_cap,
                               size_t *out_len) {
    if (conn == nullptr || buf == nullptr || out_len == nullptr) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    size_t needed = nxp_session_export_size(conn);
    if (buf_cap < needed) {
        return NXP_ERROR(NXP_ERR_BUFFER_TOO_SMALL);
    }

    uint8_t *p = buf;

    /* Header */
    put_u32(&p, NXP_SESSION_MAGIC);
    put_u32(&p, NXP_SESSION_VERSION);
    put_u32(&p, (uint32_t)needed);

    /* Connection IDs */
    put_u8(&p, conn->scid.len);
    put_bytes(&p, conn->scid.data, conn->scid.len);
    put_u8(&p, conn->dcid.len);
    put_bytes(&p, conn->dcid.data, conn->dcid.len);

    /* State */
    put_u8(&p, (uint8_t)conn->state);
    put_u8(&p, conn->is_server ? 1 : 0);

    /* Packet number */
    put_u64(&p, conn->next_pkt_num);

    /* Peer address */
    put_bytes(&p, (const uint8_t *)&conn->peer_addr, sizeof(nxp_addr));

    /* Crypto state */
    put_u8(&p, (uint8_t)conn->crypto.algo);
    put_u8(&p, conn->crypto.available ? 1 : 0);

    /* Send keys */
    put_u8(&p, conn->crypto.send.key_len);
    put_bytes(&p, conn->crypto.send.key, NXP_AEAD_MAX_KEY_LEN);
    put_bytes(&p, conn->crypto.send.iv, NXP_AEAD_IV_LEN);
    put_bytes(&p, conn->crypto.send.hp_key, NXP_HP_KEY_LEN);

    /* Recv keys */
    put_u8(&p, conn->crypto.recv.key_len);
    put_bytes(&p, conn->crypto.recv.key, NXP_AEAD_MAX_KEY_LEN);
    put_bytes(&p, conn->crypto.recv.iv, NXP_AEAD_IV_LEN);
    put_bytes(&p, conn->crypto.recv.hp_key, NXP_HP_KEY_LEN);

    /* Flow control */
    put_u64(&p, conn->conn_flow.peer_max_data);
    put_u64(&p, conn->conn_flow.data_sent);
    put_u64(&p, conn->conn_flow.local_max_data);
    put_u64(&p, conn->conn_flow.data_recv);

    /* RTT estimates */
    put_u64(&p, conn->ack.smoothed_rtt);
    put_u64(&p, conn->ack.rtt_var);
    put_u64(&p, conn->ack.min_rtt);

    /* Idle timeout */
    put_u64(&p, conn->idle_timeout_us);

    *out_len = (size_t)(p - buf);
    return NXP_SUCCESS;
}

/* ── Import ────────────────────────────────────────────── */

nxp_result nxp_session_import(const uint8_t *buf, size_t buf_len,
                               nxp_conn **conn_out) {
    if (buf == nullptr || conn_out == nullptr) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    const uint8_t *p = buf;
    const uint8_t *end = buf + buf_len;

    /* Header */
    uint32_t magic, version, total_len;
    if (!get_u32(&p, end, &magic)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (magic != NXP_SESSION_MAGIC) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (!get_u32(&p, end, &version)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (version != NXP_SESSION_VERSION) return NXP_ERROR(NXP_ERR_VERSION_MISMATCH);
    if (!get_u32(&p, end, &total_len)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (total_len > buf_len) return NXP_ERROR(NXP_ERR_BUFFER_TOO_SMALL);

    /* Connection IDs */
    nxp_conn_id scid, dcid;
    memset(&scid, 0, sizeof(scid));
    memset(&dcid, 0, sizeof(dcid));

    if (!get_u8(&p, end, &scid.len)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (scid.len > NXP_MAX_CID_LEN) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (!get_bytes(&p, end, scid.data, scid.len)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);

    if (!get_u8(&p, end, &dcid.len)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (dcid.len > NXP_MAX_CID_LEN) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (!get_bytes(&p, end, dcid.data, dcid.len)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);

    /* State */
    uint8_t state_u8, is_server_u8;
    if (!get_u8(&p, end, &state_u8)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (!get_u8(&p, end, &is_server_u8)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);

    /* Packet number */
    uint64_t next_pkt_num;
    if (!get_u64(&p, end, &next_pkt_num)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);

    /* Peer address */
    nxp_addr peer_addr;
    if (!get_bytes(&p, end, (uint8_t *)&peer_addr, sizeof(nxp_addr))) {
        return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    }

    /* Crypto state */
    uint8_t algo_u8, crypto_available_u8;
    if (!get_u8(&p, end, &algo_u8)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (!get_u8(&p, end, &crypto_available_u8)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);

    nxp_crypto_keys send_keys, recv_keys;
    memset(&send_keys, 0, sizeof(send_keys));
    memset(&recv_keys, 0, sizeof(recv_keys));

    if (!get_u8(&p, end, &send_keys.key_len)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (!get_bytes(&p, end, send_keys.key, NXP_AEAD_MAX_KEY_LEN)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (!get_bytes(&p, end, send_keys.iv, NXP_AEAD_IV_LEN)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (!get_bytes(&p, end, send_keys.hp_key, NXP_HP_KEY_LEN)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);

    if (!get_u8(&p, end, &recv_keys.key_len)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (!get_bytes(&p, end, recv_keys.key, NXP_AEAD_MAX_KEY_LEN)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (!get_bytes(&p, end, recv_keys.iv, NXP_AEAD_IV_LEN)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (!get_bytes(&p, end, recv_keys.hp_key, NXP_HP_KEY_LEN)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);

    /* Flow control */
    uint64_t peer_max_data, data_sent, local_max_data, data_recv;
    if (!get_u64(&p, end, &peer_max_data)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (!get_u64(&p, end, &data_sent)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (!get_u64(&p, end, &local_max_data)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (!get_u64(&p, end, &data_recv)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);

    /* RTT estimates */
    uint64_t smoothed_rtt, rtt_var, min_rtt;
    if (!get_u64(&p, end, &smoothed_rtt)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (!get_u64(&p, end, &rtt_var)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (!get_u64(&p, end, &min_rtt)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);

    /* Idle timeout */
    uint64_t idle_timeout_us;
    if (!get_u64(&p, end, &idle_timeout_us)) return NXP_ERROR(NXP_ERR_INVALID_PACKET);

    /* ── Reconstruct connection ──────────────────────────── */

    nxp_conn_config config;
    memset(&config, 0, sizeof(config));
    config.scid = scid;
    config.peer_addr = peer_addr;
    config.idle_timeout_us = idle_timeout_us;
    config.initial_max_data = local_max_data;

    nxp_conn *conn = nxp_conn_create(&config, is_server_u8 != 0);
    if (conn == nullptr) return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);

    /* Restore state */
    conn->dcid = dcid;
    conn->state = (nxp_conn_state)state_u8;
    conn->next_pkt_num = next_pkt_num;

    /* Restore crypto */
    conn->crypto.algo = (nxp_aead_algo)algo_u8;
    conn->crypto.available = (crypto_available_u8 != 0);
    conn->crypto.send = send_keys;
    conn->crypto.recv = recv_keys;

    /* Restore flow control */
    conn->conn_flow.peer_max_data = peer_max_data;
    conn->conn_flow.data_sent = data_sent;
    conn->conn_flow.local_max_data = local_max_data;
    conn->conn_flow.data_recv = data_recv;

    /* Restore RTT estimates */
    conn->ack.smoothed_rtt = smoothed_rtt;
    conn->ack.rtt_var = rtt_var;
    conn->ack.min_rtt = min_rtt;
    conn->ack.has_rtt = (smoothed_rtt > 0);

    *conn_out = conn;
    return NXP_SUCCESS;
}
