/*
 * NXP Hardening Tests
 *
 * Phase 10: Tests for proof-of-work, secure memory zeroing,
 * and hash difficulty verification.
 */
#include "test_framework.h"
#include "crypto/proof_of_work.h"
#include "crypto/secure_mem.h"
#include "crypto/crypto_internal.h"
#include <string.h>

/* ── Test: PoW Challenge Generation ────────────────────── */

NXP_TEST(pow_generate_challenge) {
    nxp_pow_challenge ch;
    NXP_ASSERT_OK(nxp_pow_generate_challenge(&ch, 8, 1000000, 5000000));

    NXP_ASSERT_EQ(ch.difficulty, (uint8_t)8);
    NXP_ASSERT_EQ(ch.timestamp_us, (uint64_t)1000000);
    NXP_ASSERT_EQ(ch.expiry_us, (uint64_t)6000000);

    /* Challenge should be non-zero (statistically certain) */
    bool all_zero = true;
    for (int i = 0; i < NXP_POW_CHALLENGE_LEN; i++) {
        if (ch.challenge[i] != 0) { all_zero = false; break; }
    }
    NXP_ASSERT(!all_zero);
}

/* ── Test: PoW Invalid Difficulty ──────────────────────── */

NXP_TEST(pow_invalid_difficulty) {
    nxp_pow_challenge ch;
    nxp_result r = nxp_pow_generate_challenge(&ch, NXP_POW_MAX_DIFFICULTY + 1,
                                               0, 5000000);
    NXP_ASSERT(nxp_result_is_err(r));
}

/* ── Test: PoW Hash Check ──────────────────────────────── */

NXP_TEST(pow_hash_check) {
    /* All zeros hash has maximum leading zeros */
    uint8_t all_zeros[NXP_POW_HASH_LEN];
    memset(all_zeros, 0, sizeof(all_zeros));
    NXP_ASSERT(nxp_pow_check_hash(all_zeros, 0));
    NXP_ASSERT(nxp_pow_check_hash(all_zeros, 8));
    NXP_ASSERT(nxp_pow_check_hash(all_zeros, 16));
    NXP_ASSERT(nxp_pow_check_hash(all_zeros, 32));

    /* 0x00 0x80 ... has exactly 8 leading zero bits */
    uint8_t eight_zeros[NXP_POW_HASH_LEN];
    memset(eight_zeros, 0, sizeof(eight_zeros));
    eight_zeros[1] = 0x80;
    NXP_ASSERT(nxp_pow_check_hash(eight_zeros, 8));
    NXP_ASSERT(!nxp_pow_check_hash(eight_zeros, 9));

    /* 0x00 0x00 0x01 ... has 23 leading zero bits */
    uint8_t twentythree[NXP_POW_HASH_LEN];
    memset(twentythree, 0, sizeof(twentythree));
    twentythree[2] = 0x01;
    NXP_ASSERT(nxp_pow_check_hash(twentythree, 23));
    NXP_ASSERT(!nxp_pow_check_hash(twentythree, 24));

    /* 0x0F ... has 4 leading zero bits */
    uint8_t four_zeros[NXP_POW_HASH_LEN];
    memset(four_zeros, 0xFF, sizeof(four_zeros));
    four_zeros[0] = 0x0F;
    NXP_ASSERT(nxp_pow_check_hash(four_zeros, 4));
    NXP_ASSERT(!nxp_pow_check_hash(four_zeros, 5));

    /* Difficulty 0 always passes */
    uint8_t all_ones[NXP_POW_HASH_LEN];
    memset(all_ones, 0xFF, sizeof(all_ones));
    NXP_ASSERT(nxp_pow_check_hash(all_ones, 0));
}

/* ── Test: PoW Solve + Verify (difficulty 8) ───────────── */

NXP_TEST(pow_solve_and_verify) {
    nxp_pow_challenge ch;
    NXP_ASSERT_OK(nxp_pow_generate_challenge(&ch, 8, 0, 10000000));

    nxp_pow_solution sol;
    NXP_ASSERT_OK(nxp_pow_solve(&ch, &sol, 0));  /* unlimited iterations */

    /* Verify the solution */
    NXP_ASSERT_OK(nxp_pow_verify(&ch, &sol, 5000000));

    /* Verify the hash actually has 8 leading zero bits */
    uint8_t input[NXP_POW_CHALLENGE_LEN + NXP_POW_NONCE_LEN];
    memcpy(input, sol.challenge, NXP_POW_CHALLENGE_LEN);
    memcpy(input + NXP_POW_CHALLENGE_LEN, sol.nonce, NXP_POW_NONCE_LEN);

    uint8_t hash[NXP_POW_HASH_LEN];
    NXP_ASSERT(nxp_crypto_hash(input, sizeof(input), hash));
    NXP_ASSERT(nxp_pow_check_hash(hash, 8));
}

/* ── Test: PoW Solve with Iteration Limit ──────────────── */

NXP_TEST(pow_solve_iteration_limit) {
    nxp_pow_challenge ch;
    NXP_ASSERT_OK(nxp_pow_generate_challenge(&ch, 24, 0, 10000000));

    /* Difficulty 24 needs ~16M hashes on average. Limit to 100. */
    nxp_pow_solution sol;
    nxp_result r = nxp_pow_solve(&ch, &sol, 100);
    /* Almost certainly won't find a solution in 100 iterations */
    NXP_ASSERT_EQ((int)r.code, (int)NXP_ERR_WOULD_BLOCK);
}

/* ── Test: PoW Verify Expired Challenge ────────────────── */

NXP_TEST(pow_verify_expired) {
    nxp_pow_challenge ch;
    NXP_ASSERT_OK(nxp_pow_generate_challenge(&ch, 8, 1000000, 1000000));
    /* Expires at t=2000000 */

    nxp_pow_solution sol;
    NXP_ASSERT_OK(nxp_pow_solve(&ch, &sol, 0));

    /* Verify before expiry - should pass */
    NXP_ASSERT_OK(nxp_pow_verify(&ch, &sol, 1500000));

    /* Verify after expiry - should fail */
    nxp_result r = nxp_pow_verify(&ch, &sol, 3000000);
    NXP_ASSERT_EQ((int)r.code, (int)NXP_ERR_TOKEN_INVALID);
}

/* ── Test: PoW Verify Wrong Challenge ──────────────────── */

NXP_TEST(pow_verify_wrong_challenge) {
    nxp_pow_challenge ch;
    NXP_ASSERT_OK(nxp_pow_generate_challenge(&ch, 8, 0, 10000000));

    nxp_pow_solution sol;
    NXP_ASSERT_OK(nxp_pow_solve(&ch, &sol, 0));

    /* Tamper with the solution's challenge bytes */
    sol.challenge[0] ^= 0xFF;
    nxp_result r = nxp_pow_verify(&ch, &sol, 5000000);
    NXP_ASSERT(nxp_result_is_err(r));
}

/* ── Test: PoW Zero Difficulty (trivial) ───────────────── */

NXP_TEST(pow_zero_difficulty) {
    nxp_pow_challenge ch;
    NXP_ASSERT_OK(nxp_pow_generate_challenge(&ch, 0, 0, 10000000));

    nxp_pow_solution sol;
    NXP_ASSERT_OK(nxp_pow_solve(&ch, &sol, 0));

    /* Nonce should be 0 since difficulty 0 always passes */
    uint64_t nonce_val;
    memcpy(&nonce_val, sol.nonce, sizeof(nonce_val));
    NXP_ASSERT_EQ(nonce_val, (uint64_t)0);

    NXP_ASSERT_OK(nxp_pow_verify(&ch, &sol, 0));
}

/* ── Test: Secure Zero ─────────────────────────────────── */

NXP_TEST(secure_zero) {
    uint8_t buf[64];
    memset(buf, 0xAA, sizeof(buf));

    /* Verify it's non-zero */
    bool all_zero = true;
    for (size_t i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0) { all_zero = false; break; }
    }
    NXP_ASSERT(!all_zero);

    /* Securely zero it */
    nxp_secure_zero(buf, sizeof(buf));

    /* Verify all zero */
    all_zero = true;
    for (size_t i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0) { all_zero = false; break; }
    }
    NXP_ASSERT(all_zero);
}

/* ── Test: Secure Zero Partial ─────────────────────────── */

NXP_TEST(secure_zero_partial) {
    uint8_t buf[32];
    memset(buf, 0xFF, sizeof(buf));

    /* Zero only the first 16 bytes */
    nxp_secure_zero(buf, 16);

    for (int i = 0; i < 16; i++) {
        NXP_ASSERT_EQ(buf[i], (uint8_t)0);
    }
    for (int i = 16; i < 32; i++) {
        NXP_ASSERT_EQ(buf[i], (uint8_t)0xFF);
    }
}

/* ── main ─────────────────────────────────────────────── */

int main(void) {
    printf("=== NXP Hardening Tests (Phase 10) ===\n\n");

    NXP_RUN_TEST(pow_generate_challenge);
    NXP_RUN_TEST(pow_invalid_difficulty);
    NXP_RUN_TEST(pow_hash_check);
    NXP_RUN_TEST(pow_solve_and_verify);
    NXP_RUN_TEST(pow_solve_iteration_limit);
    NXP_RUN_TEST(pow_verify_expired);
    NXP_RUN_TEST(pow_verify_wrong_challenge);
    NXP_RUN_TEST(pow_zero_difficulty);
    NXP_RUN_TEST(secure_zero);
    NXP_RUN_TEST(secure_zero_partial);

    NXP_TEST_SUMMARY();
}
