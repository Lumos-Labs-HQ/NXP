/*
 * NXP Error Tracking System
 */
#pragma once

#include "flight_recorder.h"
#include "../../include/nxp/nxp_error.h"

/* Error tracking API */
void nxp_error_init(void);
void nxp_error_track(nxp_error_code code, const char *context, const char *file, int line);

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
