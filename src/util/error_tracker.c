/*
 * NXP Error Tracking Implementation
 */
#include "error_tracker.h"
#include "../logging/nxp_log.h"

void nxp_error_init(void) {
    nxp_flight_init();
}

void nxp_error_track(nxp_error_code code, const char *context, const char *file, int line) {
    NXP_LOG_ERROR("ERROR %d: %s at %s:%d - %s", 
        code, nxp_error_str(code), file, line, context ? context : "");
}
