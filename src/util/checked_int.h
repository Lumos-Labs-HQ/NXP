/*
 * NXP Checked Integer Arithmetic
 *
 * Wraps C23 <stdckdint.h> for overflow-safe arithmetic.
 * Falls back to manual checks if stdckdint.h is unavailable.
 */
#ifndef NXP_CHECKED_INT_H
#define NXP_CHECKED_INT_H

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

#if __has_include(<stdckdint.h>)
    #include <stdckdint.h>
    #define NXP_HAS_CHECKED_INT 1
#else
    #define NXP_HAS_CHECKED_INT 0
#endif

/*
 * nxp_checked_add_u64: *result = a + b
 * Returns true on overflow (result is truncated).
 */
[[nodiscard]] static inline bool nxp_checked_add_u64(uint64_t *result, uint64_t a, uint64_t b) {
#if NXP_HAS_CHECKED_INT
    return ckd_add(result, a, b);
#else
    *result = a + b;
    return *result < a;
#endif
}

/*
 * nxp_checked_sub_u64: *result = a - b
 * Returns true on underflow.
 */
[[nodiscard]] static inline bool nxp_checked_sub_u64(uint64_t *result, uint64_t a, uint64_t b) {
#if NXP_HAS_CHECKED_INT
    return ckd_sub(result, a, b);
#else
    *result = a - b;
    return a < b;
#endif
}

/*
 * nxp_checked_mul_u64: *result = a * b
 * Returns true on overflow.
 */
[[nodiscard]] static inline bool nxp_checked_mul_u64(uint64_t *result, uint64_t a, uint64_t b) {
#if NXP_HAS_CHECKED_INT
    return ckd_mul(result, a, b);
#else
    *result = a * b;
    if (a == 0) return false;
    return *result / a != b;
#endif
}

/*
 * nxp_checked_add_size: size_t overflow check
 */
[[nodiscard]] static inline bool nxp_checked_add_size(size_t *result, size_t a, size_t b) {
#if NXP_HAS_CHECKED_INT
    return ckd_add(result, a, b);
#else
    *result = a + b;
    return *result < a;
#endif
}

#endif /* NXP_CHECKED_INT_H */
