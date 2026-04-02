/*
 * NXP ACK Tracker + Loss Detection
 *
 * Phase 4: Tracks received packet numbers (for generating ACKs),
 * sent packet metadata (for processing peer ACKs), RTT estimation,
 * and time/reorder-based loss detection (RACK-style).
 */
#include "connection_internal.h"

#include <stdlib.h>
#include <string.h>

/* ── Helpers ───────────────────────────────────────────── */

/* Initial RTT estimate before any sample */
#define INITIAL_RTT_US  333000   /* 333ms */

static uint64_t u64_max(uint64_t a, uint64_t b) { return a > b ? a : b; }
static uint64_t u64_min(uint64_t a, uint64_t b) { return a < b ? a : b; }

/* PTO = smoothed_rtt + max(4 * rtt_var, 1ms) + max_ack_delay */
static uint64_t compute_pto(const nxp_ack_state *ack) {
    uint64_t rtt     = ack->has_rtt ? ack->smoothed_rtt : INITIAL_RTT_US;
    uint64_t var     = ack->has_rtt ? ack->rtt_var : (INITIAL_RTT_US / 2);
    uint64_t pto_val = rtt + u64_max(4 * var, (uint64_t)1000) + NXP_ACK_DELAY_US;
    return pto_val;
}

/* ── Init / Cleanup ───────────────────────────────────── */

void nxp_ack_init(nxp_ack_state *ack) {
    memset(ack, 0, sizeof(*ack));
    ack->smoothed_rtt = INITIAL_RTT_US;
    ack->rtt_var      = INITIAL_RTT_US / 2;
    ack->min_rtt      = UINT64_MAX;
    ack->loss_time    = UINT64_MAX;

    ack->sent_cap = 256;
    ack->sent     = (nxp_sent_pkt *)calloc(ack->sent_cap, sizeof(nxp_sent_pkt));
}

void nxp_ack_cleanup(nxp_ack_state *ack) {
    free(ack->sent);
    ack->sent      = nullptr;
    ack->sent_count = 0;
    ack->sent_cap   = 0;
}

/* ── Received Packet Tracking (for outbound ACK generation) ── */

void nxp_ack_on_pkt_recv(nxp_ack_state *ack, uint64_t pkt_num,
                          uint64_t now_us, bool ack_eliciting) {
    /* Check for duplicate/replay - packet already received */
    for (uint32_t i = 0; i < ack->recv_range_count; i++) {
        nxp_recv_pn_range *r = &ack->recv_ranges[i];
        if (pkt_num >= r->start && pkt_num <= r->end) {
            /* Duplicate packet - ignore (replay attack or network duplication) */
            return;
        }
    }

    /* Update largest received */
    if (ack->recv_range_count == 0 || pkt_num > ack->largest_recv_pn) {
        ack->largest_recv_pn   = pkt_num;
        ack->largest_recv_time = now_us;
    }

    /* Insert pkt_num into the recv_ranges (sorted descending by start) */
    /* Try to extend an existing range or merge */
    bool inserted = false;

    for (uint32_t i = 0; i < ack->recv_range_count; i++) {
        nxp_recv_pn_range *r = &ack->recv_ranges[i];

        /* Already in range (should not happen due to check above) */
        if (pkt_num >= r->start && pkt_num <= r->end) {
            inserted = true;
            break;
        }

        /* Extend end by 1 */
        if (pkt_num == r->end + 1) {
            r->end = pkt_num;
            /* Try to merge with the previous range (lower index = higher pn) */
            if (i > 0 && ack->recv_ranges[i - 1].start == r->end + 1) {
                ack->recv_ranges[i - 1].start = r->start;
                /* Remove range i */
                memmove(&ack->recv_ranges[i], &ack->recv_ranges[i + 1],
                        (ack->recv_range_count - i - 1) * sizeof(nxp_recv_pn_range));
                ack->recv_range_count--;
            }
            inserted = true;
            break;
        }

        /* Extend start by 1 */
        if (pkt_num + 1 == r->start) {
            r->start = pkt_num;
            /* Try to merge with the next range */
            if (i + 1 < ack->recv_range_count &&
                ack->recv_ranges[i + 1].end + 1 == r->start) {
                r->start = ack->recv_ranges[i + 1].start;
                /* Remove range i+1 */
                memmove(&ack->recv_ranges[i + 1], &ack->recv_ranges[i + 2],
                        (ack->recv_range_count - i - 2) * sizeof(nxp_recv_pn_range));
                ack->recv_range_count--;
            }
            inserted = true;
            break;
        }
    }

    if (!inserted && ack->recv_range_count < NXP_MAX_RECV_RANGES) {
        /* Find insertion point (maintain descending order by start) */
        uint32_t pos = 0;
        while (pos < ack->recv_range_count &&
               ack->recv_ranges[pos].start > pkt_num) {
            pos++;
        }
        /* Shift everything after pos down */
        if (pos < ack->recv_range_count) {
            memmove(&ack->recv_ranges[pos + 1], &ack->recv_ranges[pos],
                    (ack->recv_range_count - pos) * sizeof(nxp_recv_pn_range));
        }
        ack->recv_ranges[pos].start = pkt_num;
        ack->recv_ranges[pos].end   = pkt_num;
        ack->recv_range_count++;
    }

    if (ack_eliciting) {
        ack->ack_needed = true;
        ack->ack_delay_timer = now_us + NXP_ACK_DELAY_US;
    }
}

/* ── Sent Packet Tracking ─────────────────────────────── */

void nxp_ack_on_pkt_sent(nxp_ack_state *ack, const nxp_sent_pkt *pkt) {
    /* Grow the sent array if needed */
    if (ack->sent_count >= ack->sent_cap) {
        uint32_t new_cap = ack->sent_cap * 2;
        nxp_sent_pkt *new_sent = (nxp_sent_pkt *)realloc(
            ack->sent, new_cap * sizeof(nxp_sent_pkt));
        if (new_sent == nullptr) return;
        ack->sent     = new_sent;
        ack->sent_cap = new_cap;
    }

    ack->sent[ack->sent_count] = *pkt;
    ack->sent_count++;

    if (pkt->in_flight) {
        ack->bytes_in_flight += pkt->sent_bytes;
    }
    if (pkt->ack_eliciting) {
        ack->time_of_last_ack_eliciting = pkt->sent_time;
    }
}

/* ── RTT Update ───────────────────────────────────────── */

static void update_rtt(nxp_ack_state *ack, uint64_t latest_rtt,
                       uint64_t ack_delay) {
    ack->latest_rtt = latest_rtt;
    ack->min_rtt    = u64_min(ack->min_rtt, latest_rtt);

    /* Cap ack_delay to max_ack_delay after handshake */
    if (ack_delay > NXP_ACK_DELAY_US) {
        ack_delay = NXP_ACK_DELAY_US;
    }

    if (!ack->has_rtt) {
        /* First RTT sample */
        ack->smoothed_rtt = latest_rtt;
        ack->rtt_var      = latest_rtt / 2;
        ack->has_rtt      = true;
    } else {
        /* Subtract ack_delay only if it doesn't bring us below min_rtt */
        uint64_t adjusted = latest_rtt;
        if (adjusted > ack_delay && (adjusted - ack_delay) >= ack->min_rtt) {
            adjusted -= ack_delay;
        }

        /* EWMA: rtt_var = 3/4 * rtt_var + 1/4 * |srtt - adjusted| */
        uint64_t diff = (ack->smoothed_rtt > adjusted)
                        ? (ack->smoothed_rtt - adjusted)
                        : (adjusted - ack->smoothed_rtt);
        ack->rtt_var = (3 * ack->rtt_var + diff) / 4;

        /* EWMA: srtt = 7/8 * srtt + 1/8 * adjusted */
        ack->smoothed_rtt = (7 * ack->smoothed_rtt + adjusted) / 8;
    }
}

/* ── Loss Detection ───────────────────────────────────── */

static void detect_lost_packets(nxp_ack_state *ack, uint64_t now_us,
                                uint64_t largest_acked_pn,
                                nxp_loss_cb on_loss, void *ctx) {
    ack->loss_time = UINT64_MAX;

    /* Time threshold: 9/8 * max(srtt, latest_rtt) */
    uint64_t max_rtt = u64_max(ack->smoothed_rtt, ack->latest_rtt);
    uint64_t loss_delay = max_rtt * NXP_LOSS_DELAY_FACTOR / 8;
    /* Minimum of 1ms */
    if (loss_delay < 1000) loss_delay = 1000;

    /* Walk the sent packets array */
    uint32_t write_idx = 0;
    for (uint32_t i = 0; i < ack->sent_count; i++) {
        nxp_sent_pkt *pkt = &ack->sent[i];

        /* Skip already declared lost packets */
        if (pkt->declared_lost) {
            continue;
        }

        /* Only consider packets older than largest_acked */
        if (pkt->pkt_num > largest_acked_pn) {
            ack->sent[write_idx++] = *pkt;
            continue;
        }

        /* Packet reorder threshold: lost if pkt_num + threshold <= largest_acked */
        bool reorder_lost = (pkt->pkt_num + NXP_PACKET_REORDER_THRESH <= largest_acked_pn);

        /* Time threshold: lost if sent_time + loss_delay <= now */
        bool time_lost = (pkt->sent_time + loss_delay <= now_us);

        if (reorder_lost || time_lost) {
            pkt->declared_lost = true;
            if (pkt->in_flight) {
                if (ack->bytes_in_flight >= pkt->sent_bytes) {
                    ack->bytes_in_flight -= pkt->sent_bytes;
                }
            }
            if (on_loss != nullptr) {
                on_loss(ctx, pkt);
            }
            /* Don't copy to write position - packet is removed */
        } else {
            /* Not yet lost, but set loss_time for timer */
            uint64_t deadline = pkt->sent_time + loss_delay;
            ack->loss_time = u64_min(ack->loss_time, deadline);
            ack->sent[write_idx++] = *pkt;
        }
    }
    ack->sent_count = write_idx;
}

/* ── Process Incoming ACK Frame ───────────────────────── */

void nxp_ack_on_ack_recv(nxp_ack_state *ack, const nxp_frame_ack *frame,
                          uint64_t now_us,
                          nxp_ack_cb on_ack, nxp_loss_cb on_loss, void *ctx) {
    uint64_t largest = frame->largest_acked;

    /* Build a quick lookup: iterate ACK ranges and mark acked packets.
     *
     * ACK frame structure (QUIC-style):
     *   largest_acked
     *   first_ack_range  => [largest - first_ack_range, largest]
     *   then for each range:
     *     gap: skip (gap + 1) packet numbers
     *     ack_range: this many additional packets acked
     */

    /* Walk through sent packets, matching against ACK ranges */
    /* First, determine the ranges from the ACK frame */

    /* Range 0: [largest - first_ack_range, largest] */
    uint64_t range_end   = largest;
    uint64_t range_start = largest - frame->first_ack_range;

    /* Track if we got an RTT sample */
    bool got_rtt_sample = false;
    uint64_t rtt_sample = 0;

    /* Process sent packets */
    uint32_t write_idx = 0;
    for (uint32_t i = 0; i < ack->sent_count; i++) {
        nxp_sent_pkt *pkt = &ack->sent[i];

        /* Check if this pkt_num falls in any ACK range */
        bool acked = false;

        /* Check the first range */
        uint64_t re = largest;
        uint64_t rs = largest - frame->first_ack_range;

        if (pkt->pkt_num >= rs && pkt->pkt_num <= re) {
            acked = true;
        }

        /* Check additional ranges */
        if (!acked) {
            uint64_t cur_end = rs;
            for (uint32_t r = 0; r < frame->range_count && !acked; r++) {
                /* Gap: skip (gap + 1) packets below cur_end */
                if (cur_end < frame->ranges[r].gap + 2) break;
                cur_end = cur_end - frame->ranges[r].gap - 2;
                /* Range: [cur_end - ack_range, cur_end] */
                uint64_t r_start = (cur_end >= frame->ranges[r].ack_range)
                                   ? cur_end - frame->ranges[r].ack_range
                                   : 0;
                if (pkt->pkt_num >= r_start && pkt->pkt_num <= cur_end) {
                    acked = true;
                }
                cur_end = r_start;
            }
        }

        if (acked) {
            /* RTT sample from the largest newly acked */
            if (pkt->pkt_num == largest && pkt->ack_eliciting) {
                rtt_sample = now_us - pkt->sent_time;
                got_rtt_sample = true;
            }

            if (pkt->in_flight) {
                if (ack->bytes_in_flight >= pkt->sent_bytes) {
                    ack->bytes_in_flight -= pkt->sent_bytes;
                }
            }

            if (on_ack != nullptr) {
                on_ack(ctx, pkt);
            }
            /* Packet is acked - remove from sent list */
        } else {
            /* Not acked - keep in sent list */
            ack->sent[write_idx++] = *pkt;
        }
    }
    ack->sent_count = write_idx;

    /* Update RTT */
    if (got_rtt_sample) {
        update_rtt(ack, rtt_sample, frame->ack_delay);
    }

    /* PTO reset on ACK */
    ack->pto_count = 0;

    /* Detect lost packets */
    detect_lost_packets(ack, now_us, largest, on_loss, ctx);

    (void)range_end;
    (void)range_start;
}

/* ── Build ACK Frame ──────────────────────────────────── */

bool nxp_ack_build_frame(const nxp_ack_state *ack, nxp_frame_ack *out,
                          uint64_t now_us) {
    if (ack->recv_range_count == 0) return false;

    memset(out, 0, sizeof(*out));

    /* Ranges are stored in descending order by start.
     * First range (index 0) has the largest packet numbers. */
    out->largest_acked = ack->recv_ranges[0].end;

    /* ACK delay since we received the largest */
    if (now_us > ack->largest_recv_time) {
        out->ack_delay = now_us - ack->largest_recv_time;
    }

    /* First ACK range */
    out->first_ack_range = ack->recv_ranges[0].end - ack->recv_ranges[0].start;

    /* Additional ranges (convert our [start,end] pairs to gap/range format) */
    out->range_count = 0;
    for (uint32_t i = 1; i < ack->recv_range_count && out->range_count < NXP_ACK_MAX_RANGES; i++) {
        /* Gap: number of unacked packets between previous range start and this range end */
        uint64_t prev_start = ack->recv_ranges[i - 1].start;
        uint64_t this_end   = ack->recv_ranges[i].end;

        /* gap = prev_start - this_end - 2 (the encoding is gap+1 missing packets) */
        if (prev_start < this_end + 2) break;
        out->ranges[out->range_count].gap       = prev_start - this_end - 2;
        out->ranges[out->range_count].ack_range  = ack->recv_ranges[i].end - ack->recv_ranges[i].start;
        out->range_count++;
    }

    return true;
}

/* ── Loss Timer ───────────────────────────────────────── */

uint64_t nxp_ack_loss_timeout(const nxp_ack_state *ack) {
    /* If there's a time-based loss deadline, use it */
    if (ack->loss_time != UINT64_MAX) {
        return ack->loss_time;
    }

    /* If no ack-eliciting packets in flight, no timer needed */
    if (ack->bytes_in_flight == 0) {
        return UINT64_MAX;
    }

    /* PTO timer */
    uint64_t pto = compute_pto(ack);
    uint64_t timeout = ack->time_of_last_ack_eliciting + pto * ((uint64_t)1 << ack->pto_count);
    return timeout;
}

void nxp_ack_on_loss_timeout(nxp_ack_state *ack, uint64_t now_us,
                              nxp_loss_cb on_loss, void *ctx) {
    if (ack->loss_time != UINT64_MAX && now_us >= ack->loss_time) {
        /* Time-based loss: find the largest acked pkt_num we know about */
        uint64_t largest = 0;
        for (uint32_t i = 0; i < ack->sent_count; i++) {
            if (ack->sent[i].pkt_num > largest) {
                largest = ack->sent[i].pkt_num;
            }
        }
        detect_lost_packets(ack, now_us, largest, on_loss, ctx);
        return;
    }

    /* PTO timeout - mark as probe needed */
    ack->pto_count++;
}
