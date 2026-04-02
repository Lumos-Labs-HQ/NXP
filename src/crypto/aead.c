/*
 * NXP AEAD Encryption - OpenSSL 3.x Backend
 *
 * Phase 5: AES-256-GCM and ChaCha20-Poly1305 authenticated encryption.
 * Used for packet payload protection.
 */
#include "crypto_internal.h"
#include "secure_mem.h"

#include <openssl/evp.h>
#include <string.h>

/* ── Cipher Selection ─────────────────────────────────── */

static const EVP_CIPHER *get_cipher(nxp_aead_algo algo) {
    switch (algo) {
    case NXP_AEAD_AES_256_GCM:       return EVP_aes_256_gcm();
    case NXP_AEAD_CHACHA20_POLY1305: return EVP_chacha20_poly1305();
    }
    return nullptr;
}

/* ── AEAD Encrypt ─────────────────────────────────────── */

ssize_t nxp_aead_encrypt(
    nxp_aead_algo algo,
    const uint8_t *key, uint8_t key_len,
    const uint8_t nonce[NXP_AEAD_IV_LEN],
    const uint8_t *aad, size_t aad_len,
    const uint8_t *plaintext, size_t pt_len,
    uint8_t *ciphertext)
{
    const EVP_CIPHER *cipher = get_cipher(algo);
    if (cipher == nullptr) return -1;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr) return -1;

    ssize_t result = -1;
    int outl = 0;

    if (EVP_EncryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr) != 1)
        goto done;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN,
                             NXP_AEAD_IV_LEN, nullptr) != 1)
        goto done;
    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, nonce) != 1)
        goto done;

    (void)key_len; /* Both algos use 32-byte keys */

    /* AAD (associated data = packet header, not encrypted but authenticated) */
    if (aad_len > 0) {
        if (EVP_EncryptUpdate(ctx, nullptr, &outl, aad, (int)aad_len) != 1)
            goto done;
    }

    /* Encrypt plaintext */
    size_t written = 0;
    if (pt_len > 0) {
        if (EVP_EncryptUpdate(ctx, ciphertext, &outl,
                               plaintext, (int)pt_len) != 1)
            goto done;
        written = (size_t)outl;
    }

    /* Finalize */
    if (EVP_EncryptFinal_ex(ctx, ciphertext + written, &outl) != 1)
        goto done;
    written += (size_t)outl;

    /* Append AEAD tag */
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG,
                             NXP_AEAD_TAG_LEN,
                             ciphertext + written) != 1)
        goto done;
    written += NXP_AEAD_TAG_LEN;

    result = (ssize_t)written;

done:
    EVP_CIPHER_CTX_free(ctx);
    return result;
}

/* ── AEAD Decrypt ─────────────────────────────────────── */

ssize_t nxp_aead_decrypt(
    nxp_aead_algo algo,
    const uint8_t *key, uint8_t key_len,
    const uint8_t nonce[NXP_AEAD_IV_LEN],
    const uint8_t *aad, size_t aad_len,
    const uint8_t *ciphertext, size_t ct_len,
    uint8_t *plaintext)
{
    if (ct_len < NXP_AEAD_TAG_LEN) return -1;

    const EVP_CIPHER *cipher = get_cipher(algo);
    if (cipher == nullptr) return -1;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr) return -1;

    ssize_t result = -1;
    int outl = 0;
    size_t payload_len = ct_len - NXP_AEAD_TAG_LEN;

    if (EVP_DecryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr) != 1)
        goto done;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN,
                             NXP_AEAD_IV_LEN, nullptr) != 1)
        goto done;
    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, nonce) != 1)
        goto done;

    (void)key_len;

    /* AAD */
    if (aad_len > 0) {
        if (EVP_DecryptUpdate(ctx, nullptr, &outl, aad, (int)aad_len) != 1)
            goto done;
    }

    /* Decrypt ciphertext (excluding tag) */
    size_t written = 0;
    if (payload_len > 0) {
        if (EVP_DecryptUpdate(ctx, plaintext, &outl,
                               ciphertext, (int)payload_len) != 1)
            goto done;
        written = (size_t)outl;
    }

    /* Set expected tag */
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG,
                             NXP_AEAD_TAG_LEN,
                             (void *)(ciphertext + payload_len)) != 1)
        goto done;

    /* Verify tag */
    if (EVP_DecryptFinal_ex(ctx, plaintext + written, &outl) != 1) {
        /* Authentication failure - wipe plaintext */
        nxp_secure_zero(plaintext, written);
        goto done;
    }
    written += (size_t)outl;

    result = (ssize_t)written;

done:
    if (result < 0 && plaintext != nullptr) {
        /* Wipe plaintext on any error */
        nxp_secure_zero(plaintext, ct_len);
    }
    EVP_CIPHER_CTX_free(ctx);
    return result;
}
