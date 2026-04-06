/*
 * NXP Flight Recorder - Error Tracking System
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define NXP_FLIGHT_BUFFER_SIZE 1000
#define NXP_FLIGHT_DATA_SIZE 64

typedef enum {
    NXP_FLIGHT_EVENT_PACKET_RX = 1,
    NXP_FLIGHT_EVENT_PACKET_TX = 2,
    NXP_FLIGHT_EVENT_CRYPTO_OP = 3,
    NXP_FLIGHT_EVENT_CONN_STATE = 4,
    NXP_FLIGHT_EVENT_MEMORY_OP = 5,
    NXP_FLIGHT_EVENT_ERROR = 6,
    NXP_FLIGHT_EVENT_NETWORK = 7
} nxp_event_type;

typedef struct {
    uint64_t timestamp_us;
    nxp_event_type type;
    char data[NXP_FLIGHT_DATA_SIZE];
} nxp_flight_record;

typedef struct {
    nxp_flight_record records[NXP_FLIGHT_BUFFER_SIZE];
    volatile size_t head;
    volatile bool full;
} nxp_flight_recorder;

/* Core API */
void nxp_flight_init(void);
void nxp_flight_record_event(nxp_event_type type, const char *fmt, ...);
void nxp_flight_dump(size_t count);
void nxp_flight_dump_all(void);

/* Convenience Macros */
#define NXP_FLIGHT_PACKET_RX(size, src) \
    nxp_flight_record_event(NXP_FLIGHT_EVENT_PACKET_RX, "size=%zu src=%s", (size_t)(size), src)

#define NXP_FLIGHT_PACKET_TX(size, dst) \
    nxp_flight_record_event(NXP_FLIGHT_EVENT_PACKET_TX, "size=%zu dst=%s", (size_t)(size), dst)

#define NXP_FLIGHT_CRYPTO(op, result) \
    nxp_flight_record_event(NXP_FLIGHT_EVENT_CRYPTO_OP, "op=%s result=%s", op, result)

#define NXP_FLIGHT_CONN_STATE(old, new) \
    nxp_flight_record_event(NXP_FLIGHT_EVENT_CONN_STATE, "old=%s new=%s", old, new)

#define NXP_FLIGHT_ERROR(code, msg) \
    nxp_flight_record_event(NXP_FLIGHT_EVENT_ERROR, "code=%d msg=%s", code, msg)
