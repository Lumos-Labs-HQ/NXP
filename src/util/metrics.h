/*
 * NXP Metrics API - Prometheus format
 */
#pragma once

#include "../../include/nxp/nxp_types.h"
#include "../util/error_tracker.h"
#include <stddef.h>

/* Render all metrics in Prometheus text format into buf.
 * Returns bytes written (excluding null terminator), or -1 if buf too small. */
int nxp_metrics_render(char *buf, size_t buf_len);

/* Update connection metrics (call after each nxp_conn_get_stats) */
void nxp_metrics_update_conn(const nxp_conn_stats *stats);

/* Reset all metrics counters */
void nxp_metrics_reset(void);
