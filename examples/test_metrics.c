/*
 * Prometheus Metrics Demo
 */
#include "../src/core/connection_internal.h"
#include "../src/util/metrics.h"
#include "../src/platform/platform.h"
#include <stdio.h>
#include <string.h>

int main() {
    printf("=== NXP Prometheus Metrics Demo ===\n\n");

    /* Simulate two connections with stats */
    nxp_conn_stats s1 = {
        .bytes_sent = 102400, .bytes_recv = 204800,
        .packets_sent = 100,  .packets_recv = 200, .packets_lost = 2,
        .rtt_smoothed_us = 12000, .rtt_min_us = 8000,
        .cwnd = 65536, .bytes_in_flight = 4096,
        .streams_opened = 3,
    };
    nxp_conn_stats s2 = {
        .bytes_sent = 51200,  .bytes_recv = 102400,
        .packets_sent = 50,   .packets_recv = 100, .packets_lost = 1,
        .rtt_smoothed_us = 25000, .rtt_min_us = 15000,
        .cwnd = 32768, .bytes_in_flight = 2048,
        .streams_opened = 1,
    };

    nxp_error_init();
    nxp_metrics_reset();
    nxp_metrics_update_conn(&s1);
    nxp_metrics_update_conn(&s2);

    /* Simulate some errors */
    NXP_LOG_ERROR_ONLY(NXP_ERR_INVALID_PACKET, "test");
    NXP_LOG_ERROR_ONLY(NXP_ERR_CRYPTO_FAIL, "test");

    /* Render Prometheus output */
    char buf[4096];
    int n = nxp_metrics_render(buf, sizeof(buf));
    if (n < 0) {
        printf("ERROR: buffer too small\n");
        return 1;
    }

    printf("%s\n", buf);
    printf("--- %d bytes rendered ---\n", n);
    return 0;
}
