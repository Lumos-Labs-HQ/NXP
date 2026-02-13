/*
 * NXP Retry Tokens - Implementation
 *
 * Phase 6: Stateless retry token creation and validation for DDoS
 * mitigation. When the server is under load, it sends a Retry packet
 * containing a token that proves the client owns its source address.
 *
 * Token format (plaintext, before encryption):
 *   [version:1][timestamp:8][client_addr:28][original_dcid_len:1][original_dcid:0-20]
 *
 * Wire format: [nonce:12][ciphertext+tag]
 * Encrypted with AES-256-GCM using a server-local key.
 */
#include "crypto_internal.h"
#include "secure_mem.h"
#include "nxp/nxp_types.h"
#include "util/random.h"

#include <string.h>

/* ── Constants ────────────────────────────────────────── */

#define NXP_RETRY_TOKEN_VERSION    0x01
#define NXP_RETRY_TOKEN_MAX_AGE_US (10ULL * 1000000ULL) /* 10 seconds */
#define NXP_RETRY_TOKEN_NONCE_LEN  NXP_AEAD_IV_LEN

/* ── Token Creation (Server Side) ─────────────────────── */

nxp_result nxp_retry_token_create(
    const uint8_t server_key[NXP_AES_256_KEY_LEN],
    const nxp_addr *client_addr,
    const nxp_conn_id *original_dcid,
    uint64_t now_us,
    uint8_t *token_out, size_t token_cap, size_t *token_len_out)
{
    if (server_key == nullptr || client_addr == nullptr ||
        original_dcid == nullptr || token_out == nullptr ||
        token_len_out == nullptr) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    /* Build plaintext */
    uint8_t pt[128];
    size_t pt_len = 0;

    /* Version */
    pt[pt_len++] = NXP_RETRY_TOKEN_VERSION;

    /* Timestamp (8 bytes, big-endian) */
    for (int i = 7; i >= 0; i--) {
        pt[pt_len++] = (uint8_t)(now_us >> (uint64_t)(i * 8));
    }

    /* Client address (28 bytes raw) */
    memcpy(pt + pt_len, client_addr->raw, sizeof(client_addr->raw));
    pt_len += sizeof(client_addr->raw);

    /* Original DCID */
    pt[pt_len++] = original_dcid->len;
    if (original_dcid->len > 0) {
        memcpy(pt + pt_len, original_dcid->data, original_dcid->len);
        pt_len += original_dcid->len;
    }

    /* Encrypt: output = [nonce:12][ciphertext+tag] */
    size_t needed = NXP_RETRY_TOKEN_NONCE_LEN + pt_len + NXP_AEAD_TAG_LEN;
    if (token_cap < needed) return NXP_ERROR(NXP_ERR_BUFFER_TOO_SMALL);

    /* Generate random nonce */
    uint8_t nonce[NXP_RETRY_TOKEN_NONCE_LEN];
    nxp_result r = nxp_random_bytes(nonce, sizeof(nonce));
    if (r.code != NXP_OK) return r;

    memcpy(token_out, nonce, NXP_RETRY_TOKEN_NONCE_LEN);

    /* Encrypt with AES-256-GCM (no AAD) */
    ssize_t ct_len = nxp_aead_encrypt(
        NXP_AEAD_AES_256_GCM,
        server_key, NXP_AES_256_KEY_LEN,
        nonce, nullptr, 0,
        pt, pt_len, token_out + NXP_RETRY_TOKEN_NONCE_LEN
    );
    if (ct_len < 0) {
        nxp_secure_zero(pt, sizeof(pt));
        return NXP_ERROR(NXP_ERR_CRYPTO_FAIL);
    }

    *token_len_out = NXP_RETRY_TOKEN_NONCE_LEN + (size_t)ct_len;
    nxp_secure_zero(pt, sizeof(pt));

    return NXP_SUCCESS;
}

/* ── Token Validation (Server Side) ───────────────────── */

nxp_result nxp_retry_token_validate(
    const uint8_t server_key[NXP_AES_256_KEY_LEN],
    const nxp_addr *client_addr,
    const nxp_conn_id *original_dcid,
    const uint8_t *token, size_t token_len,
    uint64_t now_us)
{
    if (server_key == nullptr || client_addr == nullptr ||
        original_dcid == nullptr || token == nullptr) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    /* Minimum token: nonce + version + time + addr + dcid_len + tag */
    size_t min_len = NXP_RETRY_TOKEN_NONCE_LEN + 1 + 8 + 28 + 1 + NXP_AEAD_TAG_LEN;
    if (token_len < min_len) return NXP_ERROR(NXP_ERR_TOKEN_INVALID);

    /* Decrypt */
    const uint8_t *nonce = token;
    const uint8_t *ct = token + NXP_RETRY_TOKEN_NONCE_LEN;
    size_t ct_len = token_len - NXP_RETRY_TOKEN_NONCE_LEN;

    uint8_t pt[128];
    ssize_t pt_len = nxp_aead_decrypt(
        NXP_AEAD_AES_256_GCM,
        server_key, NXP_AES_256_KEY_LEN,
        nonce, nullptr, 0,
        ct, ct_len, pt
    );
    if (pt_len < 0) return NXP_ERROR(NXP_ERR_TOKEN_INVALID);

    size_t pos = 0;

    /* Version check */
    if ((size_t)pt_len < 1 + 8 + 28 + 1) {
        nxp_secure_zero(pt, sizeof(pt));
        return NXP_ERROR(NXP_ERR_TOKEN_INVALID);
    }
    if (pt[pos++] != NXP_RETRY_TOKEN_VERSION) {
        nxp_secure_zero(pt, sizeof(pt));
        return NXP_ERROR(NXP_ERR_TOKEN_INVALID);
    }

    /* Timestamp check */
    uint64_t creation_time = 0;
    for (int i = 7; i >= 0; i--) {
        creation_time |= (uint64_t)pt[pos++] << (uint64_t)(i * 8);
    }
    if (now_us > creation_time + NXP_RETRY_TOKEN_MAX_AGE_US) {
        nxp_secure_zero(pt, sizeof(pt));
        return NXP_ERROR(NXP_ERR_TOKEN_INVALID);
    }

    /* Client address check */
    if (memcmp(pt + pos, client_addr->raw, 28) != 0) {
        nxp_secure_zero(pt, sizeof(pt));
        return NXP_ERROR(NXP_ERR_TOKEN_INVALID);
    }
    pos += 28;

    /* Original DCID check */
    uint8_t dcid_len = pt[pos++];
    if (dcid_len != original_dcid->len) {
        nxp_secure_zero(pt, sizeof(pt));
        return NXP_ERROR(NXP_ERR_TOKEN_INVALID);
    }
    if (dcid_len > 0) {
        if (pos + dcid_len > (size_t)pt_len) {
            nxp_secure_zero(pt, sizeof(pt));
            return NXP_ERROR(NXP_ERR_TOKEN_INVALID);
        }
        if (memcmp(pt + pos, original_dcid->data, dcid_len) != 0) {
            nxp_secure_zero(pt, sizeof(pt));
            return NXP_ERROR(NXP_ERR_TOKEN_INVALID);
        }
    }

    nxp_secure_zero(pt, sizeof(pt));
    return NXP_SUCCESS;
}
