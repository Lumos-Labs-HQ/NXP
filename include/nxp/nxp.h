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

/* ── Transport-agnostic Connection API ────────────────────── */

/*
 * Connect to a remote NXP endpoint using a URL.
 *
 * Supported schemes:
 *   nxp://host:port     NXP native over UDP (default)
 *   ws://host:port      WebSocket
 *   wss://host:port     WebSocket + TLS
 *   ntc://host:port     Raw TCP with 2-byte length prefix
 *   nrtc://host:port    WebRTC DataChannel (future)
 *
 * Example:
 *   nxp_connect_url("ws://myapp.com:8080/chat", on_conn, on_close, ctx, &conn);
 */
[[nodiscard]] nxp_result nxp_connect_url(
    const char *url,
    nxp_conn_cb on_connected,
    nxp_conn_cb on_closed,
    void *user_data,
    nxp_conn **out_conn
);

/* ── Metrics ──────────────────────────────────────────────── */

/* Render all metrics in Prometheus text format.
 * Returns bytes written, or -1 if buf too small. */
int nxp_metrics_render(char *buf, size_t buf_len);

/* Snapshot metrics from a connection into the global aggregator */
void nxp_metrics_update_conn(const nxp_conn_stats *stats);

/* Reset all metric counters */
void nxp_metrics_reset(void);

#endif /* NXP_H */
