/*
 * NXP Handshake Crypto - Key Exchange and Key Schedule
 *
 * Phase 5: X25519 key generation, shared secret computation,
 * initial key derivation (from CID), and traffic key derivation.
 */
#include "crypto_internal.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <string.h>

/* ── Protocol Constants ───────────────────────────────── */

/* NXP initial salt: "NXP1-initial-salt-v1" in ASCII */
const uint8_t NXP_INITIAL_SALT[NXP_INITIAL_SALT_LEN] = {
    0x4e, 0x58, 0x50, 0x31, 0x2d, 0x69, 0x6e, 0x69, 0x74, 0x69,
    0x61, 0x6c, 0x2d, 0x73, 0x61, 0x6c, 0x74, 0x2d, 0x76, 0x31
};

/* ── X25519 Key Exchange ──────────────────────────────── */

bool nxp_x25519_keygen(
    uint8_t public_key[NXP_X25519_KEY_LEN],
    uint8_t private_key[NXP_X25519_KEY_LEN])
{
    EVP_PKEY *pkey = nullptr;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    if (ctx == nullptr) return false;

    bool ok = false;

    if (EVP_PKEY_keygen_init(ctx) != 1) goto done;
    if (EVP_PKEY_keygen(ctx, &pkey) != 1) goto done;

    /* Extract raw private key */
    size_t priv_len = NXP_X25519_KEY_LEN;
    if (EVP_PKEY_get_raw_private_key(pkey, private_key, &priv_len) != 1)
        goto done;

    /* Extract raw public key */
    size_t pub_len = NXP_X25519_KEY_LEN;
    if (EVP_PKEY_get_raw_public_key(pkey, public_key, &pub_len) != 1)
        goto done;

    ok = true;

done:
    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(ctx);
    return ok;
}

bool nxp_x25519_shared_secret(
    const uint8_t private_key[NXP_X25519_KEY_LEN],
    const uint8_t peer_public[NXP_X25519_KEY_LEN],
    uint8_t shared_secret[NXP_X25519_KEY_LEN])
{
    EVP_PKEY *local_key = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_X25519, nullptr, private_key, NXP_X25519_KEY_LEN);
    if (local_key == nullptr) return false;

    EVP_PKEY *peer_key = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_X25519, nullptr, peer_public, NXP_X25519_KEY_LEN);
    if (peer_key == nullptr) {
        EVP_PKEY_free(local_key);
        return false;
    }

    bool ok = false;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(local_key, nullptr);
    if (ctx == nullptr) goto done;

    if (EVP_PKEY_derive_init(ctx) != 1) goto done;
    if (EVP_PKEY_derive_set_peer(ctx, peer_key) != 1) goto done;

    size_t secret_len = NXP_X25519_KEY_LEN;
    if (EVP_PKEY_derive(ctx, shared_secret, &secret_len) != 1) goto done;

    ok = (secret_len == NXP_X25519_KEY_LEN);

done:
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(peer_key);
    EVP_PKEY_free(local_key);
    return ok;
}

/* ── Nonce Construction ───────────────────────────────── */

void nxp_crypto_make_nonce(
    const uint8_t iv[NXP_AEAD_IV_LEN],
    uint64_t pkt_num,
    uint8_t nonce[NXP_AEAD_IV_LEN])
{
    memcpy(nonce, iv, NXP_AEAD_IV_LEN);

    /* XOR packet number into the rightmost bytes (big-endian) */
    for (int i = 0; i < 8; i++) {
        nonce[NXP_AEAD_IV_LEN - 1 - i] ^= (uint8_t)(pkt_num >> (8 * i));
    }
}

/* ── Traffic Key Derivation ───────────────────────────── */

bool nxp_crypto_derive_traffic_keys(
    const uint8_t *secret, size_t secret_len,
    nxp_aead_algo algo,
    nxp_crypto_keys *out)
{
    uint8_t klen = nxp_aead_key_len(algo);

    /* key = HKDF-Expand-Label(secret, "key", "", key_len) */
    if (!nxp_hkdf_expand_label(secret, secret_len, "key", nullptr, 0,
                                out->key, klen))
        return false;

    /* iv = HKDF-Expand-Label(secret, "iv", "", 12) */
    if (!nxp_hkdf_expand_label(secret, secret_len, "iv", nullptr, 0,
                                out->iv, NXP_AEAD_IV_LEN))
        return false;

    /* hp = HKDF-Expand-Label(secret, "hp", "", key_len) */
    if (!nxp_hkdf_expand_label(secret, secret_len, "hp", nullptr, 0,
                                out->hp_key, klen))
        return false;

    out->key_len = klen;
    return true;
}

/* ── Initial Key Derivation ───────────────────────────── */

bool nxp_crypto_derive_initial_keys(
    const nxp_conn_id *dcid,
    nxp_crypto_state *state)
{
    /* initial_secret = HKDF-Extract(initial_salt, dcid) */
    uint8_t initial_secret[NXP_HASH_LEN];
    if (!nxp_hkdf_extract(NXP_INITIAL_SALT, NXP_INITIAL_SALT_LEN,
                           dcid->data, dcid->len,
                           initial_secret))
        return false;

    /* client_initial_secret = HKDF-Expand-Label(initial_secret, "client in", "", 32) */
    uint8_t client_secret[NXP_HASH_LEN];
    if (!nxp_hkdf_expand_label(initial_secret, NXP_HASH_LEN,
                                "client in", nullptr, 0,
                                client_secret, NXP_HASH_LEN))
        goto fail;

    /* server_initial_secret = HKDF-Expand-Label(initial_secret, "server in", "", 32) */
    uint8_t server_secret[NXP_HASH_LEN];
    if (!nxp_hkdf_expand_label(initial_secret, NXP_HASH_LEN,
                                "server in", nullptr, 0,
                                server_secret, NXP_HASH_LEN))
        goto fail;

    state->algo = NXP_AEAD_AES_256_GCM;

    /* Client sends to server: client keys for send (client) / recv (server).
     * The 'send' keys here mean "keys used to protect packets sent by the
     * side that is the client". So:
     *   send = client_secret derived keys (for client sending)
     *   recv = server_secret derived keys (for server sending / client receiving)
     *
     * Caller is responsible for swapping send/recv based on perspective. */

    if (!nxp_crypto_derive_traffic_keys(client_secret, NXP_HASH_LEN,
                                         state->algo, &state->send))
        goto fail;

    if (!nxp_crypto_derive_traffic_keys(server_secret, NXP_HASH_LEN,
                                         state->algo, &state->recv))
        goto fail;

    state->available = true;

    /* Wipe intermediaries */
    memset(initial_secret, 0, sizeof(initial_secret));
    memset(client_secret, 0, sizeof(client_secret));
    memset(server_secret, 0, sizeof(server_secret));
    return true;

fail:
    memset(initial_secret, 0, sizeof(initial_secret));
    memset(client_secret, 0, sizeof(client_secret));
    memset(server_secret, 0, sizeof(server_secret));
    return false;
}
