/*
 * NEXUS Protocol (NXP) - Public Umbrella Header
 * Copyright (c) 2026 NXP Contributors
 * SPDX-License-Identifier: MIT
 *
 * A Modern Unified Transport Protocol for Real-Time,
 * Scalable, and Secure Internet Applications.
 */
#ifndef NXP_H
#define NXP_H

#include "nxp_types.h"
#include "nxp_error.h"
#include "nxp_config.h"
#include "nxp_connection.h"
#include "nxp_stream.h"
#include "nxp_listener.h"

/* ── Library Lifecycle ────────────────────────────────────── */

typedef struct nxp_global_config {
    uint32_t worker_threads;       /* 0 = auto (number of CPU cores) */
    uint32_t packet_pool_size;     /* Pre-allocated packet buffers (default 4096) */
} nxp_global_config;

[[nodiscard]] nxp_result nxp_init(const nxp_global_config *config);
void nxp_shutdown(void);

/* ── Event Loop ───────────────────────────────────────────── */

void nxp_run(void);    /* Run until nxp_shutdown() */
void nxp_poll(void);   /* Process pending events, return immediately */

#endif /* NXP_H */
