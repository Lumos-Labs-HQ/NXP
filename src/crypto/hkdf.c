/*
 * NXP HKDF - Key Derivation Functions
 *
 * Phase 5: HKDF-Extract, HKDF-Expand, and HKDF-Expand-Label
 * using HMAC-SHA256 via OpenSSL 3.x EVP_MAC API.
 */
#include "crypto_internal.h"

#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <string.h>

/* ── Helper: one-shot HMAC-SHA256 ────────────────────── */

static bool hmac_sha256(
    const uint8_t *key, size_t key_len,
    const uint8_t *data, size_t data_len,
    uint8_t out[NXP_HASH_LEN])
{
    EVP_MAC *mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    if (mac == nullptr) return false;

    EVP_MAC_CTX *ctx = EVP_MAC_CTX_new(mac);
    if (ctx == nullptr) { EVP_MAC_free(mac); return false; }

    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, "SHA256", 0),
        OSSL_PARAM_construct_end()
    };

    bool ok = true;
    if (EVP_MAC_init(ctx, key, key_len, params) != 1) ok = false;
    if (ok && EVP_MAC_update(ctx, data, data_len) != 1) ok = false;

    size_t out_len = 0;
    if (ok && EVP_MAC_final(ctx, out, &out_len, NXP_HASH_LEN) != 1) ok = false;

    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
    return ok && out_len == NXP_HASH_LEN;
}

/* ── Helper: incremental HMAC-SHA256 (for Expand) ───── */

static bool hmac_sha256_multi(
    const uint8_t *key, size_t key_len,
    const uint8_t *parts[], const size_t part_lens[], size_t num_parts,
    uint8_t out[NXP_HASH_LEN])
{
    EVP_MAC *mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    if (mac == nullptr) return false;

    EVP_MAC_CTX *ctx = EVP_MAC_CTX_new(mac);
    if (ctx == nullptr) { EVP_MAC_free(mac); return false; }

    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, "SHA256", 0),
        OSSL_PARAM_construct_end()
    };

    bool ok = true;
    if (EVP_MAC_init(ctx, key, key_len, params) != 1) ok = false;

    for (size_t i = 0; ok && i < num_parts; i++) {
        if (part_lens[i] > 0) {
            if (EVP_MAC_update(ctx, parts[i], part_lens[i]) != 1) ok = false;
        }
    }

    size_t out_len = 0;
    if (ok && EVP_MAC_final(ctx, out, &out_len, NXP_HASH_LEN) != 1) ok = false;

    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
    return ok && out_len == NXP_HASH_LEN;
}

/* ── HKDF-Extract ─────────────────────────────────────── */

bool nxp_hkdf_extract(
    const uint8_t *salt, size_t salt_len,
    const uint8_t *ikm, size_t ikm_len,
    uint8_t prk[NXP_HASH_LEN])
{
    /* If salt is empty, use a string of HashLen zeros */
    uint8_t zero_salt[NXP_HASH_LEN];
    if (salt == nullptr || salt_len == 0) {
        memset(zero_salt, 0, sizeof(zero_salt));
        salt = zero_salt;
        salt_len = NXP_HASH_LEN;
    }

    return hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
}

/* ── HKDF-Expand ──────────────────────────────────────── */

bool nxp_hkdf_expand(
    const uint8_t *prk, size_t prk_len,
    const uint8_t *info, size_t info_len,
    uint8_t *okm, size_t okm_len)
{
    if (okm_len > 255 * NXP_HASH_LEN) return false;

    uint8_t t[NXP_HASH_LEN];
    size_t t_len = 0;
    size_t offset = 0;
    uint8_t counter = 1;

    while (offset < okm_len) {
        /* T(i) = HMAC-SHA256(PRK, T(i-1) || info || counter) */
        const uint8_t *parts[3] = { t, info, &counter };
        size_t part_lens[3] = { t_len, info_len, 1 };

        if (!hmac_sha256_multi(prk, prk_len, parts, part_lens, 3, t))
            return false;

        t_len = NXP_HASH_LEN;
        size_t copy_len = okm_len - offset;
        if (copy_len > NXP_HASH_LEN) copy_len = NXP_HASH_LEN;
        memcpy(okm + offset, t, copy_len);
        offset += copy_len;
        counter++;
    }

    return true;
}

/* ── HKDF-Expand-Label ────────────────────────────────── */

bool nxp_hkdf_expand_label(
    const uint8_t *secret, size_t secret_len,
    const char *label,
    const uint8_t *context, size_t context_len,
    uint8_t *out, size_t out_len)
{
    /*
     * NXP HkdfLabel format (like TLS 1.3 but with "nxp1 " prefix):
     *   uint16 length
     *   uint8  label_length
     *   opaque label[label_length] = "nxp1 " + Label
     *   uint8  context_length
     *   opaque context[context_length]
     */
    const char *prefix = "nxp1 ";
    size_t prefix_len = 5;
    size_t label_str_len = strlen(label);
    size_t full_label_len = prefix_len + label_str_len;

    if (full_label_len > 255 || context_len > 255 || out_len > 65535) {
        return false;
    }

    /* Build the info buffer */
    uint8_t info[512];
    size_t info_len = 0;

    /* uint16 length (big-endian) */
    info[info_len++] = (uint8_t)(out_len >> 8);
    info[info_len++] = (uint8_t)(out_len & 0xFF);

    /* uint8 label_length + "nxp1 " + label */
    info[info_len++] = (uint8_t)full_label_len;
    memcpy(info + info_len, prefix, prefix_len);
    info_len += prefix_len;
    memcpy(info + info_len, label, label_str_len);
    info_len += label_str_len;

    /* uint8 context_length + context */
    info[info_len++] = (uint8_t)context_len;
    if (context_len > 0 && context != nullptr) {
        memcpy(info + info_len, context, context_len);
        info_len += context_len;
    }

    return nxp_hkdf_expand(secret, secret_len, info, info_len, out, out_len);
}

/* ── SHA-256 Hash ─────────────────────────────────────── */

bool nxp_crypto_hash(
    const uint8_t *data, size_t data_len,
    uint8_t out[NXP_HASH_LEN])
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == nullptr) return false;

    bool ok = true;
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) ok = false;
    if (ok && EVP_DigestUpdate(ctx, data, data_len) != 1) ok = false;

    unsigned int out_len = 0;
    if (ok && EVP_DigestFinal_ex(ctx, out, &out_len) != 1) ok = false;

    EVP_MD_CTX_free(ctx);
    return ok && out_len == NXP_HASH_LEN;
}
