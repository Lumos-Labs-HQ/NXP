/*
 * NXP Random Number Generation
 *
 * Cryptographically secure random for connection IDs, nonces, etc.
 * Uses OS-provided randomness (CryptGenRandom on Windows, /dev/urandom on Linux).
 */
#ifndef NXP_RANDOM_H
#define NXP_RANDOM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "nxp/nxp_error.h"

/* Fill buffer with cryptographically secure random bytes */
[[nodiscard]] nxp_result nxp_random_bytes(uint8_t *buf, size_t len);

/* Generate a random uint64 */
[[nodiscard]] uint64_t nxp_random_u64(void);

/* Generate a random uint32 */
[[nodiscard]] uint32_t nxp_random_u32(void);

#endif /* NXP_RANDOM_H */
