/*
 * NXP Proof of Work - Implementation
 *
 * Phase 10: SHA-256 based PoW for handshake DoS mitigation.
 *
 * The hash input is: challenge_bytes[16] || nonce_bytes[8] (24 bytes).
 * The solver increments the nonce until the hash has enough leading zeros.
 */
#include "proof_of_work.h"
#include "crypto_internal.h"
#include "util/random.h"

#include <string.h>

/* ── Leading Zero Bit Count ───────────────────────────── */

bool nxp_pow_check_hash(const uint8_t hash[NXP_POW_HASH_LEN],
                         uint8_t difficulty) {
    if (difficulty == 0) return true;
    if (difficulty > NXP_POW_MAX_DIFFICULTY) return false;

    /* Check full zero bytes first */
    uint8_t full_bytes = difficulty / 8;
    uint8_t remaining_bits = difficulty % 8;

    for (uint8_t i = 0; i < full_bytes; i++) {
        if (hash[i] != 0) return false;
    }

    /* Check partial byte */
    if (remaining_bits > 0 && full_bytes < NXP_POW_HASH_LEN) {
        uint8_t mask = (uint8_t)(0xFF << (8 - remaining_bits));
        if ((hash[full_bytes] & mask) != 0) return false;
    }

    return true;
}

/* ── Generate Challenge ───────────────────────────────── */

nxp_result nxp_pow_generate_challenge(nxp_pow_challenge *out,
                                       uint8_t difficulty,
                                       uint64_t now_us,
                                       uint64_t validity_us) {
    if (out == nullptr) return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    if (difficulty > NXP_POW_MAX_DIFFICULTY) return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);

    memset(out, 0, sizeof(*out));

    /* Generate random challenge bytes */
    nxp_result r = nxp_random_bytes(out->challenge, NXP_POW_CHALLENGE_LEN);
    if (nxp_result_is_err(r)) return r;

    out->difficulty = difficulty;
    out->timestamp_us = now_us;
    out->expiry_us = now_us + validity_us;

    return NXP_SUCCESS;
}

/* ── Solve Challenge ──────────────────────────────────── */

nxp_result nxp_pow_solve(const nxp_pow_challenge *challenge,
                          nxp_pow_solution *solution_out,
                          uint64_t max_iterations) {
    if (challenge == nullptr || solution_out == nullptr) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    /* Copy challenge to solution */
    memcpy(solution_out->challenge, challenge->challenge, NXP_POW_CHALLENGE_LEN);

    /* Prepare hash input: challenge || nonce */
    uint8_t input[NXP_POW_CHALLENGE_LEN + NXP_POW_NONCE_LEN];
    memcpy(input, challenge->challenge, NXP_POW_CHALLENGE_LEN);

    uint64_t nonce = 0;
    uint64_t iterations = 0;

    while (true) {
        /* Set nonce in input buffer (little-endian) */
        memcpy(input + NXP_POW_CHALLENGE_LEN, &nonce, NXP_POW_NONCE_LEN);

        /* Compute SHA-256 */
        uint8_t hash[NXP_POW_HASH_LEN];
        if (!nxp_crypto_hash(input, sizeof(input), hash)) {
            return NXP_ERROR(NXP_ERR_CRYPTO_FAIL);
        }

        /* Check difficulty */
        if (nxp_pow_check_hash(hash, challenge->difficulty)) {
            memcpy(solution_out->nonce, &nonce, NXP_POW_NONCE_LEN);
            return NXP_SUCCESS;
        }

        nonce++;
        iterations++;

        if (max_iterations > 0 && iterations >= max_iterations) {
            return NXP_ERROR(NXP_ERR_WOULD_BLOCK);
        }
    }
}

/* ── Verify Solution ──────────────────────────────────── */

nxp_result nxp_pow_verify(const nxp_pow_challenge *challenge,
                           const nxp_pow_solution *solution,
                           uint64_t now_us) {
    if (challenge == nullptr || solution == nullptr) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    /* Check challenge match */
    if (memcmp(challenge->challenge, solution->challenge,
               NXP_POW_CHALLENGE_LEN) != 0) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    /* Check expiry */
    if (now_us > challenge->expiry_us) {
        return NXP_ERROR(NXP_ERR_TOKEN_INVALID);
    }

    /* Compute hash and verify difficulty */
    uint8_t input[NXP_POW_CHALLENGE_LEN + NXP_POW_NONCE_LEN];
    memcpy(input, solution->challenge, NXP_POW_CHALLENGE_LEN);
    memcpy(input + NXP_POW_CHALLENGE_LEN, solution->nonce, NXP_POW_NONCE_LEN);

    uint8_t hash[NXP_POW_HASH_LEN];
    if (!nxp_crypto_hash(input, sizeof(input), hash)) {
        return NXP_ERROR(NXP_ERR_CRYPTO_FAIL);
    }

    if (!nxp_pow_check_hash(hash, challenge->difficulty)) {
        return NXP_ERROR(NXP_ERR_CRYPTO_FAIL);
    }

    return NXP_SUCCESS;
}
