/*
 * NXP Linux Platform - Init/Cleanup
 */
#ifndef _WIN32

#include "../platform.h"
#include "../platform_memory.h"
#include <stdlib.h>
#include <string.h>

nxp_result nxp_platform_init(void) {
    return nxp_socket_init();
}

void nxp_platform_cleanup(void) {
    nxp_socket_cleanup();
}

/* ── Memory ─────────────────────────────────────────────── */

void *nxp_aligned_alloc(size_t alignment, size_t size) {
    void *ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return nullptr;
    }
    return ptr;
}

void nxp_aligned_free(void *ptr) {
    free(ptr);
}

void nxp_secure_zero(void *ptr, size_t len) {
#if __has_builtin(__builtin_memset_inline)
    __builtin_memset_inline(ptr, 0, len);
#else
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) {
        *p++ = 0;
    }
#endif
}

#endif /* !_WIN32 */
