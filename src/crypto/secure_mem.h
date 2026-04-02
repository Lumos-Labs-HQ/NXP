/*
 * NXP Secure Memory Operations - Header
 *
 * Phase 10: Guaranteed non-optimizable memory zeroing for key material.
 *
 * C23 defines memset_explicit() but GCC 14 / glibc doesn't provide it yet.
 * We use explicit_bzero() on Linux (_DEFAULT_SOURCE) and
 * SecureZeroMemory() on Windows as portable alternatives.
 */
#ifndef NXP_SECURE_MEM_H
#define NXP_SECURE_MEM_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
  #include <windows.h>
  static inline void nxp_secure_zero(void *ptr, size_t len) {
      SecureZeroMemory(ptr, len);
  }
#else
  /*
   * explicit_bzero is guaranteed not to be optimized away.
   * Available in glibc with _DEFAULT_SOURCE (set in CMake).
   */
  #include <string.h>
  static inline void nxp_secure_zero(void *ptr, size_t len) {
      explicit_bzero(ptr, len);
  }
#endif

/* Constant-time memory comparison (timing-safe) */
static inline int nxp_secure_compare(const void *a, const void *b, size_t len) {
    const uint8_t *aa = (const uint8_t *)a;
    const uint8_t *bb = (const uint8_t *)b;
    uint8_t diff = 0;
    
    for (size_t i = 0; i < len; i++) {
        diff |= aa[i] ^ bb[i];
    }
    
    return diff; /* 0 if equal, non-zero if different */
}

#endif /* NXP_SECURE_MEM_H */
