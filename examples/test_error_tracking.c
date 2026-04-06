/*
 * Error Tracking & Flight Recorder Demo
 * Shows how the error tracking system works with flight recorder
 */
#include "../src/core/connection_internal.h"
#include "../src/util/error_tracker.h"
#include "../src/util/flight_recorder.h"
#include "../src/platform/platform.h"
#include "../src/logging/nxp_log.h"
#include <stdio.h>
#include <string.h>

int main() {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  NXP Error Tracking & Flight Recorder Demo                ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    // Initialize logging first
    nxp_log_init(NULL, NXP_LOG_INFO);
    
    // Initialize error tracking
    nxp_error_init();
    printf("✅ Error tracking initialized\n\n");
    
    // Simulate various events
    printf("[1] Recording normal operations...\n");
    NXP_FLIGHT_PACKET_TX(1200, "127.0.0.1:9001");
    NXP_FLIGHT_PACKET_RX(800, "127.0.0.1:9002");
    NXP_FLIGHT_CONN_STATE(NXP_CONN_IDLE, NXP_CONN_ESTABLISHED);
    NXP_FLIGHT_STREAM(42, "write", 1024);
    NXP_FLIGHT_ACK(100, 25000);
    
    printf("[2] Simulating errors...\n");
    NXP_LOG_ERROR_ONLY(NXP_ERR_INVALID_PACKET, "malformed packet header");
    NXP_LOG_ERROR_ONLY(NXP_ERR_CRYPTO_FAIL, "AEAD tag mismatch");
    
    printf("[3] Recording more events...\n");
    NXP_FLIGHT_PACKET_TX(1400, "127.0.0.1:9001");
    NXP_FLIGHT_LOSS(99, "timeout");
    NXP_FLIGHT_STREAM(42, "read", 512);
    
    printf("[4] Triggering critical error...\n");
    NXP_CRITICAL_ERROR(NXP_ERR_OUT_OF_MEMORY, "stream buffer allocation failed");
    
    printf("\n[5] Dumping last 10 events:\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    nxp_flight_dump(10);
    
    printf("\n[6] Dumping only ERROR events:\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    nxp_flight_dump_filtered(NXP_FLIGHT_EVENT_ERROR, 10);
    
    printf("\n[7] Error statistics:\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    nxp_error_stats stats;
    nxp_error_get_stats(&stats);
    printf("Total errors: %llu\n", (unsigned long long)stats.total_errors);
    printf("Last error: %s (code %d)\n", 
        nxp_error_str(stats.last_error_code), stats.last_error_code);
    printf("Last error time: %llu us\n", (unsigned long long)stats.last_error_time_us);
    
    printf("\n✅ Error tracking demo complete!\n");
    printf("\n💡 Key Features:\n");
    printf("   • Flight recorder: circular buffer of 1000 events\n");
    printf("   • Error tracking: statistics + context\n");
    printf("   • Filtered dumps: by event type or time range\n");
    printf("   • Auto-dump on critical errors\n");
    
    nxp_log_shutdown();
    return 0;
}
