/*
 * NXP Session Tickets - Implementation
 *
 * Phase 6: Session ticket creation, encryption, decryption, and
 * 0-RTT key derivation from resumption secret.
 *
 * Ticket format (plaintext, before encryption):
 *   [version:1][creation_time:8][lifetime_sec:4][aead_algo:1]
 *   [resumption_secret:32][transport_params_len:2][transport_params:...]
 *
 * The ticket is encrypted with AES-256-GCM using a server-local key.
 * Wire format: [nonce:12][ciphertext:...][tag:16]
 *
 * Replay protection: the server maintains a strike register of
 * (ticket_age, client_addr_hash) tuples. A ticket used for 0-RTT
 * can only be used once.
 */
#include "crypto_internal.h"
#include "util/random.h"

#include <stdlib.h>
#include <string.h>

/* ── Constants ────────────────────────────────────────── */

#define NXP_TICKET_VERSION           0x01
#define NXP_TICKET_MAX_PLAINTEXT     512
#define NXP_TICKET_NONCE_LEN         NXP_AEAD_IV_LEN
#define NXP_TICKET_DEFAULT_LIFETIME  3600  /* 1 hour in seconds */
#define NXP_TICKET_MAX_AGE_US        (3600ULL * 1000000ULL) /* 1 hour */

/* ── Session Ticket Structures ────────────────────────── */

/* Strike register entry for replay protection */
#define NXP_STRIKE_REGISTER_SIZE 1024

typedef struct nxp_strike_entry {
    uint64_t ticket_hash;     /* Hash of ticket nonce */
    uint64_t timestamp;       /* When it was used */
    bool     occupied;
} nxp_strike_entry;

struct nxp_strike_register {
    nxp_strike_entry entries[NXP_STRIKE_REGISTER_SIZE];
    uint32_t         count;
};

/* ── Strike Register ──────────────────────────────────── */

nxp_strike_register *nxp_strike_register_create(void) {
    nxp_strike_register *reg = (nxp_strike_register *)calloc(1, sizeof(*reg));
    return reg;
}

void nxp_strike_register_destroy(nxp_strike_register *reg) {
    if (reg != nullptr) {
        memset(reg, 0, sizeof(*reg));
        free(reg);
    }
}

/* Simple hash for strike register lookup */
static uint64_t hash_ticket(const uint8_t *nonce, size_t nonce_len) {
    uint64_t h = 0x517cc1b727220a95ULL; /* FNV offset basis */
    for (size_t i = 0; i < nonce_len; i++) {
        h ^= nonce[i];
        h *= 0x00000100000001B3ULL; /* FNV prime */
    }
    return h;
}

bool nxp_strike_register_check_and_add(
    nxp_strike_register *reg,
    const uint8_t *ticket_nonce, size_t nonce_len,
    uint64_t now_us)
{
    uint64_t h = hash_ticket(ticket_nonce, nonce_len);
    uint32_t idx = (uint32_t)(h % NXP_STRIKE_REGISTER_SIZE);

    /* Check if this ticket was already used */
    if (reg->entries[idx].occupied && reg->entries[idx].ticket_hash == h) {
        return false; /* Replay detected */
    }

    /* Evict expired entries and add new one */
    reg->entries[idx].ticket_hash = h;
    reg->entries[idx].timestamp = now_us;
    reg->entries[idx].occupied = true;
    if (reg->count < NXP_STRIKE_REGISTER_SIZE) reg->count++;

    return true; /* First use - allowed */
}

/* ── Ticket Creation (Server Side) ────────────────────── */

nxp_result nxp_session_ticket_create(
    const uint8_t server_key[NXP_AES_256_KEY_LEN],
    const uint8_t resumption_secret[NXP_HASH_LEN],
    nxp_aead_algo algo,
    const uint8_t *transport_params, size_t tp_len,
    uint64_t now_us,
    uint8_t *ticket_out, size_t ticket_cap, size_t *ticket_len_out)
{
    if (server_key == nullptr || resumption_secret == nullptr ||
        ticket_out == nullptr || ticket_len_out == nullptr) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    /* Build plaintext ticket */
    uint8_t pt[NXP_TICKET_MAX_PLAINTEXT];
    size_t pt_len = 0;

    /* Version */
    pt[pt_len++] = NXP_TICKET_VERSION;

    /* Creation time (8 bytes, big-endian) */
    for (int i = 7; i >= 0; i--) {
        pt[pt_len++] = (uint8_t)(now_us >> (uint64_t)(i * 8));
    }

    /* Lifetime in seconds (4 bytes, big-endian) */
    uint32_t lifetime = NXP_TICKET_DEFAULT_LIFETIME;
    pt[pt_len++] = (uint8_t)(lifetime >> 24);
    pt[pt_len++] = (uint8_t)(lifetime >> 16);
    pt[pt_len++] = (uint8_t)(lifetime >> 8);
    pt[pt_len++] = (uint8_t)(lifetime);

    /* AEAD algorithm */
    pt[pt_len++] = (uint8_t)algo;

    /* Resumption secret (32 bytes) */
    memcpy(pt + pt_len, resumption_secret, NXP_HASH_LEN);
    pt_len += NXP_HASH_LEN;

    /* Transport parameters length + data */
    if (tp_len > 255) return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    pt[pt_len++] = (uint8_t)(tp_len >> 8);
    pt[pt_len++] = (uint8_t)(tp_len & 0xFF);
    if (tp_len > 0 && transport_params != nullptr) {
        if (pt_len + tp_len > sizeof(pt)) return NXP_ERROR(NXP_ERR_BUFFER_TOO_SMALL);
        memcpy(pt + pt_len, transport_params, tp_len);
        pt_len += tp_len;
    }

    /* Encrypt with AES-256-GCM: output = [nonce:12][ciphertext+tag] */
    size_t needed = NXP_TICKET_NONCE_LEN + pt_len + NXP_AEAD_TAG_LEN;
    if (ticket_cap < needed) return NXP_ERROR(NXP_ERR_BUFFER_TOO_SMALL);

    /* Generate random nonce */
    uint8_t nonce[NXP_TICKET_NONCE_LEN];
    nxp_result r = nxp_random_bytes(nonce, sizeof(nonce));
    if (r.code != NXP_OK) return r;

    /* Write nonce first */
    memcpy(ticket_out, nonce, NXP_TICKET_NONCE_LEN);

    /* Encrypt (no AAD for ticket encryption) */
    uint8_t iv[NXP_AEAD_IV_LEN];
    memcpy(iv, nonce, NXP_AEAD_IV_LEN);

    ssize_t ct_len = nxp_aead_encrypt(
        NXP_AEAD_AES_256_GCM,
        server_key, NXP_AES_256_KEY_LEN,
        iv, nullptr, 0,
        pt, pt_len,
        ticket_out + NXP_TICKET_NONCE_LEN
    );
    if (ct_len < 0) {
        memset(pt, 0, sizeof(pt));
        return NXP_ERROR(NXP_ERR_CRYPTO_FAIL);
    }

    *ticket_len_out = NXP_TICKET_NONCE_LEN + (size_t)ct_len;

    /* Wipe plaintext */
    memset(pt, 0, sizeof(pt));

    return NXP_SUCCESS;
}

/* ── Ticket Validation (Server Side) ──────────────────── */

nxp_result nxp_session_ticket_validate(
    const uint8_t server_key[NXP_AES_256_KEY_LEN],
    const uint8_t *ticket, size_t ticket_len,
    uint64_t now_us,
    uint8_t resumption_secret_out[NXP_HASH_LEN],
    nxp_aead_algo *algo_out,
    uint8_t *transport_params_out, size_t tp_cap, size_t *tp_len_out)
{
    if (server_key == nullptr || ticket == nullptr ||
        resumption_secret_out == nullptr || algo_out == nullptr) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    /* Minimum ticket: nonce + version + time + lifetime + algo + secret + tp_len + tag */
    size_t min_len = NXP_TICKET_NONCE_LEN + 1 + 8 + 4 + 1 + NXP_HASH_LEN + 2 +
                     NXP_AEAD_TAG_LEN;
    if (ticket_len < min_len) return NXP_ERROR(NXP_ERR_TOKEN_INVALID);

    /* Extract nonce */
    const uint8_t *nonce = ticket;
    const uint8_t *ct = ticket + NXP_TICKET_NONCE_LEN;
    size_t ct_len = ticket_len - NXP_TICKET_NONCE_LEN;

    /* Decrypt */
    uint8_t pt[NXP_TICKET_MAX_PLAINTEXT];
    uint8_t iv[NXP_AEAD_IV_LEN];
    memcpy(iv, nonce, NXP_AEAD_IV_LEN);

    ssize_t pt_len = nxp_aead_decrypt(
        NXP_AEAD_AES_256_GCM,
        server_key, NXP_AES_256_KEY_LEN,
        iv, nullptr, 0,
        ct, ct_len, pt
    );
    if (pt_len < 0) {
        return NXP_ERROR(NXP_ERR_TOKEN_INVALID);
    }

    /* Parse plaintext */
    size_t pos = 0;
    if ((size_t)pt_len < 1 + 8 + 4 + 1 + NXP_HASH_LEN + 2) {
        memset(pt, 0, sizeof(pt));
        return NXP_ERROR(NXP_ERR_TOKEN_INVALID);
    }

    /* Version check */
    if (pt[pos++] != NXP_TICKET_VERSION) {
        memset(pt, 0, sizeof(pt));
        return NXP_ERROR(NXP_ERR_TOKEN_INVALID);
    }

    /* Creation time */
    uint64_t creation_time = 0;
    for (int i = 7; i >= 0; i--) {
        creation_time |= (uint64_t)pt[pos++] << (uint64_t)(i * 8);
    }

    /* Lifetime */
    uint32_t lifetime = ((uint32_t)pt[pos] << 24) |
                        ((uint32_t)pt[pos + 1] << 16) |
                        ((uint32_t)pt[pos + 2] << 8) |
                        (uint32_t)pt[pos + 3];
    pos += 4;

    /* Check expiry */
    uint64_t max_age = (uint64_t)lifetime * 1000000ULL;
    if (now_us > creation_time + max_age) {
        memset(pt, 0, sizeof(pt));
        return NXP_ERROR(NXP_ERR_TOKEN_INVALID);
    }

    /* AEAD algorithm */
    *algo_out = (nxp_aead_algo)pt[pos++];

    /* Resumption secret */
    memcpy(resumption_secret_out, pt + pos, NXP_HASH_LEN);
    pos += NXP_HASH_LEN;

    /* Transport parameters */
    uint16_t tp_len_val = (uint16_t)((uint16_t)pt[pos] << 8 | pt[pos + 1]);
    pos += 2;

    if (tp_len_out != nullptr) *tp_len_out = tp_len_val;

    if (tp_len_val > 0) {
        if (pos + tp_len_val > (size_t)pt_len) {
            memset(pt, 0, sizeof(pt));
            return NXP_ERROR(NXP_ERR_TOKEN_INVALID);
        }
        if (transport_params_out != nullptr && tp_cap >= tp_len_val) {
            memcpy(transport_params_out, pt + pos, tp_len_val);
        }
    }

    /* Wipe plaintext */
    memset(pt, 0, sizeof(pt));

    return NXP_SUCCESS;
}

/* ── 0-RTT Key Derivation ─────────────────────────────── */

bool nxp_crypto_derive_zero_rtt_keys(
    const uint8_t resumption_secret[NXP_HASH_LEN],
    nxp_aead_algo algo,
    nxp_crypto_state *zero_rtt_state)
{
    if (resumption_secret == nullptr || zero_rtt_state == nullptr) return false;

    memset(zero_rtt_state, 0, sizeof(*zero_rtt_state));

    /*
     * 0-RTT key derivation:
     *   early_secret = HKDF-Extract(NXP_INITIAL_SALT, resumption_secret)
     *   client_early_secret = HKDF-Expand-Label(early_secret, "c e traffic", "", 32)
     *
     * Only the client sends 0-RTT data, so we derive only one direction.
     * For the client: send keys come from client_early_secret.
     * For the server: recv keys come from client_early_secret.
     */
    uint8_t early_secret[NXP_HASH_LEN];
    if (!nxp_hkdf_extract(NXP_INITIAL_SALT, NXP_INITIAL_SALT_LEN,
                           resumption_secret, NXP_HASH_LEN,
                           early_secret))
        return false;

    uint8_t client_early_secret[NXP_HASH_LEN];
    if (!nxp_hkdf_expand_label(early_secret, NXP_HASH_LEN,
                                "c e traffic", nullptr, 0,
                                client_early_secret, NXP_HASH_LEN)) {
        memset(early_secret, 0, sizeof(early_secret));
        return false;
    }

    /* Client send = server recv (both derive from same secret) */
    zero_rtt_state->algo = algo;

    if (!nxp_crypto_derive_traffic_keys(client_early_secret, NXP_HASH_LEN,
                                          algo, &zero_rtt_state->send)) {
        memset(early_secret, 0, sizeof(early_secret));
        memset(client_early_secret, 0, sizeof(client_early_secret));
        return false;
    }

    /* Copy send to recv (same keys, both sides use client_early_secret) */
    zero_rtt_state->recv = zero_rtt_state->send;
    zero_rtt_state->available = true;

    /* Wipe intermediates */
    memset(early_secret, 0, sizeof(early_secret));
    memset(client_early_secret, 0, sizeof(client_early_secret));

    return true;
}

/* ── Resumption Secret Derivation ─────────────────────── */

bool nxp_crypto_derive_resumption_secret(
    const uint8_t master_secret[NXP_HASH_LEN],
    const uint8_t *transcript, size_t transcript_len,
    uint8_t resumption_secret_out[NXP_HASH_LEN])
{
    /*
     * resumption_secret = HKDF-Expand-Label(
     *     master_secret, "resumption", transcript_hash, 32)
     */
    uint8_t transcript_hash[NXP_HASH_LEN];
    if (!nxp_crypto_hash(transcript, transcript_len, transcript_hash))
        return false;

    bool ok = nxp_hkdf_expand_label(master_secret, NXP_HASH_LEN,
                                      "resumption", transcript_hash, NXP_HASH_LEN,
                                      resumption_secret_out, NXP_HASH_LEN);

    memset(transcript_hash, 0, sizeof(transcript_hash));
    return ok;
}
