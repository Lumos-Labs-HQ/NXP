/*
 * NXP Proof of Work - Header
 *
 * Phase 10: Optional SHA-256 based proof-of-work challenge for handshake
 * DoS mitigation. Server sends a challenge with target difficulty;
 * client must find a nonce such that SHA-256(challenge || nonce) has
 * at least `difficulty` leading zero bits.
 *
 * Difficulty levels:
 *   8  = ~256 hashes avg (light, always-on)
 *   16 = ~65536 hashes avg (moderate, under load)
 *   20 = ~1M hashes avg (aggressive, under attack)
 */
#ifndef NXP_PROOF_OF_WORK_H
#define NXP_PROOF_OF_WORK_H

#include "nxp/nxp_error.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ── Constants ────────────────────────────────────────── */

#define NXP_POW_CHALLENGE_LEN   16    /* 128-bit challenge */
#define NXP_POW_NONCE_LEN       8     /* 64-bit nonce */
#define NXP_POW_HASH_LEN        32    /* SHA-256 output */
#define NXP_POW_MAX_DIFFICULTY   32   /* Max leading zero bits */

/* ── Challenge ────────────────────────────────────────── */

typedef struct nxp_pow_challenge {
    uint8_t  challenge[NXP_POW_CHALLENGE_LEN];
    uint8_t  difficulty;   /* Required leading zero bits */
    uint64_t timestamp_us; /* When the challenge was issued */
    uint64_t expiry_us;    /* Challenge expires after this (absolute time) */
} nxp_pow_challenge;

/* ── Solution ─────────────────────────────────────────── */

typedef struct nxp_pow_solution {
    uint8_t  challenge[NXP_POW_CHALLENGE_LEN];
    uint8_t  nonce[NXP_POW_NONCE_LEN];
} nxp_pow_solution;

/* ── API ──────────────────────────────────────────────── */

/*
 * Generate a PoW challenge for the client.
 * difficulty = number of required leading zero bits in SHA-256 hash.
 */
[[nodiscard]] nxp_result nxp_pow_generate_challenge(
    nxp_pow_challenge *out,
    uint8_t difficulty,
    uint64_t now_us,
    uint64_t validity_us   /* How long the challenge is valid */
);

/*
 * Solve a PoW challenge (client-side).
 * Finds a nonce such that SHA-256(challenge || nonce) has the required
 * leading zero bits. max_iterations limits CPU time (0 = unlimited).
 *
 * Returns NXP_OK on success, NXP_ERR_WOULD_BLOCK if max_iterations reached.
 */
[[nodiscard]] nxp_result nxp_pow_solve(
    const nxp_pow_challenge *challenge,
    nxp_pow_solution *solution_out,
    uint64_t max_iterations
);

/*
 * Verify a PoW solution (server-side).
 * Checks that SHA-256(challenge || nonce) has the required zero bits
 * and that the challenge hasn't expired.
 */
[[nodiscard]] nxp_result nxp_pow_verify(
    const nxp_pow_challenge *challenge,
    const nxp_pow_solution *solution,
    uint64_t now_us
);

/*
 * Check if a hash has at least `difficulty` leading zero bits.
 */
[[nodiscard]] bool nxp_pow_check_hash(
    const uint8_t hash[NXP_POW_HASH_LEN],
    uint8_t difficulty
);

#endif /* NXP_PROOF_OF_WORK_H */
