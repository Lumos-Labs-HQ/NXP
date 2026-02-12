/*
 * NXP Crypto Engine - Internal Header
 *
 * Phase 5: AEAD encryption, HKDF key derivation, X25519 key exchange,
 * header protection, and key schedule for the NXP handshake.
 *
 * Uses OpenSSL 3.x as the cryptographic backend.
 */
#ifndef NXP_CRYPTO_INTERNAL_H
#define NXP_CRYPTO_INTERNAL_H

#include "nxp/nxp_types.h"
#include "nxp/nxp_error.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#else
#include <sys/types.h>
#endif

/* ── Constants ────────────────────────────────────────── */

#define NXP_AES_256_KEY_LEN   32
#define NXP_CHACHA20_KEY_LEN  32
#define NXP_AEAD_IV_LEN       12
#define NXP_AEAD_MAX_KEY_LEN  32
#define NXP_HP_KEY_LEN        32
#define NXP_HASH_LEN          32   /* SHA-256 output */
#define NXP_X25519_KEY_LEN    32

/* Protocol-specific initial salt: "NXP1-initial-salt-v1" */
#define NXP_INITIAL_SALT_LEN  20
extern const uint8_t NXP_INITIAL_SALT[NXP_INITIAL_SALT_LEN];

/* ── AEAD Algorithm ───────────────────────────────────── */

typedef enum nxp_aead_algo {
    NXP_AEAD_AES_256_GCM       = 0,
    NXP_AEAD_CHACHA20_POLY1305 = 1,
} nxp_aead_algo;

/* Key length for a given algorithm */
static inline uint8_t nxp_aead_key_len(nxp_aead_algo algo) {
    (void)algo;
    return 32; /* Both AES-256-GCM and ChaCha20-Poly1305 use 32-byte keys */
}

/* ── Key Material ─────────────────────────────────────── */

typedef struct nxp_crypto_keys {
    uint8_t key[NXP_AEAD_MAX_KEY_LEN];
    uint8_t iv[NXP_AEAD_IV_LEN];
    uint8_t hp_key[NXP_HP_KEY_LEN];
    uint8_t key_len;
} nxp_crypto_keys;

/* Crypto state for a packet number space (one direction pair) */
typedef struct nxp_crypto_state {
    nxp_crypto_keys send;
    nxp_crypto_keys recv;
    nxp_aead_algo   algo;
    bool            available;
} nxp_crypto_state;

/* ── AEAD API ─────────────────────────────────────────── */

/*
 * Encrypt plaintext with AEAD.
 * ciphertext buffer must hold pt_len + NXP_AEAD_TAG_LEN bytes.
 * Returns total ciphertext length (pt_len + tag), or -1 on error.
 */
[[nodiscard]] ssize_t nxp_aead_encrypt(
    nxp_aead_algo algo,
    const uint8_t *key, uint8_t key_len,
    const uint8_t nonce[NXP_AEAD_IV_LEN],
    const uint8_t *aad, size_t aad_len,
    const uint8_t *plaintext, size_t pt_len,
    uint8_t *ciphertext
);

/*
 * Decrypt ciphertext with AEAD.
 * ct_len includes the AEAD tag (16 bytes).
 * Returns plaintext length (ct_len - tag), or -1 on auth failure.
 */
[[nodiscard]] ssize_t nxp_aead_decrypt(
    nxp_aead_algo algo,
    const uint8_t *key, uint8_t key_len,
    const uint8_t nonce[NXP_AEAD_IV_LEN],
    const uint8_t *aad, size_t aad_len,
    const uint8_t *ciphertext, size_t ct_len,
    uint8_t *plaintext
);

/* ── HKDF API ─────────────────────────────────────────── */

/* HKDF-Extract: PRK = HMAC-SHA256(salt, IKM) */
[[nodiscard]] bool nxp_hkdf_extract(
    const uint8_t *salt, size_t salt_len,
    const uint8_t *ikm, size_t ikm_len,
    uint8_t prk[NXP_HASH_LEN]
);

/* HKDF-Expand: OKM = HKDF-Expand(PRK, info, L) */
[[nodiscard]] bool nxp_hkdf_expand(
    const uint8_t *prk, size_t prk_len,
    const uint8_t *info, size_t info_len,
    uint8_t *okm, size_t okm_len
);

/*
 * HKDF-Expand-Label (NXP protocol label format):
 *   HkdfLabel = uint16(length) || uint8(len("nxp1 "+label)) ||
 *               "nxp1 " || label || uint8(context_len) || context
 */
[[nodiscard]] bool nxp_hkdf_expand_label(
    const uint8_t *secret, size_t secret_len,
    const char *label,
    const uint8_t *context, size_t context_len,
    uint8_t *out, size_t out_len
);

/* ── X25519 API ───────────────────────────────────────── */

/* Generate an X25519 keypair */
[[nodiscard]] bool nxp_x25519_keygen(
    uint8_t public_key[NXP_X25519_KEY_LEN],
    uint8_t private_key[NXP_X25519_KEY_LEN]
);

/* Compute shared secret from local private + peer public */
[[nodiscard]] bool nxp_x25519_shared_secret(
    const uint8_t private_key[NXP_X25519_KEY_LEN],
    const uint8_t peer_public[NXP_X25519_KEY_LEN],
    uint8_t shared_secret[NXP_X25519_KEY_LEN]
);

/* ── Key Derivation ───────────────────────────────────── */

/*
 * Derive initial keys from destination connection ID.
 * Both client and server keys are derived.
 * Algorithm is always AES-256-GCM for initial packets.
 */
[[nodiscard]] bool nxp_crypto_derive_initial_keys(
    const nxp_conn_id *dcid,
    nxp_crypto_state *initial_state
);

/*
 * Derive AEAD key + IV + HP key from a directional traffic secret.
 */
[[nodiscard]] bool nxp_crypto_derive_traffic_keys(
    const uint8_t *secret, size_t secret_len,
    nxp_aead_algo algo,
    nxp_crypto_keys *out
);

/* Construct AEAD nonce: IV XOR left-padded packet number */
void nxp_crypto_make_nonce(
    const uint8_t iv[NXP_AEAD_IV_LEN],
    uint64_t pkt_num,
    uint8_t nonce[NXP_AEAD_IV_LEN]
);

/* ── Header Protection ────────────────────────────────── */

/*
 * Apply header protection after payload encryption.
 * Masks the first byte (flag bits) and packet number bytes.
 * The packet buffer must contain: header || encrypted_payload.
 */
[[nodiscard]] bool nxp_hp_protect(
    nxp_aead_algo algo,
    const uint8_t *hp_key, uint8_t key_len,
    uint8_t *packet, size_t pkt_len,
    size_t pn_offset, uint8_t pn_len
);

/*
 * Remove header protection before payload decryption.
 * Unmasks the first byte and packet number bytes.
 * Returns the unmasked packet number length (1-4), or 0 on error.
 */
[[nodiscard]] uint8_t nxp_hp_unprotect(
    nxp_aead_algo algo,
    const uint8_t *hp_key, uint8_t key_len,
    uint8_t *packet, size_t pkt_len,
    size_t pn_offset
);

/* ── Utility ──────────────────────────────────────────── */

/* SHA-256 hash */
[[nodiscard]] bool nxp_crypto_hash(
    const uint8_t *data, size_t data_len,
    uint8_t out[NXP_HASH_LEN]
);

#endif /* NXP_CRYPTO_INTERNAL_H */
