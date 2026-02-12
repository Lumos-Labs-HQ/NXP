/*
 * NXP Platform Memory Abstraction
 *
 * Aligned allocation and platform memory utilities.
 */
#ifndef NXP_PLATFORM_MEMORY_H
#define NXP_PLATFORM_MEMORY_H

#include <stddef.h>
#include <stdalign.h>

/* Allocate aligned memory */
[[nodiscard]] void *nxp_aligned_alloc(size_t alignment, size_t size);

/* Free aligned memory */
void nxp_aligned_free(void *ptr);

/* Secure memory clear (not optimized away by compiler) */
void nxp_secure_zero(void *ptr, size_t len);

#endif /* NXP_PLATFORM_MEMORY_H */
