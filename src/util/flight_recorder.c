/*
 * NXP Flight Recorder Implementation
 */
#include "flight_recorder.h"
#include "../platform/platform.h"
#include "../logging/nxp_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static nxp_flight_recorder g_recorder = {0};

void nxp_flight_init(void) {
    memset(&g_recorder, 0, sizeof(g_recorder));
}

void nxp_flight_record_event(nxp_event_type type, const char *fmt, ...) {
    if (!fmt) return;
    
    size_t index = g_recorder.head;
    nxp_flight_record *rec = &g_recorder.records[index];
    
    // Record timestamp
    rec->timestamp_us = nxp_time_now_us();
    rec->type = type;
    
    // Format event data
    va_list args;
    va_start(args, fmt);
    vsnprintf(rec->data, NXP_FLIGHT_DATA_SIZE, fmt, args);
    va_end(args);
    
    // Advance circular buffer
    g_recorder.head = (g_recorder.head + 1) % NXP_FLIGHT_BUFFER_SIZE;
    if (g_recorder.head == 0) {
        g_recorder.full = true;
    }
}

static const char* event_type_name(nxp_event_type type) {
    switch (type) {
        case NXP_FLIGHT_EVENT_PACKET_RX: return "PKT_RX";
        case NXP_FLIGHT_EVENT_PACKET_TX: return "PKT_TX";
        case NXP_FLIGHT_EVENT_CRYPTO_OP: return "CRYPTO";
        case NXP_FLIGHT_EVENT_CONN_STATE: return "CONN";
        case NXP_FLIGHT_EVENT_MEMORY_OP: return "MEMORY";
        case NXP_FLIGHT_EVENT_ERROR: return "ERROR";
        case NXP_FLIGHT_EVENT_NETWORK: return "NETWORK";
        case NXP_FLIGHT_EVENT_STREAM: return "STREAM";
        case NXP_FLIGHT_EVENT_ACK: return "ACK";
        case NXP_FLIGHT_EVENT_LOSS: return "LOSS";
        default: return "UNKNOWN";
    }
}

void nxp_flight_dump(size_t count) {
    size_t total = g_recorder.full ? NXP_FLIGHT_BUFFER_SIZE : g_recorder.head;
    if (count > total) count = total;
    if (count == 0) return;
    
    NXP_LOG_INFO("=== FLIGHT RECORDER DUMP (last %zu events) ===", count);
    
    size_t start = g_recorder.full ? 
        (g_recorder.head + NXP_FLIGHT_BUFFER_SIZE - count) % NXP_FLIGHT_BUFFER_SIZE :
        (g_recorder.head >= count ? g_recorder.head - count : 0);
    
    for (size_t i = 0; i < count; i++) {
        size_t idx = (start + i) % NXP_FLIGHT_BUFFER_SIZE;
        nxp_flight_record *rec = &g_recorder.records[idx];
        
        NXP_LOG_INFO("[%llu] %s: %s", 
            (unsigned long long)rec->timestamp_us,
            event_type_name(rec->type),
            rec->data);
    }
    
    NXP_LOG_INFO("=== END FLIGHT RECORDER DUMP ===");
}

void nxp_flight_dump_all(void) {
    size_t total = g_recorder.full ? NXP_FLIGHT_BUFFER_SIZE : g_recorder.head;
    nxp_flight_dump(total);
}

void nxp_flight_dump_filtered(nxp_event_type type, size_t count) {
    size_t total = g_recorder.full ? NXP_FLIGHT_BUFFER_SIZE : g_recorder.head;
    if (total == 0) return;
    
    NXP_LOG_INFO("=== FLIGHT RECORDER DUMP (type=%s, max=%zu) ===", 
        event_type_name(type), count);
    
    size_t found = 0;
    size_t start = g_recorder.full ? g_recorder.head : 0;
    
    // Scan backwards from most recent
    for (size_t i = 0; i < total && found < count; i++) {
        size_t idx = (start + total - 1 - i) % NXP_FLIGHT_BUFFER_SIZE;
        nxp_flight_record *rec = &g_recorder.records[idx];
        
        if (rec->type == type) {
            NXP_LOG_INFO("[%llu] %s: %s", 
                (unsigned long long)rec->timestamp_us,
                event_type_name(rec->type),
                rec->data);
            found++;
        }
    }
    
    NXP_LOG_INFO("=== END FILTERED DUMP (%zu events) ===", found);
}

void nxp_flight_dump_range(uint64_t start_us, uint64_t end_us) {
    size_t total = g_recorder.full ? NXP_FLIGHT_BUFFER_SIZE : g_recorder.head;
    if (total == 0) return;
    
    NXP_LOG_INFO("=== FLIGHT RECORDER DUMP (time range: %llu - %llu us) ===", 
        (unsigned long long)start_us, (unsigned long long)end_us);
    
    size_t found = 0;
    size_t start_idx = g_recorder.full ? g_recorder.head : 0;
    
    for (size_t i = 0; i < total; i++) {
        size_t idx = (start_idx + i) % NXP_FLIGHT_BUFFER_SIZE;
        nxp_flight_record *rec = &g_recorder.records[idx];
        
        if (rec->timestamp_us >= start_us && rec->timestamp_us <= end_us) {
            NXP_LOG_INFO("[%llu] %s: %s", 
                (unsigned long long)rec->timestamp_us,
                event_type_name(rec->type),
                rec->data);
            found++;
        }
    }
    
    NXP_LOG_INFO("=== END RANGE DUMP (%zu events) ===", found);
}
