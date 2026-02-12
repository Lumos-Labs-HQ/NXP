/*
 * NXP Platform Time Abstraction
 *
 * Monotonic high-resolution clock for all timing needs.
 */
#ifndef NXP_PLATFORM_TIME_H
#define NXP_PLATFORM_TIME_H

#include <stdint.h>

/* Monotonic microsecond clock (never goes backwards) */
[[nodiscard]] uint64_t nxp_time_now_us(void);

/* High-resolution nanosecond clock (for benchmarking) */
[[nodiscard]] uint64_t nxp_time_now_ns(void);

/* Sleep for specified microseconds */
void nxp_time_sleep_us(uint64_t us);

#endif /* NXP_PLATFORM_TIME_H */
