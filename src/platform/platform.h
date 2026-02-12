/*
 * NXP Platform Layer - Main Header
 *
 * Aggregates all platform abstractions.
 */
#ifndef NXP_PLATFORM_H
#define NXP_PLATFORM_H

#include "platform_time.h"
#include "platform_thread.h"
#include "platform_socket.h"
#include "platform_memory.h"
#include "platform_atomic.h"
#include "platform_endian.h"
#include "event_loop.h"

/* Platform initialization (call once at startup, before any other platform call) */
[[nodiscard]] nxp_result nxp_platform_init(void);

/* Platform cleanup */
void nxp_platform_cleanup(void);

#endif /* NXP_PLATFORM_H */
