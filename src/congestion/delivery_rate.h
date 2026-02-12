/*
 * NXP Delivery Rate Estimation - Header
 *
 * Phase 7: Per-packet bandwidth sampling for BBR.
 * Measures how fast data is being delivered to the receiver.
 *
 * Inspired by the Linux kernel's delivery rate estimation.
 *
 * When a packet is sent, we snapshot the cumulative delivered bytes
 * and timestamps. When an ACK arrives, we compute:
 *   delivery_rate = delta_delivered / delta_time
 */
#ifndef NXP_DELIVERY_RATE_H
#define NXP_DELIVERY_RATE_H

#include <stdint.h>
#include <stdbool.h>

/* Snapshot taken when a packet is sent */
typedef struct nxp_dr_pkt_state {
    uint64_t delivered;           /* Total delivered when this pkt was sent */
    uint64_t delivered_time;      /* When delivered was last updated */
    uint64_t first_sent_time;     /* When we sent the first unacked pkt */
    bool     is_app_limited;      /* Were we app-limited when sending? */
    uint64_t app_limited_at;      /* delivered count when app-limited started */
} nxp_dr_pkt_state;

/* Per-connection delivery rate estimation state */
typedef struct nxp_delivery_rate {
    /* Cumulative counters */
    uint64_t delivered;           /* Total bytes delivered (ACKed) */
    uint64_t delivered_time;      /* Time of last delivery update */
    uint64_t first_sent_time;     /* Time first unacked pkt was sent */

    /* Rate sample computed from the most recent ACK */
    uint64_t rate_sample_bps;     /* Latest delivery rate in bytes/sec */
    uint64_t rate_interval_us;    /* Time interval for rate sample */
    uint64_t rate_delivered;      /* Bytes delivered in sample interval */
    bool     rate_is_app_limited; /* Was the sample app-limited? */

    /* Application-limited tracking */
    bool     app_limited;         /* Currently app-limited? */
    uint64_t app_limited_at;      /* delivered when we became app-limited */

    /* Round-trip counter for BBR */
    uint64_t round_count;         /* Number of completed rounds */
    uint64_t next_round_delivered; /* delivered count at start of next round */
    bool     round_start;         /* Did we start a new round this ACK? */
} nxp_delivery_rate;

/* Initialize delivery rate state */
void nxp_dr_init(nxp_delivery_rate *dr);

/* Snapshot state when sending a packet. Caller stores this in sent_pkt. */
void nxp_dr_on_packet_sent(nxp_delivery_rate *dr,
                            uint32_t sent_bytes,
                            uint64_t now_us,
                            nxp_dr_pkt_state *out);

/* Update when a packet is ACKed. Computes a new rate sample. */
void nxp_dr_on_packet_acked(nxp_delivery_rate *dr,
                             const nxp_dr_pkt_state *pkt_state,
                             uint32_t acked_bytes,
                             uint64_t now_us);

/* Mark the connection as application-limited (send buffer empty). */
void nxp_dr_set_app_limited(nxp_delivery_rate *dr);

/* Clear application-limited state. */
void nxp_dr_clear_app_limited(nxp_delivery_rate *dr);

/* Check if a new round-trip started with the most recent ACK. */
static inline bool nxp_dr_is_round_start(const nxp_delivery_rate *dr) {
    return dr->round_start;
}

/* Get current delivery rate in bytes/second (0 if no sample). */
static inline uint64_t nxp_dr_get_rate(const nxp_delivery_rate *dr) {
    return dr->rate_sample_bps;
}

#endif /* NXP_DELIVERY_RATE_H */
