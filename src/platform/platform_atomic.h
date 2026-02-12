/*
 * NXP Platform Atomic Operations
 *
 * Thin wrappers around C23 stdatomic.
 */
#ifndef NXP_PLATFORM_ATOMIC_H
#define NXP_PLATFORM_ATOMIC_H

#include <stdatomic.h>
#include <stdint.h>

/* Atomic uint64 */
typedef atomic_uint_least64_t nxp_atomic_u64;

static inline void nxp_atomic_store_u64(nxp_atomic_u64 *a, uint64_t val) {
    atomic_store_explicit(a, val, memory_order_release);
}

static inline uint64_t nxp_atomic_load_u64(const nxp_atomic_u64 *a) {
    return atomic_load_explicit((nxp_atomic_u64 *)a, memory_order_acquire);
}

static inline uint64_t nxp_atomic_fetch_add_u64(nxp_atomic_u64 *a, uint64_t val) {
    return atomic_fetch_add_explicit(a, val, memory_order_acq_rel);
}

/* Atomic uint32 */
typedef atomic_uint_least32_t nxp_atomic_u32;

static inline void nxp_atomic_store_u32(nxp_atomic_u32 *a, uint32_t val) {
    atomic_store_explicit(a, val, memory_order_release);
}

static inline uint32_t nxp_atomic_load_u32(const nxp_atomic_u32 *a) {
    return atomic_load_explicit((nxp_atomic_u32 *)a, memory_order_acquire);
}

/* Atomic bool */
typedef atomic_bool nxp_atomic_bool;

static inline void nxp_atomic_store_bool(nxp_atomic_bool *a, bool val) {
    atomic_store_explicit(a, val, memory_order_release);
}

static inline bool nxp_atomic_load_bool(const nxp_atomic_bool *a) {
    return atomic_load_explicit((nxp_atomic_bool *)a, memory_order_acquire);
}

#endif /* NXP_PLATFORM_ATOMIC_H */
