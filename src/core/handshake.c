/*
 * NXP Handshake State Machine
 *
 * Phase 5: 1-RTT handshake using X25519 + HKDF key schedule.
 * Processes CRYPTO frame data, derives handshake and application keys.
 *
 * Message format (inside CRYPTO frames):
 *   ClientHello: [0x01][x25519_pub:32][num_ciphers:1][cipher_ids...][transport_params]
 *   ServerHello: [0x02][x25519_pub:32][selected_cipher:1][transport_params]
 *
 * Transport params: sequence of [param_id:varint][param_len:varint][value:bytes]
 */
#include "handshake_internal.h"
#include "crypto_internal.h"
#include "util/varint.h"

#include <stdlib.h>
#include <string.h>

/* ── Helpers: Transport Parameter Encoding ────────────── */

static size_t encode_tp_varint(uint8_t *buf, size_t cap,
                                uint8_t param_id, uint64_t value)
{
    uint8_t val_buf[8];
    size_t val_len = nxp_varint_encode(value, val_buf, sizeof(val_buf));
    if (val_len == 0) return 0;

    /* param_id (1 byte) + param_len varint + value */
    size_t needed = 1 + 1 + val_len; /* param_id + len(1 byte for small vals) + value */
    if (needed > cap) return 0;

    size_t pos = 0;
    buf[pos++] = param_id;
    buf[pos++] = (uint8_t)val_len;
    memcpy(buf + pos, val_buf, val_len);
    pos += val_len;
    return pos;
}

static size_t encode_transport_params(const nxp_transport_params *tp,
                                       uint8_t *buf, size_t cap)
{
    size_t pos = 0;
    size_t n;

    n = encode_tp_varint(buf + pos, cap - pos,
                          NXP_TP_INITIAL_MAX_DATA, tp->initial_max_data);
    if (n == 0) return 0;
    pos += n;

    n = encode_tp_varint(buf + pos, cap - pos,
                          NXP_TP_INITIAL_MAX_STREAM_DATA, tp->initial_max_stream_data);
    if (n == 0) return 0;
    pos += n;

    n = encode_tp_varint(buf + pos, cap - pos,
                          NXP_TP_MAX_STREAMS_BIDI, tp->max_streams_bidi);
    if (n == 0) return 0;
    pos += n;

    n = encode_tp_varint(buf + pos, cap - pos,
                          NXP_TP_MAX_STREAMS_UNI, tp->max_streams_uni);
    if (n == 0) return 0;
    pos += n;

    n = encode_tp_varint(buf + pos, cap - pos,
                          NXP_TP_IDLE_TIMEOUT, tp->idle_timeout_us / 1000);
    if (n == 0) return 0;
    pos += n;

    return pos;
}

static bool decode_transport_params(const uint8_t *buf, size_t len,
                                     nxp_transport_params *tp)
{
    size_t pos = 0;
    memset(tp, 0, sizeof(*tp));

    while (pos < len) {
        if (pos + 2 > len) return false;
        uint8_t param_id = buf[pos++];
        uint8_t param_len = buf[pos++];
        if (pos + param_len > len) return false;

        uint64_t val = 0;
        size_t consumed = nxp_varint_decode(buf + pos, param_len, &val);
        if (consumed == 0) return false;

        switch (param_id) {
        case NXP_TP_INITIAL_MAX_DATA:       tp->initial_max_data = val; break;
        case NXP_TP_INITIAL_MAX_STREAM_DATA: tp->initial_max_stream_data = val; break;
        case NXP_TP_MAX_STREAMS_BIDI:       tp->max_streams_bidi = (uint32_t)val; break;
        case NXP_TP_MAX_STREAMS_UNI:        tp->max_streams_uni = (uint32_t)val; break;
        case NXP_TP_IDLE_TIMEOUT:           tp->idle_timeout_us = val * 1000; break;
        default: break; /* Ignore unknown params */
        }

        pos += param_len;
    }

    return true;
}

/* ── Helpers: Handshake Message Encoding ──────────────── */

static size_t encode_client_hello(const nxp_handshake *hs,
                                   uint8_t *buf, size_t cap)
{
    /* [msg_type:1][x25519_pub:32][num_ciphers:1][cipher_ids...][transport_params] */
    size_t pos = 0;
    if (cap < 36) return 0;

    buf[pos++] = NXP_HS_CLIENT_HELLO;
    memcpy(buf + pos, hs->local_pubkey, NXP_X25519_KEY_LEN);
    pos += NXP_X25519_KEY_LEN;

    /* Supported ciphers: AES-256-GCM and ChaCha20-Poly1305 */
    buf[pos++] = 2;
    buf[pos++] = (uint8_t)NXP_AEAD_AES_256_GCM;
    buf[pos++] = (uint8_t)NXP_AEAD_CHACHA20_POLY1305;

    /* Transport parameters */
    size_t tp_len = encode_transport_params(&hs->local_params,
                                             buf + pos, cap - pos);
    pos += tp_len;

    return pos;
}

static size_t encode_server_hello(const nxp_handshake *hs,
                                   uint8_t *buf, size_t cap)
{
    /* [msg_type:1][x25519_pub:32][selected_cipher:1][transport_params] */
    size_t pos = 0;
    if (cap < 35) return 0;

    buf[pos++] = NXP_HS_SERVER_HELLO;
    memcpy(buf + pos, hs->local_pubkey, NXP_X25519_KEY_LEN);
    pos += NXP_X25519_KEY_LEN;

    buf[pos++] = (uint8_t)hs->selected_algo;

    /* Transport parameters */
    size_t tp_len = encode_transport_params(&hs->local_params,
                                             buf + pos, cap - pos);
    pos += tp_len;

    return pos;
}

/* ── Key Schedule ─────────────────────────────────────── */

static bool derive_handshake_and_app_keys(nxp_handshake *hs)
{
    /* transcript_hash = SHA-256(ClientHello || ServerHello) */
    uint8_t transcript_hash[NXP_HASH_LEN];
    if (!nxp_crypto_hash(hs->transcript, hs->transcript_len, transcript_hash))
        return false;

    /* derived_secret = HKDF-Expand-Label(initial_secret, "derived", "", 32)
     * We re-derive the initial secret here from the initial keys' context.
     * Actually, we need the initial_secret. Let's use a simpler approach:
     *
     * handshake_secret = HKDF-Extract(0...0, shared_secret)
     * This binds to the X25519 exchange. The transcript hash is mixed
     * into the per-direction secrets below.
     */
    uint8_t zero_salt[NXP_HASH_LEN];
    memset(zero_salt, 0, sizeof(zero_salt));

    uint8_t handshake_secret[NXP_HASH_LEN];
    if (!nxp_hkdf_extract(zero_salt, NXP_HASH_LEN,
                           hs->shared_secret, NXP_X25519_KEY_LEN,
                           handshake_secret))
        return false;

    /* Derive handshake traffic secrets */
    uint8_t client_hs_secret[NXP_HASH_LEN];
    if (!nxp_hkdf_expand_label(handshake_secret, NXP_HASH_LEN,
                                "c hs traffic", transcript_hash, NXP_HASH_LEN,
                                client_hs_secret, NXP_HASH_LEN))
        goto fail;

    uint8_t server_hs_secret[NXP_HASH_LEN];
    if (!nxp_hkdf_expand_label(handshake_secret, NXP_HASH_LEN,
                                "s hs traffic", transcript_hash, NXP_HASH_LEN,
                                server_hs_secret, NXP_HASH_LEN))
        goto fail;

    /* Derive handshake keys */
    hs->handshake_keys.algo = hs->selected_algo;
    if (hs->is_server) {
        if (!nxp_crypto_derive_traffic_keys(server_hs_secret, NXP_HASH_LEN,
                                             hs->selected_algo, &hs->handshake_keys.send))
            goto fail;
        if (!nxp_crypto_derive_traffic_keys(client_hs_secret, NXP_HASH_LEN,
                                             hs->selected_algo, &hs->handshake_keys.recv))
            goto fail;
    } else {
        if (!nxp_crypto_derive_traffic_keys(client_hs_secret, NXP_HASH_LEN,
                                             hs->selected_algo, &hs->handshake_keys.send))
            goto fail;
        if (!nxp_crypto_derive_traffic_keys(server_hs_secret, NXP_HASH_LEN,
                                             hs->selected_algo, &hs->handshake_keys.recv))
            goto fail;
    }
    hs->handshake_keys.available = true;

    /* Derive application keys:
     * master_secret = HKDF-Extract(derived, zeros)
     * derived = HKDF-Expand-Label(handshake_secret, "derived", "", 32) */
    uint8_t derived[NXP_HASH_LEN];
    if (!nxp_hkdf_expand_label(handshake_secret, NXP_HASH_LEN,
                                "derived", nullptr, 0,
                                derived, NXP_HASH_LEN))
        goto fail;

    uint8_t zeros[NXP_HASH_LEN];
    memset(zeros, 0, sizeof(zeros));

    uint8_t master_secret[NXP_HASH_LEN];
    if (!nxp_hkdf_extract(derived, NXP_HASH_LEN,
                           zeros, NXP_HASH_LEN,
                           master_secret))
        goto fail;

    uint8_t client_app_secret[NXP_HASH_LEN];
    if (!nxp_hkdf_expand_label(master_secret, NXP_HASH_LEN,
                                "c ap traffic", transcript_hash, NXP_HASH_LEN,
                                client_app_secret, NXP_HASH_LEN))
        goto fail;

    uint8_t server_app_secret[NXP_HASH_LEN];
    if (!nxp_hkdf_expand_label(master_secret, NXP_HASH_LEN,
                                "s ap traffic", transcript_hash, NXP_HASH_LEN,
                                server_app_secret, NXP_HASH_LEN))
        goto fail;

    hs->app_keys.algo = hs->selected_algo;
    if (hs->is_server) {
        if (!nxp_crypto_derive_traffic_keys(server_app_secret, NXP_HASH_LEN,
                                             hs->selected_algo, &hs->app_keys.send))
            goto fail;
        if (!nxp_crypto_derive_traffic_keys(client_app_secret, NXP_HASH_LEN,
                                             hs->selected_algo, &hs->app_keys.recv))
            goto fail;
    } else {
        if (!nxp_crypto_derive_traffic_keys(client_app_secret, NXP_HASH_LEN,
                                             hs->selected_algo, &hs->app_keys.send))
            goto fail;
        if (!nxp_crypto_derive_traffic_keys(server_app_secret, NXP_HASH_LEN,
                                             hs->selected_algo, &hs->app_keys.recv))
            goto fail;
    }
    hs->app_keys.available = true;

    /* Wipe intermediate secrets */
    memset(handshake_secret, 0, sizeof(handshake_secret));
    memset(client_hs_secret, 0, sizeof(client_hs_secret));
    memset(server_hs_secret, 0, sizeof(server_hs_secret));
    memset(derived, 0, sizeof(derived));
    memset(master_secret, 0, sizeof(master_secret));
    memset(client_app_secret, 0, sizeof(client_app_secret));
    memset(server_app_secret, 0, sizeof(server_app_secret));
    return true;

fail:
    memset(handshake_secret, 0, sizeof(handshake_secret));
    memset(client_hs_secret, 0, sizeof(client_hs_secret));
    memset(server_hs_secret, 0, sizeof(server_hs_secret));
    return false;
}

/* ── Create / Destroy ─────────────────────────────────── */

nxp_handshake *nxp_handshake_create(bool is_server) {
    nxp_handshake *hs = (nxp_handshake *)calloc(1, sizeof(nxp_handshake));
    if (hs == nullptr) return nullptr;

    hs->is_server = is_server;
    hs->state = NXP_HS_IDLE;
    hs->selected_algo = NXP_AEAD_AES_256_GCM;

    /* Generate X25519 keypair */
    if (!nxp_x25519_keygen(hs->local_pubkey, hs->local_privkey)) {
        free(hs);
        return nullptr;
    }

    return hs;
}

void nxp_handshake_destroy(nxp_handshake *hs) {
    if (hs == nullptr) return;

    /* Wipe all key material */
    memset(hs->local_privkey, 0, sizeof(hs->local_privkey));
    memset(hs->shared_secret, 0, sizeof(hs->shared_secret));
    memset(&hs->initial_keys, 0, sizeof(hs->initial_keys));
    memset(&hs->handshake_keys, 0, sizeof(hs->handshake_keys));
    memset(&hs->app_keys, 0, sizeof(hs->app_keys));

    free(hs);
}

void nxp_handshake_set_local_params(nxp_handshake *hs,
                                     const nxp_transport_params *params)
{
    hs->local_params = *params;
}

/* ── Client: Start Handshake ──────────────────────────── */

nxp_result nxp_handshake_start_client(
    nxp_handshake *hs,
    const nxp_conn_id *server_dcid)
{
    if (hs->state != NXP_HS_IDLE || hs->is_server) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    /* Derive initial keys from server's DCID.
     * Client sends with client keys, receives with server keys.
     * nxp_crypto_derive_initial_keys puts client keys in .send, server in .recv.
     * That's already correct for the client perspective. */
    if (!nxp_crypto_derive_initial_keys(server_dcid, &hs->initial_keys)) {
        return NXP_ERROR(NXP_ERR_CRYPTO_FAIL);
    }

    /* Encode ClientHello into send buffer */
    hs->send_len = encode_client_hello(hs, hs->send_buf, sizeof(hs->send_buf));
    if (hs->send_len == 0) return NXP_ERROR(NXP_ERR_INTERNAL);
    hs->send_offset = 0;
    hs->send_level = NXP_CRYPTO_INITIAL;

    /* Add to transcript */
    memcpy(hs->transcript, hs->send_buf, hs->send_len);
    hs->transcript_len = hs->send_len;

    hs->state = NXP_HS_WAIT_SERVER_HELLO;
    return NXP_SUCCESS;
}

/* ── Server: Start Handshake ──────────────────────────── */

nxp_result nxp_handshake_start_server(
    nxp_handshake *hs,
    const nxp_conn_id *local_scid)
{
    if (hs->state != NXP_HS_IDLE || !hs->is_server) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    /* Derive initial keys from our SCID (which is the client's DCID).
     * Server receives with client keys, sends with server keys.
     * nxp_crypto_derive_initial_keys puts client keys in .send, server in .recv.
     * For server: swap send/recv. */
    if (!nxp_crypto_derive_initial_keys(local_scid, &hs->initial_keys)) {
        return NXP_ERROR(NXP_ERR_CRYPTO_FAIL);
    }

    /* Swap: server sends with server keys, receives with client keys */
    nxp_crypto_keys tmp = hs->initial_keys.send;
    hs->initial_keys.send = hs->initial_keys.recv;
    hs->initial_keys.recv = tmp;

    hs->state = NXP_HS_WAIT_CLIENT_HELLO;
    return NXP_SUCCESS;
}

/* ── Process Incoming CRYPTO Data ─────────────────────── */

static nxp_result process_client_hello(nxp_handshake *hs,
                                        const uint8_t *data, size_t len)
{
    /* Parse: [0x01][pubkey:32][num_ciphers:1][cipher_ids...][transport_params] */
    if (len < 36) return NXP_ERROR(NXP_ERR_HANDSHAKE_FAIL);
    if (data[0] != NXP_HS_CLIENT_HELLO) return NXP_ERROR(NXP_ERR_HANDSHAKE_FAIL);

    size_t pos = 1;
    memcpy(hs->peer_pubkey, data + pos, NXP_X25519_KEY_LEN);
    pos += NXP_X25519_KEY_LEN;
    hs->has_peer_pubkey = true;

    /* Parse supported ciphers */
    uint8_t num_ciphers = data[pos++];
    if (pos + num_ciphers > len) return NXP_ERROR(NXP_ERR_HANDSHAKE_FAIL);

    /* Select the first cipher we support (prefer AES-256-GCM) */
    bool found = false;
    for (uint8_t i = 0; i < num_ciphers; i++) {
        uint8_t cipher_id = data[pos + i];
        if (cipher_id == NXP_AEAD_AES_256_GCM ||
            cipher_id == NXP_AEAD_CHACHA20_POLY1305) {
            hs->selected_algo = (nxp_aead_algo)cipher_id;
            found = true;
            break;
        }
    }
    if (!found) return NXP_ERROR(NXP_ERR_HANDSHAKE_FAIL);
    pos += num_ciphers;

    /* Parse transport parameters */
    if (pos < len) {
        if (!decode_transport_params(data + pos, len - pos, &hs->peer_params))
            return NXP_ERROR(NXP_ERR_HANDSHAKE_FAIL);
        hs->has_peer_params = true;
    }

    /* Add ClientHello to transcript */
    if (hs->transcript_len + len > sizeof(hs->transcript))
        return NXP_ERROR(NXP_ERR_INTERNAL);
    memcpy(hs->transcript + hs->transcript_len, data, len);
    hs->transcript_len += len;

    /* Compute shared secret */
    if (!nxp_x25519_shared_secret(hs->local_privkey, hs->peer_pubkey,
                                   hs->shared_secret))
        return NXP_ERROR(NXP_ERR_CRYPTO_FAIL);

    /* Encode ServerHello into send buffer.
     * Send at Initial level because the client needs ServerHello content
     * to derive handshake/app keys (both sides have initial keys). */
    hs->send_len = encode_server_hello(hs, hs->send_buf, sizeof(hs->send_buf));
    if (hs->send_len == 0) return NXP_ERROR(NXP_ERR_INTERNAL);
    hs->send_offset = 0;
    hs->send_level = NXP_CRYPTO_INITIAL;

    /* Add ServerHello to transcript */
    if (hs->transcript_len + hs->send_len > sizeof(hs->transcript))
        return NXP_ERROR(NXP_ERR_INTERNAL);
    memcpy(hs->transcript + hs->transcript_len, hs->send_buf, hs->send_len);
    hs->transcript_len += hs->send_len;

    /* Derive handshake and application keys */
    if (!derive_handshake_and_app_keys(hs))
        return NXP_ERROR(NXP_ERR_CRYPTO_FAIL);

    /* Server sends HANDSHAKE_DONE in a 1-RTT packet */
    hs->send_handshake_done = true;

    /* Server is done after sending ServerHello + HANDSHAKE_DONE */
    hs->state = NXP_HS_COMPLETE;
    return NXP_SUCCESS;
}

static nxp_result process_server_hello(nxp_handshake *hs,
                                        const uint8_t *data, size_t len)
{
    /* Parse: [0x02][pubkey:32][selected_cipher:1][transport_params] */
    if (len < 35) return NXP_ERROR(NXP_ERR_HANDSHAKE_FAIL);
    if (data[0] != NXP_HS_SERVER_HELLO) return NXP_ERROR(NXP_ERR_HANDSHAKE_FAIL);

    size_t pos = 1;
    memcpy(hs->peer_pubkey, data + pos, NXP_X25519_KEY_LEN);
    pos += NXP_X25519_KEY_LEN;
    hs->has_peer_pubkey = true;

    hs->selected_algo = (nxp_aead_algo)data[pos++];

    /* Parse transport parameters */
    if (pos < len) {
        if (!decode_transport_params(data + pos, len - pos, &hs->peer_params))
            return NXP_ERROR(NXP_ERR_HANDSHAKE_FAIL);
        hs->has_peer_params = true;
    }

    /* Add ServerHello to transcript */
    if (hs->transcript_len + len > sizeof(hs->transcript))
        return NXP_ERROR(NXP_ERR_INTERNAL);
    memcpy(hs->transcript + hs->transcript_len, data, len);
    hs->transcript_len += len;

    /* Compute shared secret */
    if (!nxp_x25519_shared_secret(hs->local_privkey, hs->peer_pubkey,
                                   hs->shared_secret))
        return NXP_ERROR(NXP_ERR_CRYPTO_FAIL);

    /* Derive handshake and application keys */
    if (!derive_handshake_and_app_keys(hs))
        return NXP_ERROR(NXP_ERR_CRYPTO_FAIL);

    hs->state = NXP_HS_WAIT_HANDSHAKE_DONE;
    return NXP_SUCCESS;
}

nxp_result nxp_handshake_recv_crypto(
    nxp_handshake *hs,
    nxp_crypto_level level,
    const uint8_t *data, size_t len)
{
    switch (hs->state) {
    case NXP_HS_WAIT_CLIENT_HELLO:
        if (level != NXP_CRYPTO_INITIAL)
            return NXP_ERROR(NXP_ERR_HANDSHAKE_FAIL);
        return process_client_hello(hs, data, len);

    case NXP_HS_WAIT_SERVER_HELLO:
        if (level != NXP_CRYPTO_INITIAL)
            return NXP_ERROR(NXP_ERR_HANDSHAKE_FAIL);
        return process_server_hello(hs, data, len);

    default:
        return NXP_ERROR(NXP_ERR_HANDSHAKE_FAIL);
    }
}

nxp_result nxp_handshake_on_handshake_done(nxp_handshake *hs) {
    if (hs->state != NXP_HS_WAIT_HANDSHAKE_DONE) {
        return NXP_ERROR(NXP_ERR_HANDSHAKE_FAIL);
    }
    hs->state = NXP_HS_COMPLETE;
    return NXP_SUCCESS;
}

/* ── Query / Data Access ──────────────────────────────── */

bool nxp_handshake_has_data(const nxp_handshake *hs) {
    return hs->send_offset < hs->send_len || hs->send_handshake_done;
}

nxp_crypto_level nxp_handshake_send_level(const nxp_handshake *hs) {
    if (hs->send_offset < hs->send_len) return hs->send_level;
    if (hs->send_handshake_done) return NXP_CRYPTO_APPLICATION;
    return NXP_CRYPTO_APPLICATION; /* Default */
}

size_t nxp_handshake_fill_crypto(
    nxp_handshake *hs,
    uint8_t *data, size_t max_len,
    uint64_t *offset)
{
    size_t remaining = hs->send_len - hs->send_offset;
    if (remaining == 0) return 0;

    size_t to_copy = remaining;
    if (to_copy > max_len) to_copy = max_len;

    *offset = hs->send_offset;
    memcpy(data, hs->send_buf + hs->send_offset, to_copy);
    hs->send_offset += to_copy;

    return to_copy;
}
