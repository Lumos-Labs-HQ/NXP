/*
 * NXP Windows Platform - Init/Cleanup
 */
#ifdef _WIN32

#include "../platform.h"
#include "../platform_memory.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>

nxp_result nxp_platform_init(void) {
    return nxp_socket_init();
}

void nxp_platform_cleanup(void) {
    nxp_socket_cleanup();
}

/* ── Memory ─────────────────────────────────────────────── */

void *nxp_aligned_alloc(size_t alignment, size_t size) {
    return _aligned_malloc(size, alignment);
}

void nxp_aligned_free(void *ptr) {
    _aligned_free(ptr);
}

void nxp_secure_zero(void *ptr, size_t len) {
    SecureZeroMemory(ptr, len);
}

#endif /* _WIN32 */
