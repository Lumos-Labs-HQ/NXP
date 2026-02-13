/*
 * NXP Heartbeat Engine - Implementation
 *
 * Phase 8: Keep-alive via HEARTBEAT frames.
 * Two roles per direction:
 *   - Probe: send HEARTBEAT with our timestamp, expect echo
 *   - Echo:  receive peer's HEARTBEAT, echo it back
 *
 * If we miss NXP_HEARTBEAT_MISS_THRESHOLD consecutive intervals
 * without receiving any response or data, declare the connection dead.
 */
#include "heartbeat_internal.h"

#include <stdint.h>
#include <string.h>

void nxp_heartbeat_init(nxp_heartbeat *hb, uint64_t interval_us) {
    memset(hb, 0, sizeof(*hb));
    hb->interval_us = interval_us;
    hb->enabled = (interval_us > 0);
}

void nxp_heartbeat_check(nxp_heartbeat *hb, uint64_t now_us) {
    if (!hb->enabled) return;

    /* Fire the first heartbeat immediately */
    bool fire = false;
    if (!hb->time_initialized) {
        hb->time_initialized = true;
        fire = true;
    } else if (now_us >= hb->last_sent_us + hb->interval_us) {
        fire = true;
    }

    /* Is it time to send the next heartbeat probe? */
    if (fire) {
        /* If we were awaiting a response and it didn't arrive, count a miss */
        if (hb->awaiting_response) {
            hb->consecutive_misses++;
        }

        hb->send_heartbeat = true;
        hb->pending_timestamp = now_us;
        hb->awaiting_response = true;
        hb->last_sent_us = now_us;
    }
}

void nxp_heartbeat_on_recv(nxp_heartbeat *hb, uint64_t timestamp_us,
                            uint64_t now_us) {
    hb->last_recv_us = now_us;

    /* Echo the peer's timestamp back */
    hb->send_echo = true;
    hb->echo_timestamp = timestamp_us;

    /* Any received data resets the miss counter */
    hb->consecutive_misses = 0;
    hb->awaiting_response = false;
}

void nxp_heartbeat_on_echo(nxp_heartbeat *hb, uint64_t echo_timestamp,
                            uint64_t now_us) {
    (void)now_us;

    /* This is a response to our probe - check if it matches */
    if (echo_timestamp == hb->pending_timestamp) {
        hb->consecutive_misses = 0;
        hb->awaiting_response = false;
    }
}

uint64_t nxp_heartbeat_next_timeout(const nxp_heartbeat *hb) {
    if (!hb->enabled) return UINT64_MAX;
    return hb->last_sent_us + hb->interval_us;
}

bool nxp_heartbeat_is_dead(const nxp_heartbeat *hb) {
    if (!hb->enabled) return false;
    return hb->consecutive_misses >= NXP_HEARTBEAT_MISS_THRESHOLD;
}
