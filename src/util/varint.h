/*
 * NXP Variable-Length Integer Encoding
 *
 * Uses QUIC RFC 9000 encoding: 2-bit prefix determines length (1/2/4/8 bytes).
 * Max value: 2^62 - 1.
 */
#ifndef NXP_VARINT_H
#define NXP_VARINT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define NXP_VARINT_MAX ((uint64_t)4611686018427387903ULL)  /* 2^62 - 1 */

/* Encode a varint into buf. Returns bytes written (1/2/4/8) or 0 on error. */
[[nodiscard]] size_t nxp_varint_encode(uint64_t value, uint8_t *buf, size_t buf_len);

/* Decode a varint from buf. Returns bytes consumed (1/2/4/8) or 0 on error.
 * The decoded value is written to *out. */
[[nodiscard]] size_t nxp_varint_decode(const uint8_t *buf, size_t buf_len, uint64_t *out);

/* Get the encoded length of a value without encoding it */
[[nodiscard]] static inline size_t nxp_varint_len(uint64_t value) {
    if (value <= 63)              return 1;
    if (value <= 16383)           return 2;
    if (value <= 1073741823)      return 4;
    if (value <= NXP_VARINT_MAX)  return 8;
    return 0; /* Value too large */
}

#endif /* NXP_VARINT_H */
