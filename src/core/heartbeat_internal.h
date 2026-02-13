/*
 * NXP Heartbeat Engine - Internal Header
 *
 * Phase 8: Timer-based keep-alive using HEARTBEAT frames (0x20).
 * Detects dead connections and measures one-way latency.
 *
 * The heartbeat carries a sender timestamp. The receiver echoes it back,
 * allowing the sender to compute RTT independently of ACK-based RTT.
 */
#ifndef NXP_HEARTBEAT_INTERNAL_H
#define NXP_HEARTBEAT_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>

/* Default heartbeat interval: 5 seconds */
#define NXP_HEARTBEAT_DEFAULT_INTERVAL_US   (5ULL * 1000000ULL)

/* Miss threshold: declare dead after this many consecutive misses */
#define NXP_HEARTBEAT_MISS_THRESHOLD        3

typedef struct nxp_heartbeat {
    uint64_t interval_us;          /* How often to send (0 = disabled) */
    uint64_t last_sent_us;         /* When we last sent a heartbeat */
    uint64_t last_recv_us;         /* When we last received a heartbeat */
    uint64_t pending_timestamp;    /* Timestamp of the heartbeat awaiting echo */
    uint32_t consecutive_misses;   /* # of intervals without a response */
    bool     enabled;
    bool     send_heartbeat;       /* Need to send a heartbeat frame */
    bool     send_echo;            /* Need to echo a received heartbeat */
    uint64_t echo_timestamp;       /* Timestamp to echo back */
    bool     awaiting_response;    /* Sent a heartbeat, waiting for echo */
    bool     time_initialized;    /* Has the first check happened? */
} nxp_heartbeat;

/* Initialize heartbeat state */
void nxp_heartbeat_init(nxp_heartbeat *hb, uint64_t interval_us);

/* Check if it's time to send a heartbeat. Call from timeout handler. */
void nxp_heartbeat_check(nxp_heartbeat *hb, uint64_t now_us);

/* Called when we receive a HEARTBEAT frame from peer */
void nxp_heartbeat_on_recv(nxp_heartbeat *hb, uint64_t timestamp_us,
                            uint64_t now_us);

/* Called when our heartbeat echo is received (response to our probe) */
void nxp_heartbeat_on_echo(nxp_heartbeat *hb, uint64_t echo_timestamp,
                            uint64_t now_us);

/* Get the next heartbeat deadline (UINT64_MAX if disabled) */
uint64_t nxp_heartbeat_next_timeout(const nxp_heartbeat *hb);

/* Check if the connection should be considered dead */
bool nxp_heartbeat_is_dead(const nxp_heartbeat *hb);

/* Has any heartbeat-related frame to send? */
static inline bool nxp_heartbeat_has_pending(const nxp_heartbeat *hb) {
    return hb->send_heartbeat || hb->send_echo;
}

#endif /* NXP_HEARTBEAT_INTERNAL_H */
