/*
 * NXP Metrics API - Prometheus text format implementation
 */
#include "metrics.h"
#include "error_tracker.h"
#include <stdio.h>
#include <string.h>

/* ── Aggregated metrics state ── */

typedef struct {
    /* Connection stats (aggregated across all conns) */
    uint64_t bytes_sent;
    uint64_t bytes_recv;
    uint64_t packets_sent;
    uint64_t packets_recv;
    uint64_t packets_lost;
    uint64_t rtt_smoothed_us;
    uint64_t rtt_min_us;
    uint64_t cwnd;
    uint64_t bytes_in_flight;
    uint64_t streams_opened;
    uint64_t active_connections;
} nxp_metrics_state;

static nxp_metrics_state g_metrics = {0};

void nxp_metrics_update_conn(const nxp_conn_stats *stats) {
    if (!stats) return;
    g_metrics.bytes_sent      += stats->bytes_sent;
    g_metrics.bytes_recv      += stats->bytes_recv;
    g_metrics.packets_sent    += stats->packets_sent;
    g_metrics.packets_recv    += stats->packets_recv;
    g_metrics.packets_lost    += stats->packets_lost;
    g_metrics.rtt_smoothed_us  = stats->rtt_smoothed_us; /* last-write wins */
    g_metrics.rtt_min_us       = stats->rtt_min_us;
    g_metrics.cwnd             = stats->cwnd;
    g_metrics.bytes_in_flight  = stats->bytes_in_flight;
    g_metrics.streams_opened  += stats->streams_opened;
    g_metrics.active_connections++;
}

void nxp_metrics_reset(void) {
    memset(&g_metrics, 0, sizeof(g_metrics));
}

/* ── Prometheus text format renderer ── */

/* Helper: append a single metric line */
static int append(char *buf, size_t buf_len, size_t *pos,
                  const char *name, const char *help,
                  const char *type, unsigned long long value) {
    int n = snprintf(buf + *pos, buf_len - *pos,
        "# HELP %s %s\n"
        "# TYPE %s %s\n"
        "%s %llu\n",
        name, help,
        name, type,
        name, value);
    if (n < 0 || (size_t)n >= buf_len - *pos) return -1;
    *pos += (size_t)n;
    return 0;
}

int nxp_metrics_render(char *buf, size_t buf_len) {
    if (!buf || buf_len == 0) return -1;

    size_t pos = 0;
    nxp_error_stats err;
    nxp_error_get_stats(&err);

#define M(name, help, type, val) \
    if (append(buf, buf_len, &pos, name, help, type, (unsigned long long)(val)) < 0) return -1;

    /* Connection metrics */
    M("nxp_active_connections",   "Number of active connections",          "gauge",   g_metrics.active_connections)
    M("nxp_bytes_sent_total",     "Total bytes sent",                      "counter", g_metrics.bytes_sent)
    M("nxp_bytes_recv_total",     "Total bytes received",                  "counter", g_metrics.bytes_recv)
    M("nxp_packets_sent_total",   "Total packets sent",                    "counter", g_metrics.packets_sent)
    M("nxp_packets_recv_total",   "Total packets received",                "counter", g_metrics.packets_recv)
    M("nxp_packets_lost_total",   "Total packets lost",                    "counter", g_metrics.packets_lost)
    M("nxp_streams_opened_total", "Total streams opened",                  "counter", g_metrics.streams_opened)

    /* RTT / congestion */
    M("nxp_rtt_smoothed_us",      "Smoothed RTT in microseconds",          "gauge",   g_metrics.rtt_smoothed_us)
    M("nxp_rtt_min_us",           "Minimum RTT in microseconds",           "gauge",   g_metrics.rtt_min_us)
    M("nxp_cwnd_bytes",           "Congestion window in bytes",            "gauge",   g_metrics.cwnd)
    M("nxp_bytes_in_flight",      "Bytes currently in flight",             "gauge",   g_metrics.bytes_in_flight)

    /* Error metrics */
    M("nxp_errors_total",         "Total errors tracked",                  "counter", err.total_errors)

#undef M

    return (int)pos;
}
