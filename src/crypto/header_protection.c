/*
 * NXP Header Protection
 *
 * Phase 5: Masks and unmasks the first byte + packet number bytes
 * of a packet to prevent middlebox ossification.
 *
 * AES-based HP:  mask = AES-ECB(hp_key, sample)
 * ChaCha20 HP:   mask = ChaCha20(hp_key, counter=sample[0..3], nonce=sample[4..15], zeros)
 */
#include "crypto_internal.h"
#include "core/packet_internal.h"

#include <openssl/evp.h>
#include <string.h>

/* ── HP Mask Generation ───────────────────────────────── */

#define NXP_HP_SAMPLE_LEN 16
#define NXP_HP_MASK_LEN   5

static bool generate_hp_mask(
    nxp_aead_algo algo,
    const uint8_t *hp_key, uint8_t key_len,
    const uint8_t sample[NXP_HP_SAMPLE_LEN],
    uint8_t mask[NXP_HP_MASK_LEN])
{
    if (algo == NXP_AEAD_AES_256_GCM) {
        /* AES-ECB encrypt the sample to get the mask */
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        if (ctx == nullptr) return false;

        bool ok = false;
        const EVP_CIPHER *cipher = EVP_aes_256_ecb();
        int outl = 0;

        if (EVP_EncryptInit_ex(ctx, cipher, nullptr, hp_key, nullptr) != 1) goto aes_done;
        EVP_CIPHER_CTX_set_padding(ctx, 0); /* No padding for ECB */

        uint8_t block[16];
        if (EVP_EncryptUpdate(ctx, block, &outl, sample, 16) != 1) goto aes_done;
        memcpy(mask, block, NXP_HP_MASK_LEN);
        ok = true;

    aes_done:
        EVP_CIPHER_CTX_free(ctx);
        (void)key_len;
        return ok;

    } else if (algo == NXP_AEAD_CHACHA20_POLY1305) {
        /* ChaCha20: counter = sample[0..3], nonce = sample[4..15] */
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        if (ctx == nullptr) return false;

        bool ok = false;
        uint8_t zeros[NXP_HP_MASK_LEN];
        memset(zeros, 0, sizeof(zeros));

        /* Build the IV: 4 bytes counter (sample[0..3]) + 12 bytes nonce implicit
         * Actually for ChaCha20 in OpenSSL, the IV is 16 bytes = counter(4) + nonce(12).
         * We use sample[0..3] as counter, sample[4..15] as nonce. */
        uint8_t iv[16];
        memcpy(iv, sample, 16); /* counter(4) || nonce(12) */

        int outl = 0;
        if (EVP_EncryptInit_ex(ctx, EVP_chacha20(), nullptr, hp_key, iv) != 1)
            goto cc_done;
        if (EVP_EncryptUpdate(ctx, mask, &outl, zeros, NXP_HP_MASK_LEN) != 1)
            goto cc_done;
        ok = true;

    cc_done:
        EVP_CIPHER_CTX_free(ctx);
        (void)key_len;
        return ok;
    }

    return false;
}

/* ── Apply Header Protection ──────────────────────────── */

bool nxp_hp_protect(
    nxp_aead_algo algo,
    const uint8_t *hp_key, uint8_t key_len,
    uint8_t *packet, size_t pkt_len,
    size_t pn_offset, uint8_t pn_len)
{
    /* Sample starts 4 bytes after the packet number start */
    size_t sample_offset = pn_offset + 4;
    if (sample_offset + NXP_HP_SAMPLE_LEN > pkt_len) return false;

    uint8_t mask[NXP_HP_MASK_LEN];
    if (!generate_hp_mask(algo, hp_key, key_len,
                          packet + sample_offset, mask))
        return false;

    /* Mask the first byte */
    bool is_long = (packet[0] & NXP_PKT_FORM_BIT) != 0;
    if (is_long) {
        packet[0] ^= mask[0] & 0x0F; /* Long header: mask lower 4 bits */
    } else {
        packet[0] ^= mask[0] & 0x1F; /* Short header: mask lower 5 bits */
    }

    /* Mask the packet number bytes */
    for (uint8_t i = 0; i < pn_len; i++) {
        packet[pn_offset + i] ^= mask[1 + i];
    }

    return true;
}

/* ── Remove Header Protection ─────────────────────────── */

uint8_t nxp_hp_unprotect(
    nxp_aead_algo algo,
    const uint8_t *hp_key, uint8_t key_len,
    uint8_t *packet, size_t pkt_len,
    size_t pn_offset)
{
    /* Sample starts 4 bytes after the packet number start
     * (we always sample as if pn_len is 4, the maximum) */
    size_t sample_offset = pn_offset + 4;
    if (sample_offset + NXP_HP_SAMPLE_LEN > pkt_len) return 0;

    uint8_t mask[NXP_HP_MASK_LEN];
    if (!generate_hp_mask(algo, hp_key, key_len,
                          packet + sample_offset, mask))
        return 0;

    /* Unmask the first byte to read the real PN length */
    bool is_long = (packet[0] & NXP_PKT_FORM_BIT) != 0;
    if (is_long) {
        packet[0] ^= mask[0] & 0x0F;
    } else {
        packet[0] ^= mask[0] & 0x1F;
    }

    /* Read PN length from the now-clear first byte */
    uint8_t pn_len = (packet[0] & 0x03) + 1;

    /* Unmask the packet number bytes */
    for (uint8_t i = 0; i < pn_len; i++) {
        packet[pn_offset + i] ^= mask[1 + i];
    }

    return pn_len;
}
