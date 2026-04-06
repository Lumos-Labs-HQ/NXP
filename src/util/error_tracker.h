/*
 * NXP Error Tracking System
 */
#pragma once

#include "flight_recorder.h"
#include "../../include/nxp/nxp_error.h"
#include <stdint.h>

/* Error statistics */
typedef struct {
    uint64_t total_errors;
    uint64_t errors_by_code[32];  /* Track first 32 error codes */
    uint64_t last_error_time_us;
    nxp_error_code last_error_code;
} nxp_error_stats;

/* Error tracking API */
void nxp_error_init(void);
void nxp_error_track(nxp_error_code code, const char *context, const char *file, int line);
void nxp_error_get_stats(nxp_error_stats *out);
void nxp_error_reset_stats(void);

/* Error tracking macros with flight recorder integration */
#define NXP_TRACK_ERROR(code, context) do { \
    NXP_FLIGHT_ERROR(code, context); \
    nxp_error_track(code, context, __FILE__, __LINE__); \
    nxp_flight_dump(50); \
} while(0)

/* Critical error - dump more flight data */
#define NXP_CRITICAL_ERROR(code, context) do { \
    NXP_FLIGHT_ERROR(code, context); \
    nxp_error_track(code, context, __FILE__, __LINE__); \
    nxp_flight_dump(200); \
} while(0)

/* Lightweight error tracking (no dump) */
#define NXP_LOG_ERROR_ONLY(code, context) do { \
    NXP_FLIGHT_ERROR(code, context); \
    nxp_error_track(code, context, __FILE__, __LINE__); \
} while(0)
