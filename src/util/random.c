/*
 * NXP Random - Implementation
 *
 * Platform-specific secure random:
 * - Windows: BCryptGenRandom
 * - Linux: getrandom() syscall
 */
#include "random.h"

#include <string.h>

#ifdef _WIN32
    #include <windows.h>
    #include <bcrypt.h>
    #pragma comment(lib, "bcrypt.lib")
#else
    #include <sys/random.h>
#endif

nxp_result nxp_random_bytes(uint8_t *buf, size_t len) {
    if (buf == nullptr || len == 0) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

#ifdef _WIN32
    NTSTATUS status = BCryptGenRandom(
        nullptr, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG
    );
    if (!BCRYPT_SUCCESS(status)) {
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }
#else
    ssize_t ret = getrandom(buf, len, 0);
    if (ret < 0 || (size_t)ret != len) {
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }
#endif

    return NXP_SUCCESS;
}

uint64_t nxp_random_u64(void) {
    uint64_t value = 0;
    (void)nxp_random_bytes((uint8_t *)&value, sizeof(value));
    return value;
}

uint32_t nxp_random_u32(void) {
    uint32_t value = 0;
    (void)nxp_random_bytes((uint8_t *)&value, sizeof(value));
    return value;
}
