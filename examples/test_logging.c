/*
 * Logging Integration Demo
 * Shows the complete logging system with flight recorder
 */
#include "../src/core/connection_internal.h"
#include "../src/util/error_tracker.h"
#include "../src/util/flight_recorder.h"
#include "../src/platform/platform.h"
#include "../src/logging/nxp_log.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main() {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  NXP Logging Integration Demo                             ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    // Initialize logging at INFO level
    nxp_log_init(NULL, NXP_LOG_DEBUG);
    nxp_error_init();
    
    printf("✅ Logging initialized (level=DEBUG)\n\n");
    
    // Create a connection (will log)
    printf("[1] Creating connection...\n");
    nxp_conn_config cfg = {0};
    cfg.scid.data[0] = 0x01;
    cfg.scid.len = 8;
    cfg.idle_timeout_us = 30000000;
    cfg.initial_max_data = 1048576;
    cfg.initial_max_stream_data = 262144;
    cfg.max_streams_bidi = 256;
    cfg.max_streams_uni = 256;
    nxp_addr_from_string("127.0.0.1", 9001, &cfg.peer_addr);
    
    nxp_conn *conn = nxp_conn_create(&cfg, false);
    nxp_conn_id dcid = {.len = 8};
    dcid.data[0] = 0x02;
    nxp_conn_set_established(conn, &dcid);
    
    printf("\n[2] Opening stream...\n");
    uint64_t stream_id;
    nxp_conn_open_stream(conn, &stream_id, NXP_STREAM_RELIABLE, false);
    
    printf("\n[3] Writing data to stream...\n");
    const char *msg = "Hello, NXP logging!";
    nxp_conn_stream_send(conn, stream_id, (const uint8_t*)msg, strlen(msg), false);
    
    printf("\n[4] Generating packet...\n");
    uint8_t pkt[1500];
    uint64_t now = nxp_time_now_us();
    ssize_t pkt_len = nxp_conn_send(conn, pkt, sizeof(pkt), now);
    printf("Generated packet: %zd bytes\n", pkt_len);
    
    printf("\n[5] Simulating packet receive...\n");
    nxp_conn_recv(conn, pkt, pkt_len, now + 1000);
    
    printf("\n[6] Triggering some errors...\n");
    NXP_LOG_ERROR_ONLY(NXP_ERR_INVALID_PACKET, "simulated packet error");
    NXP_LOG_ERROR_ONLY(NXP_ERR_CRYPTO_FAIL, "simulated crypto error");
    
    printf("\n[7] Flight recorder dump (last 20 events):\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    nxp_flight_dump(20);
    
    printf("\n[8] Error statistics:\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    nxp_error_stats stats;
    nxp_error_get_stats(&stats);
    printf("Total errors: %llu\n", (unsigned long long)stats.total_errors);
    printf("Last error: %s (code %d)\n", 
           nxp_error_str(stats.last_error_code), stats.last_error_code);
    
    printf("\n[9] Cleaning up...\n");
    nxp_conn_destroy(conn);
    
    printf("\n✅ Demo complete!\n\n");
    printf("💡 Key Features Demonstrated:\n");
    printf("   • Structured logging (Quill backend)\n");
    printf("   • Flight recorder integration\n");
    printf("   • Error tracking with statistics\n");
    printf("   • Connection lifecycle logging\n");
    printf("   • Stream operation logging\n");
    printf("   • Packet RX/TX logging\n");
    
    nxp_log_shutdown();
    return 0;
}
