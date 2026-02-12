/*
 * NXP Delivery Rate Estimation - Implementation
 *
 * Phase 7: Per-packet bandwidth sampling for BBR.
 */
#include "delivery_rate.h"

#include <string.h>

void nxp_dr_init(nxp_delivery_rate *dr) {
    memset(dr, 0, sizeof(*dr));
}

void nxp_dr_on_packet_sent(nxp_delivery_rate *dr,
                            uint32_t sent_bytes,
                            uint64_t now_us,
                            nxp_dr_pkt_state *out) {
    (void)sent_bytes;

    /* If no packets in flight, record this as the first sent time */
    if (dr->first_sent_time == 0) {
        dr->first_sent_time = now_us;
        dr->delivered_time  = now_us;
    }

    /* Snapshot the current delivery state */
    out->delivered       = dr->delivered;
    out->delivered_time  = dr->delivered_time;
    out->first_sent_time = dr->first_sent_time;
    out->is_app_limited  = dr->app_limited;
    out->app_limited_at  = dr->app_limited_at;

    /* This packet is now the most recently sent */
    dr->first_sent_time = now_us;
}

void nxp_dr_on_packet_acked(nxp_delivery_rate *dr,
                             const nxp_dr_pkt_state *pkt_state,
                             uint32_t acked_bytes,
                             uint64_t now_us) {
    dr->delivered += acked_bytes;
    dr->delivered_time = now_us;

    /* Check for round-trip completion.
     * A round completes when we get an ACK for a packet sent after
     * next_round_delivered was set. */
    if (pkt_state->delivered >= dr->next_round_delivered) {
        dr->next_round_delivered = dr->delivered;
        dr->round_count++;
        dr->round_start = true;
    } else {
        dr->round_start = false;
    }

    /* Compute the delivery rate sample.
     *
     * delivery_rate = delta_delivered / max(send_interval, ack_interval)
     *
     * This handles both sender-paced and receiver-paced scenarios.
     */
    uint64_t delta_delivered = dr->delivered - pkt_state->delivered;

    /* Send interval: time between this ACK's packet was sent and the
     * earliest unacked packet was sent */
    uint64_t send_elapsed = (dr->first_sent_time > pkt_state->first_sent_time)
        ? dr->first_sent_time - pkt_state->first_sent_time : 0;

    /* ACK interval: time between this ACK and when delivered was last updated
     * at the time this packet was sent */
    uint64_t ack_elapsed = (now_us > pkt_state->delivered_time)
        ? now_us - pkt_state->delivered_time : 0;

    /* Use the larger interval (the bottleneck) */
    uint64_t interval = send_elapsed > ack_elapsed ? send_elapsed : ack_elapsed;

    if (interval == 0 || delta_delivered == 0) {
        /* Can't compute a meaningful rate */
        dr->round_start = (pkt_state->delivered >= dr->next_round_delivered - acked_bytes);
        return;
    }

    dr->rate_interval_us    = interval;
    dr->rate_delivered      = delta_delivered;
    dr->rate_is_app_limited = pkt_state->is_app_limited;

    /* Convert to bytes/second: (bytes * 1e6) / microseconds */
    dr->rate_sample_bps = (delta_delivered * 1000000ULL) / interval;

    /* If this was an app-limited sample and our current delivered count
     * exceeds the point where app-limiting started, clear it.
     * The idea: once we've ACKed beyond the app-limited window,
     * the rate sample reflects true capacity, not app limitation. */
    if (dr->app_limited && dr->delivered > dr->app_limited_at) {
        dr->app_limited = false;
    }
}

void nxp_dr_set_app_limited(nxp_delivery_rate *dr) {
    dr->app_limited = true;
    dr->app_limited_at = dr->delivered;
}

void nxp_dr_clear_app_limited(nxp_delivery_rate *dr) {
    dr->app_limited = false;
}
