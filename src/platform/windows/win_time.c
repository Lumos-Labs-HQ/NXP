/*
 * NXP Windows Time - QueryPerformanceCounter based
 */
#ifdef _WIN32

#include "../platform_time.h"
#include <windows.h>

static uint64_t g_qpc_freq = 0;

static void ensure_freq(void) {
    if (g_qpc_freq == 0) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        g_qpc_freq = (uint64_t)freq.QuadPart;
    }
}

uint64_t nxp_time_now_us(void) {
    ensure_freq();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (uint64_t)now.QuadPart * 1000000ULL / g_qpc_freq;
}

uint64_t nxp_time_now_ns(void) {
    ensure_freq();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (uint64_t)now.QuadPart * 1000000000ULL / g_qpc_freq;
}

void nxp_time_sleep_us(uint64_t us) {
    /* Windows Sleep has millisecond granularity */
    DWORD ms = (DWORD)(us / 1000);
    if (ms == 0 && us > 0) ms = 1;
    Sleep(ms);
}

#endif /* _WIN32 */
