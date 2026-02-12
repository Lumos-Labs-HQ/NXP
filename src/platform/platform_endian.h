/*
 * NXP Platform Endianness
 *
 * Byte order detection and conversion utilities.
 * Uses C23 <stdbit.h> when available.
 */
#ifndef NXP_PLATFORM_ENDIAN_H
#define NXP_PLATFORM_ENDIAN_H

#include <stdint.h>

#if __has_include(<stdbit.h>)
    #include <stdbit.h>
    #if defined(__STDC_ENDIAN_LITTLE__) && defined(__STDC_ENDIAN_NATIVE__)
        #define NXP_LITTLE_ENDIAN (__STDC_ENDIAN_NATIVE__ == __STDC_ENDIAN_LITTLE__)
        #define NXP_BIG_ENDIAN    (__STDC_ENDIAN_NATIVE__ == __STDC_ENDIAN_BIG__)
    #endif
#endif

/* Fallback endian detection */
#if !defined(NXP_LITTLE_ENDIAN)
    #if defined(_WIN32) || defined(__x86_64__) || defined(__i386__) || \
        defined(__aarch64__) || defined(__ARMEL__)
        #define NXP_LITTLE_ENDIAN 1
        #define NXP_BIG_ENDIAN    0
    #else
        #define NXP_LITTLE_ENDIAN 0
        #define NXP_BIG_ENDIAN    1
    #endif
#endif

/* Byte swap functions */
static inline uint16_t nxp_bswap16(uint16_t val) {
    return (uint16_t)((val >> 8) | (val << 8));
}

static inline uint32_t nxp_bswap32(uint32_t val) {
    return ((val & 0xFF000000u) >> 24) |
           ((val & 0x00FF0000u) >> 8)  |
           ((val & 0x0000FF00u) << 8)  |
           ((val & 0x000000FFu) << 24);
}

static inline uint64_t nxp_bswap64(uint64_t val) {
    return ((val & 0xFF00000000000000ULL) >> 56) |
           ((val & 0x00FF000000000000ULL) >> 40) |
           ((val & 0x0000FF0000000000ULL) >> 24) |
           ((val & 0x000000FF00000000ULL) >> 8)  |
           ((val & 0x00000000FF000000ULL) << 8)  |
           ((val & 0x0000000000FF0000ULL) << 24) |
           ((val & 0x000000000000FF00ULL) << 40) |
           ((val & 0x00000000000000FFULL) << 56);
}

/* Host to network byte order (big endian) */
#if NXP_LITTLE_ENDIAN
    #define nxp_hton16(x) nxp_bswap16(x)
    #define nxp_hton32(x) nxp_bswap32(x)
    #define nxp_hton64(x) nxp_bswap64(x)
    #define nxp_ntoh16(x) nxp_bswap16(x)
    #define nxp_ntoh32(x) nxp_bswap32(x)
    #define nxp_ntoh64(x) nxp_bswap64(x)
#else
    #define nxp_hton16(x) (x)
    #define nxp_hton32(x) (x)
    #define nxp_hton64(x) (x)
    #define nxp_ntoh16(x) (x)
    #define nxp_ntoh32(x) (x)
    #define nxp_ntoh64(x) (x)
#endif

/* Read/write big-endian values from/to byte buffers (unaligned-safe) */
static inline uint16_t nxp_read_u16_be(const uint8_t *buf) {
    return (uint16_t)((uint16_t)buf[0] << 8 | (uint16_t)buf[1]);
}

static inline uint32_t nxp_read_u32_be(const uint8_t *buf) {
    return (uint32_t)buf[0] << 24 | (uint32_t)buf[1] << 16 |
           (uint32_t)buf[2] << 8  | (uint32_t)buf[3];
}

static inline uint64_t nxp_read_u64_be(const uint8_t *buf) {
    return (uint64_t)buf[0] << 56 | (uint64_t)buf[1] << 48 |
           (uint64_t)buf[2] << 40 | (uint64_t)buf[3] << 32 |
           (uint64_t)buf[4] << 24 | (uint64_t)buf[5] << 16 |
           (uint64_t)buf[6] << 8  | (uint64_t)buf[7];
}

static inline void nxp_write_u16_be(uint8_t *buf, uint16_t val) {
    buf[0] = (uint8_t)(val >> 8);
    buf[1] = (uint8_t)(val);
}

static inline void nxp_write_u32_be(uint8_t *buf, uint32_t val) {
    buf[0] = (uint8_t)(val >> 24);
    buf[1] = (uint8_t)(val >> 16);
    buf[2] = (uint8_t)(val >> 8);
    buf[3] = (uint8_t)(val);
}

static inline void nxp_write_u64_be(uint8_t *buf, uint64_t val) {
    buf[0] = (uint8_t)(val >> 56);
    buf[1] = (uint8_t)(val >> 48);
    buf[2] = (uint8_t)(val >> 40);
    buf[3] = (uint8_t)(val >> 32);
    buf[4] = (uint8_t)(val >> 24);
    buf[5] = (uint8_t)(val >> 16);
    buf[6] = (uint8_t)(val >> 8);
    buf[7] = (uint8_t)(val);
}

#endif /* NXP_PLATFORM_ENDIAN_H */
