/*
 * NXP CRC32C - Castagnoli CRC
 *
 * Software implementation of CRC32C (polynomial 0x1EDC6F41).
 * Used for packet integrity checks.
 */
#ifndef NXP_CRC32C_H
#define NXP_CRC32C_H

#include <stddef.h>
#include <stdint.h>

/*
 * Compute CRC32C of the given buffer.
 * Returns the 32-bit checksum.
 */
[[nodiscard]] uint32_t nxp_crc32c(const uint8_t *data, size_t len);

/*
 * Update a running CRC32C with additional data (for incremental computation).
 */
[[nodiscard]] uint32_t nxp_crc32c_update(uint32_t crc, const uint8_t *data, size_t len);

#endif /* NXP_CRC32C_H */
