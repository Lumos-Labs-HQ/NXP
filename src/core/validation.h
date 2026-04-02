/*
 * NXP Input Validation - Security Limits
 * 
 * Defines reasonable limits for all protocol values to prevent
 * integer overflow, memory exhaustion, and other attacks.
 */
#ifndef NXP_VALIDATION_H
#define NXP_VALIDATION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Maximum sizes for variable-length fields */
#define NXP_MAX_FRAME_DATA_LEN      (1024 * 1024)    /* 1 MB per frame */
#define NXP_MAX_TOKEN_LEN           1024              /* 1 KB token */
#define NXP_MAX_REASON_LEN          1024              /* 1 KB error reason */
#define NXP_MAX_CRYPTO_DATA_LEN     (16 * 1024)      /* 16 KB crypto frame */
#define NXP_MAX_DATAGRAM_LEN        (64 * 1024)      /* 64 KB datagram */

/* Stream limits */
#define NXP_MAX_STREAM_OFFSET       (1ULL << 60)     /* 1 exabyte */
#define NXP_MAX_STREAM_ID           (1ULL << 60)     /* Huge but not overflow-prone */

/* Connection limits */
#define NXP_MAX_STREAMS_PER_CONN    65536            /* 64K streams */
#define NXP_MAX_ACK_RANGES          256              /* Already defined but document here */

/* Validation helpers */
static inline bool nxp_validate_stream_id(uint64_t stream_id) {
    return stream_id < NXP_MAX_STREAM_ID;
}

static inline bool nxp_validate_offset(uint64_t offset) {
    return offset < NXP_MAX_STREAM_OFFSET;
}

static inline bool nxp_validate_data_len(uint64_t len, uint64_t max_len) {
    return len <= max_len;
}

/* Check for integer overflow in addition */
static inline bool nxp_check_add_overflow(size_t a, size_t b, size_t *result) {
    if (a > SIZE_MAX - b) return false;
    *result = a + b;
    return true;
}

/* Check if pos + len would overflow or exceed buf_len */
static inline bool nxp_validate_buffer_access(size_t pos, uint64_t len, size_t buf_len) {
    if (len > SIZE_MAX) return false;
    size_t end;
    if (!nxp_check_add_overflow(pos, (size_t)len, &end)) return false;
    return end <= buf_len;
}

#endif /* NXP_VALIDATION_H */
