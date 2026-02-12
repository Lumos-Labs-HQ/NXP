/*
 * NXP Variable-Length Integer - Implementation
 *
 * Encoding (QUIC RFC 9000 Section 16):
 *   1 byte:  prefix 00, 6 bits of value
 *   2 bytes: prefix 01, 14 bits of value
 *   4 bytes: prefix 10, 30 bits of value
 *   8 bytes: prefix 11, 62 bits of value
 */
#include "varint.h"

size_t nxp_varint_encode(uint64_t value, uint8_t *buf, size_t buf_len) {
    if (value <= 63) {
        if (buf_len < 1) return 0;
        buf[0] = (uint8_t)value;
        return 1;
    }
    if (value <= 16383) {
        if (buf_len < 2) return 0;
        buf[0] = (uint8_t)(0x40 | (value >> 8));
        buf[1] = (uint8_t)(value & 0xFF);
        return 2;
    }
    if (value <= 1073741823) {
        if (buf_len < 4) return 0;
        buf[0] = (uint8_t)(0x80 | (value >> 24));
        buf[1] = (uint8_t)((value >> 16) & 0xFF);
        buf[2] = (uint8_t)((value >> 8) & 0xFF);
        buf[3] = (uint8_t)(value & 0xFF);
        return 4;
    }
    if (value <= NXP_VARINT_MAX) {
        if (buf_len < 8) return 0;
        buf[0] = (uint8_t)(0xC0 | (value >> 56));
        buf[1] = (uint8_t)((value >> 48) & 0xFF);
        buf[2] = (uint8_t)((value >> 40) & 0xFF);
        buf[3] = (uint8_t)((value >> 32) & 0xFF);
        buf[4] = (uint8_t)((value >> 24) & 0xFF);
        buf[5] = (uint8_t)((value >> 16) & 0xFF);
        buf[6] = (uint8_t)((value >> 8) & 0xFF);
        buf[7] = (uint8_t)(value & 0xFF);
        return 8;
    }
    return 0; /* Value too large */
}

size_t nxp_varint_decode(const uint8_t *buf, size_t buf_len, uint64_t *out) {
    if (buf_len == 0 || out == nullptr) {
        return 0;
    }

    uint8_t prefix = buf[0] >> 6;

    switch (prefix) {
    case 0: /* 1 byte */
        *out = buf[0] & 0x3F;
        return 1;

    case 1: /* 2 bytes */
        if (buf_len < 2) return 0;
        *out = ((uint64_t)(buf[0] & 0x3F) << 8) |
               (uint64_t)buf[1];
        return 2;

    case 2: /* 4 bytes */
        if (buf_len < 4) return 0;
        *out = ((uint64_t)(buf[0] & 0x3F) << 24) |
               ((uint64_t)buf[1] << 16) |
               ((uint64_t)buf[2] << 8) |
               (uint64_t)buf[3];
        return 4;

    case 3: /* 8 bytes */
        if (buf_len < 8) return 0;
        *out = ((uint64_t)(buf[0] & 0x3F) << 56) |
               ((uint64_t)buf[1] << 48) |
               ((uint64_t)buf[2] << 40) |
               ((uint64_t)buf[3] << 32) |
               ((uint64_t)buf[4] << 24) |
               ((uint64_t)buf[5] << 16) |
               ((uint64_t)buf[6] << 8) |
               (uint64_t)buf[7];
        return 8;
    }

    return 0; /* unreachable */
}
