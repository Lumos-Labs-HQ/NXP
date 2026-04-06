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
    NXP_FLIGHT_EVENT_NETWORK = 7,
    NXP_FLIGHT_EVENT_STREAM = 8,
    NXP_FLIGHT_EVENT_ACK = 9,
    NXP_FLIGHT_EVENT_LOSS = 10
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
void nxp_flight_dump_filtered(nxp_event_type type, size_t count);
void nxp_flight_dump_range(uint64_t start_us, uint64_t end_us);

/* Convenience Macros */
#define NXP_FLIGHT_PACKET_RX(size, src) \
    nxp_flight_record_event(NXP_FLIGHT_EVENT_PACKET_RX, "size=%zu src=%s", (size_t)(size), src)

#define NXP_FLIGHT_PACKET_TX(size, dst) \
    nxp_flight_record_event(NXP_FLIGHT_EVENT_PACKET_TX, "size=%zu dst=%s", (size_t)(size), dst)

#define NXP_FLIGHT_CRYPTO(op, result) \
    nxp_flight_record_event(NXP_FLIGHT_EVENT_CRYPTO_OP, "op=%s result=%s", op, result)

#define NXP_FLIGHT_CONN_STATE(old, new) \
    nxp_flight_record_event(NXP_FLIGHT_EVENT_CONN_STATE, "old=%d new=%d", old, new)

#define NXP_FLIGHT_ERROR(code, msg) \
    nxp_flight_record_event(NXP_FLIGHT_EVENT_ERROR, "code=%d msg=%s", code, msg)

#define NXP_FLIGHT_STREAM(stream_id, action, bytes) \
    nxp_flight_record_event(NXP_FLIGHT_EVENT_STREAM, "id=%llu action=%s bytes=%zu", \
        (unsigned long long)(stream_id), action, (size_t)(bytes))

#define NXP_FLIGHT_ACK(pkt_num, rtt_us) \
    nxp_flight_record_event(NXP_FLIGHT_EVENT_ACK, "pkt=%llu rtt=%llu", \
        (unsigned long long)(pkt_num), (unsigned long long)(rtt_us))

#define NXP_FLIGHT_LOSS(pkt_num, reason) \
    nxp_flight_record_event(NXP_FLIGHT_EVENT_LOSS, "pkt=%llu reason=%s", \
        (unsigned long long)(pkt_num), reason)
