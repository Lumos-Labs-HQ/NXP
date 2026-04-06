/*
 * NXP Error Tracking Implementation
 */
#include "error_tracker.h"
#include "../logging/nxp_log.h"
#include "../platform/platform.h"
#include <string.h>

static nxp_error_stats g_error_stats = {0};

void nxp_error_init(void) {
    nxp_flight_init();
    memset(&g_error_stats, 0, sizeof(g_error_stats));
}

void nxp_error_track(nxp_error_code code, const char *context, const char *file, int line) {
    // Update statistics
    g_error_stats.total_errors++;
    g_error_stats.last_error_code = code;
    g_error_stats.last_error_time_us = nxp_time_now_us();
    
    // Track by error code (if within range)
    if (code >= 0 && code < 32) {
        g_error_stats.errors_by_code[code]++;
    }
    
    // Log the error
    NXP_LOG_ERROR("ERROR %d: %s at %s:%d - %s", 
        code, nxp_error_str(code), file, line, context ? context : "");
}

void nxp_error_get_stats(nxp_error_stats *out) {
    if (out) {
        memcpy(out, &g_error_stats, sizeof(nxp_error_stats));
    }
}

void nxp_error_reset_stats(void) {
    memset(&g_error_stats, 0, sizeof(g_error_stats));
}
