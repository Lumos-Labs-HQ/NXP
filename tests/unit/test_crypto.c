/*
 * Unit tests: Crypto primitives (Phase 5)
 *
 * Tests AEAD encrypt/decrypt, HKDF key derivation, X25519 key exchange,
 * header protection, initial key derivation, and nonce construction.
 */
#include "test_framework.h"
#include "crypto/crypto_internal.h"
#include "packet_internal.h"
#include <string.h>

/* ── Test: AEAD Encrypt / Decrypt (AES-256-GCM) ──────── */

NXP_TEST(aead_aes_round_trip) {
    uint8_t key[32];
    memset(key, 0x42, sizeof(key));
    uint8_t nonce[NXP_AEAD_IV_LEN];
    memset(nonce, 0x01, sizeof(nonce));

    const uint8_t aad[] = "header data";
    const uint8_t plaintext[] = "Hello, NXP crypto!";
    size_t pt_len = sizeof(plaintext) - 1;

    /* Encrypt */
    uint8_t ciphertext[128];
    ssize_t ct_len = nxp_aead_encrypt(NXP_AEAD_AES_256_GCM,
                                       key, 32, nonce,
                                       aad, sizeof(aad) - 1,
                                       plaintext, pt_len,
                                       ciphertext);
    NXP_ASSERT(ct_len > 0);
    NXP_ASSERT_EQ((size_t)ct_len, pt_len + NXP_AEAD_TAG_LEN);

    /* Ciphertext should differ from plaintext */
    NXP_ASSERT(memcmp(ciphertext, plaintext, pt_len) != 0);

    /* Decrypt */
    uint8_t decrypted[128];
    ssize_t dec_len = nxp_aead_decrypt(NXP_AEAD_AES_256_GCM,
                                        key, 32, nonce,
                                        aad, sizeof(aad) - 1,
                                        ciphertext, (size_t)ct_len,
                                        decrypted);
    NXP_ASSERT(dec_len > 0);
    NXP_ASSERT_EQ((size_t)dec_len, pt_len);
    NXP_ASSERT(memcmp(decrypted, plaintext, pt_len) == 0);
}

/* ── Test: AEAD Encrypt / Decrypt (ChaCha20-Poly1305) ── */

NXP_TEST(aead_chacha_round_trip) {
    uint8_t key[32];
    memset(key, 0xAB, sizeof(key));
    uint8_t nonce[NXP_AEAD_IV_LEN];
    memset(nonce, 0x02, sizeof(nonce));

    const uint8_t plaintext[] = "ChaCha20 test data for NXP";
    size_t pt_len = sizeof(plaintext) - 1;

    uint8_t ciphertext[128];
    ssize_t ct_len = nxp_aead_encrypt(NXP_AEAD_CHACHA20_POLY1305,
                                       key, 32, nonce,
                                       nullptr, 0,
                                       plaintext, pt_len,
                                       ciphertext);
    NXP_ASSERT(ct_len > 0);
    NXP_ASSERT_EQ((size_t)ct_len, pt_len + NXP_AEAD_TAG_LEN);

    uint8_t decrypted[128];
    ssize_t dec_len = nxp_aead_decrypt(NXP_AEAD_CHACHA20_POLY1305,
                                        key, 32, nonce,
                                        nullptr, 0,
                                        ciphertext, (size_t)ct_len,
                                        decrypted);
    NXP_ASSERT(dec_len > 0);
    NXP_ASSERT(memcmp(decrypted, plaintext, pt_len) == 0);
}

/* ── Test: AEAD Tampered Ciphertext Fails ────────────── */

NXP_TEST(aead_tamper_fails) {
    uint8_t key[32], nonce[NXP_AEAD_IV_LEN];
    memset(key, 0x55, sizeof(key));
    memset(nonce, 0x03, sizeof(nonce));

    const uint8_t plaintext[] = "tamper test";
    uint8_t ciphertext[64];
    ssize_t ct_len = nxp_aead_encrypt(NXP_AEAD_AES_256_GCM,
                                       key, 32, nonce,
                                       nullptr, 0,
                                       plaintext, sizeof(plaintext) - 1,
                                       ciphertext);
    NXP_ASSERT(ct_len > 0);

    /* Tamper with ciphertext */
    ciphertext[0] ^= 0xFF;

    uint8_t decrypted[64];
    ssize_t dec_len = nxp_aead_decrypt(NXP_AEAD_AES_256_GCM,
                                        key, 32, nonce,
                                        nullptr, 0,
                                        ciphertext, (size_t)ct_len,
                                        decrypted);
    NXP_ASSERT_EQ(dec_len, (ssize_t)-1); /* Should fail */
}

/* ── Test: HKDF-Extract ──────────────────────────────── */

NXP_TEST(hkdf_extract) {
    const uint8_t salt[] = "test salt";
    const uint8_t ikm[] = "input keying material";

    uint8_t prk[NXP_HASH_LEN];
    bool ok = nxp_hkdf_extract(salt, sizeof(salt) - 1,
                                ikm, sizeof(ikm) - 1, prk);
    NXP_ASSERT(ok);

    /* PRK should be non-zero */
    bool all_zero = true;
    for (int i = 0; i < NXP_HASH_LEN; i++) {
        if (prk[i] != 0) { all_zero = false; break; }
    }
    NXP_ASSERT(!all_zero);

    /* Same inputs should produce same output (deterministic) */
    uint8_t prk2[NXP_HASH_LEN];
    ok = nxp_hkdf_extract(salt, sizeof(salt) - 1,
                           ikm, sizeof(ikm) - 1, prk2);
    NXP_ASSERT(ok);
    NXP_ASSERT(memcmp(prk, prk2, NXP_HASH_LEN) == 0);
}

/* ── Test: HKDF-Expand ───────────────────────────────── */

NXP_TEST(hkdf_expand) {
    uint8_t prk[NXP_HASH_LEN];
    memset(prk, 0x07, sizeof(prk));

    const uint8_t info[] = "test info";
    uint8_t okm[64];
    bool ok = nxp_hkdf_expand(prk, sizeof(prk), info, sizeof(info) - 1,
                               okm, sizeof(okm));
    NXP_ASSERT(ok);

    /* Different output lengths should give different results */
    uint8_t okm2[32];
    ok = nxp_hkdf_expand(prk, sizeof(prk), info, sizeof(info) - 1,
                          okm2, sizeof(okm2));
    NXP_ASSERT(ok);
    /* First 32 bytes should match */
    NXP_ASSERT(memcmp(okm, okm2, 32) == 0);
}

/* ── Test: HKDF-Expand-Label ─────────────────────────── */

NXP_TEST(hkdf_expand_label) {
    uint8_t secret[NXP_HASH_LEN];
    memset(secret, 0x08, sizeof(secret));

    uint8_t out1[32], out2[32];
    bool ok = nxp_hkdf_expand_label(secret, sizeof(secret),
                                     "key", nullptr, 0,
                                     out1, sizeof(out1));
    NXP_ASSERT(ok);

    /* Different label should produce different output */
    ok = nxp_hkdf_expand_label(secret, sizeof(secret),
                                "iv", nullptr, 0,
                                out2, sizeof(out2));
    NXP_ASSERT(ok);
    NXP_ASSERT(memcmp(out1, out2, 32) != 0);
}

/* ── Test: X25519 Keygen ─────────────────────────────── */

NXP_TEST(x25519_keygen) {
    uint8_t pub[NXP_X25519_KEY_LEN], priv[NXP_X25519_KEY_LEN];
    bool ok = nxp_x25519_keygen(pub, priv);
    NXP_ASSERT(ok);

    /* Keys should not be all zeros */
    bool pub_zero = true, priv_zero = true;
    for (int i = 0; i < NXP_X25519_KEY_LEN; i++) {
        if (pub[i] != 0) pub_zero = false;
        if (priv[i] != 0) priv_zero = false;
    }
    NXP_ASSERT(!pub_zero);
    NXP_ASSERT(!priv_zero);

    /* Two keygen calls should produce different keys */
    uint8_t pub2[NXP_X25519_KEY_LEN], priv2[NXP_X25519_KEY_LEN];
    ok = nxp_x25519_keygen(pub2, priv2);
    NXP_ASSERT(ok);
    NXP_ASSERT(memcmp(pub, pub2, NXP_X25519_KEY_LEN) != 0);
}

/* ── Test: X25519 Shared Secret ──────────────────────── */

NXP_TEST(x25519_shared_secret) {
    uint8_t pub_a[NXP_X25519_KEY_LEN], priv_a[NXP_X25519_KEY_LEN];
    uint8_t pub_b[NXP_X25519_KEY_LEN], priv_b[NXP_X25519_KEY_LEN];
    NXP_ASSERT(nxp_x25519_keygen(pub_a, priv_a));
    NXP_ASSERT(nxp_x25519_keygen(pub_b, priv_b));

    /* Both sides should derive the same shared secret */
    uint8_t secret_a[NXP_X25519_KEY_LEN], secret_b[NXP_X25519_KEY_LEN];
    NXP_ASSERT(nxp_x25519_shared_secret(priv_a, pub_b, secret_a));
    NXP_ASSERT(nxp_x25519_shared_secret(priv_b, pub_a, secret_b));
    NXP_ASSERT(memcmp(secret_a, secret_b, NXP_X25519_KEY_LEN) == 0);
}

/* ── Test: Nonce Construction ────────────────────────── */

NXP_TEST(nonce_construction) {
    uint8_t iv[NXP_AEAD_IV_LEN];
    memset(iv, 0xFF, sizeof(iv));

    uint8_t nonce[NXP_AEAD_IV_LEN];
    nxp_crypto_make_nonce(iv, 0, nonce);
    /* pkt_num=0: nonce should equal IV */
    NXP_ASSERT(memcmp(nonce, iv, NXP_AEAD_IV_LEN) == 0);

    /* pkt_num=1: last byte should be XORed */
    nxp_crypto_make_nonce(iv, 1, nonce);
    NXP_ASSERT_EQ(nonce[NXP_AEAD_IV_LEN - 1], (uint8_t)0xFE);
    NXP_ASSERT_EQ(nonce[0], (uint8_t)0xFF); /* High bytes unchanged */
}

/* ── Test: Initial Key Derivation ────────────────────── */

NXP_TEST(initial_key_derivation) {
    nxp_conn_id dcid;
    memset(&dcid, 0, sizeof(dcid));
    dcid.data[0] = 0x83;
    dcid.data[1] = 0x94;
    dcid.len = 8;

    nxp_crypto_state state;
    bool ok = nxp_crypto_derive_initial_keys(&dcid, &state);
    NXP_ASSERT(ok);
    NXP_ASSERT(state.available);
    NXP_ASSERT_EQ((int)state.algo, (int)NXP_AEAD_AES_256_GCM);

    /* Keys should be non-zero and different between send/recv */
    NXP_ASSERT(memcmp(state.send.key, state.recv.key, 32) != 0);
    NXP_ASSERT(memcmp(state.send.iv, state.recv.iv, 12) != 0);
    NXP_ASSERT(memcmp(state.send.hp_key, state.recv.hp_key, 32) != 0);

    /* Deterministic: same DCID -> same keys */
    nxp_crypto_state state2;
    ok = nxp_crypto_derive_initial_keys(&dcid, &state2);
    NXP_ASSERT(ok);
    NXP_ASSERT(memcmp(state.send.key, state2.send.key, 32) == 0);
}

/* ── Test: Header Protection Round-Trip ──────────────── */

NXP_TEST(header_protection) {
    /* Create a fake short header packet */
    uint8_t packet[128];
    memset(packet, 0, sizeof(packet));

    /* Short header: form=0, fixed=1, spin=0, kp=0, rsv=0, pn_len=0 (1-byte PN) */
    packet[0] = 0x40; /* 01000000 */
    /* DCID (8 bytes) */
    memset(packet + 1, 0xAA, 8);
    /* PN = 42 (1 byte) */
    size_t pn_offset = 9;
    packet[pn_offset] = 42;

    /* Fake encrypted payload (need at least 4 + 16 = 20 bytes after PN) */
    for (size_t i = pn_offset + 1; i < pn_offset + 25; i++) {
        packet[i] = (uint8_t)(i & 0xFF);
    }
    size_t pkt_len = pn_offset + 25;

    /* Save original first byte and PN */
    uint8_t orig_first = packet[0];
    uint8_t orig_pn = packet[pn_offset];

    /* Generate HP key */
    uint8_t hp_key[32];
    memset(hp_key, 0x77, sizeof(hp_key));

    /* Apply HP */
    bool ok = nxp_hp_protect(NXP_AEAD_AES_256_GCM, hp_key, 32,
                              packet, pkt_len, pn_offset, 1);
    NXP_ASSERT(ok);

    /* First byte and PN should be modified */
    NXP_ASSERT(packet[0] != orig_first || packet[pn_offset] != orig_pn);

    /* Remove HP */
    uint8_t recovered_pn_len = nxp_hp_unprotect(NXP_AEAD_AES_256_GCM,
                                                  hp_key, 32,
                                                  packet, pkt_len, pn_offset);
    NXP_ASSERT(recovered_pn_len > 0);

    /* Should be back to original values */
    NXP_ASSERT_EQ(packet[0], orig_first);
    NXP_ASSERT_EQ(packet[pn_offset], orig_pn);
}

/* ── Test: SHA-256 Hash ──────────────────────────────── */

NXP_TEST(sha256_hash) {
    const uint8_t data[] = "test data";
    uint8_t hash1[NXP_HASH_LEN], hash2[NXP_HASH_LEN];

    NXP_ASSERT(nxp_crypto_hash(data, sizeof(data) - 1, hash1));
    NXP_ASSERT(nxp_crypto_hash(data, sizeof(data) - 1, hash2));
    NXP_ASSERT(memcmp(hash1, hash2, NXP_HASH_LEN) == 0);

    /* Different data -> different hash */
    const uint8_t data2[] = "other data";
    NXP_ASSERT(nxp_crypto_hash(data2, sizeof(data2) - 1, hash2));
    NXP_ASSERT(memcmp(hash1, hash2, NXP_HASH_LEN) != 0);
}

/* ── Main ──────────────────────────────────────────────── */

int main(void) {
    printf("=== NXP Crypto Tests (Phase 5) ===\n");

    NXP_RUN_TEST(aead_aes_round_trip);
    NXP_RUN_TEST(aead_chacha_round_trip);
    NXP_RUN_TEST(aead_tamper_fails);
    NXP_RUN_TEST(hkdf_extract);
    NXP_RUN_TEST(hkdf_expand);
    NXP_RUN_TEST(hkdf_expand_label);
    NXP_RUN_TEST(x25519_keygen);
    NXP_RUN_TEST(x25519_shared_secret);
    NXP_RUN_TEST(nonce_construction);
    NXP_RUN_TEST(initial_key_derivation);
    NXP_RUN_TEST(header_protection);
    NXP_RUN_TEST(sha256_hash);

    NXP_TEST_SUMMARY();
}
