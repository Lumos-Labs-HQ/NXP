/*
 * NXP BBR Congestion Control - Implementation
 *
 * Phase 7: Full BBR v1 state machine.
 *
 * BBR models the network path with two parameters:
 *   btl_bw  (bottleneck bandwidth) - max delivery rate over ~10 rounds
 *   rt_prop (propagation delay)    - min RTT over ~10 seconds
 *
 * From these, BBR computes:
 *   pacing_rate = btl_bw * pacing_gain
 *   cwnd        = btl_bw * rt_prop * cwnd_gain
 *
 * State machine:
 *   STARTUP  - exponential growth until pipe is filled
 *   DRAIN    - drain the queue created during startup
 *   PROBE_BW - steady state with gain cycling
 *   PROBE_RTT - periodically probe for lower RTT
 */
#include "bbr_internal.h"
#include "cc_interface.h"

#include <stdlib.h>
#include <string.h>

/* ── Helpers ──────────────────────────────────────────── */

/* Integer multiply with gain: val * num / den */
static uint64_t apply_gain(uint64_t val, uint32_t num, uint32_t den) {
    if (den == 0) return val;
    return (val * (uint64_t)num) / (uint64_t)den;
}

static uint64_t bbr_bdp(const nxp_bbr *bbr) {
    /* BDP = btl_bw * rt_prop / 1e6 (since rt_prop is in us) */
    if (bbr->btl_bw == 0 || bbr->rt_prop_us == 0) return NXP_BBR_MIN_CWND;
    uint64_t bdp = (bbr->btl_bw * bbr->rt_prop_us) / 1000000ULL;
    return bdp > NXP_BBR_MIN_CWND ? bdp : NXP_BBR_MIN_CWND;
}

static uint64_t bbr_inflight(const nxp_bbr *bbr, uint32_t gain_num,
                               uint32_t gain_den) {
    uint64_t bdp = bbr_bdp(bbr);
    return apply_gain(bdp, gain_num, gain_den);
}

/* ── State Initialization ─────────────────────────────── */

void nxp_bbr_init_state(nxp_bbr *bbr) {
    memset(bbr, 0, sizeof(*bbr));

    bbr->state = NXP_BBR_STARTUP;

    /* Initialize windowed max-BW filter (window = 10 rounds) */
    nxp_wf_init(&bbr->bw_filter, NXP_BBR_BW_WINDOW_ROUNDS, true);

    /* Startup gains */
    bbr->pacing_gain_num = NXP_BBR_STARTUP_PACING_GAIN_NUM;
    bbr->pacing_gain_den = NXP_BBR_STARTUP_PACING_GAIN_DEN;
    bbr->cwnd_gain_num   = NXP_BBR_STARTUP_CWND_GAIN_NUM;
    bbr->cwnd_gain_den   = NXP_BBR_STARTUP_CWND_GAIN_DEN;

    /* Initial cwnd: 10 packets (typical IW) */
    bbr->cwnd = 10 * NXP_BBR_MAX_DATAGRAM_SIZE;
    bbr->send_quantum = NXP_BBR_MAX_DATAGRAM_SIZE;

    bbr->initialized = true;
}

/* ── Model Update ─────────────────────────────────────── */

void nxp_bbr_update_model(nxp_bbr *bbr, const nxp_cc_ack_info *info) {
    /* Update round tracking from delivery rate */
    bbr->round_count = info->round_count;
    bbr->round_start = info->round_start;
    bbr->app_limited = info->is_app_limited;

    /* Update bottleneck bandwidth estimate */
    if (info->delivery_rate_bps > 0) {
        /* Only use non-app-limited samples, or if they beat current max */
        if (!info->is_app_limited ||
            info->delivery_rate_bps > nxp_wf_get(&bbr->bw_filter)) {
            nxp_wf_update(&bbr->bw_filter, info->delivery_rate_bps,
                           bbr->round_count);
        }
        bbr->btl_bw = nxp_wf_get(&bbr->bw_filter);
    }

    /* Update min RTT (rt_prop) */
    if (info->rtt_us > 0) {
        bool rtt_expired = (info->now_us - bbr->rt_prop_stamp) >
                            NXP_BBR_RTPROP_WINDOW_US;

        if (info->rtt_us <= bbr->rt_prop_us || bbr->rt_prop_us == 0 ||
            rtt_expired) {
            bbr->rt_prop_us = info->rtt_us;
            bbr->rt_prop_stamp = info->now_us;
        }

        bbr->rt_prop_expired = rtt_expired;
    }
}

/* ── Startup -> Drain Transition ──────────────────────── */

void nxp_bbr_check_startup_done(nxp_bbr *bbr) {
    if (bbr->state != NXP_BBR_STARTUP || bbr->filled_pipe) return;

    if (!bbr->round_start) return;

    /* Check if BW has plateaued: growth < 25% over last round */
    if (bbr->btl_bw >= apply_gain(bbr->full_bw,
                                    NXP_BBR_STARTUP_GROWTH_TARGET_NUM,
                                    NXP_BBR_STARTUP_GROWTH_TARGET_DEN)) {
        /* Still growing */
        bbr->full_bw = bbr->btl_bw;
        bbr->full_bw_count = 0;
        return;
    }

    /* Not growing enough */
    bbr->full_bw_count++;
    if (bbr->full_bw_count >= NXP_BBR_STARTUP_FULL_LOSS_COUNT) {
        bbr->filled_pipe = true;

        /* Transition to DRAIN */
        bbr->state = NXP_BBR_DRAIN;
        bbr->pacing_gain_num = NXP_BBR_DRAIN_PACING_GAIN_NUM;
        bbr->pacing_gain_den = NXP_BBR_DRAIN_PACING_GAIN_DEN;
        /* Keep startup cwnd gain during drain */
    }
}

/* ── Drain -> ProbeBW Transition ──────────────────────── */

void nxp_bbr_check_drain_done(nxp_bbr *bbr) {
    if (bbr->state != NXP_BBR_DRAIN) return;

    /* Exit drain when bytes_in_flight <= BDP */
    if (bbr->bytes_in_flight <= bbr_bdp(bbr)) {
        bbr->state = NXP_BBR_PROBE_BW;
        bbr->cycle_index = 0;
        bbr->pacing_gain_num = NXP_BBR_PROBE_BW_UP_NUM;
        bbr->pacing_gain_den = NXP_BBR_PROBE_BW_UP_DEN;
        bbr->cwnd_gain_num = 200;
        bbr->cwnd_gain_den = 100;
    }
}

/* ── ProbeBW Gain Cycling ─────────────────────────────── */

/* Gain cycle phases: [UP, DOWN, CRUISE x6] */
static const uint32_t probe_bw_gain_num[NXP_BBR_PROBE_BW_PHASES] = {
    125, 75, 100, 100, 100, 100, 100, 100
};
static const uint32_t probe_bw_gain_den[NXP_BBR_PROBE_BW_PHASES] = {
    100, 100, 100, 100, 100, 100, 100, 100
};

void nxp_bbr_update_probe_bw(nxp_bbr *bbr, uint64_t now_us) {
    if (bbr->state != NXP_BBR_PROBE_BW) return;

    /* Advance cycle every ~1 RTT */
    bool advance = false;
    if (bbr->rt_prop_us > 0 &&
        (now_us - bbr->cycle_stamp) >= bbr->rt_prop_us) {
        advance = true;
    }

    if (advance) {
        bbr->cycle_index = (uint8_t)((bbr->cycle_index + 1) % NXP_BBR_PROBE_BW_PHASES);
        bbr->cycle_stamp = now_us;
        bbr->pacing_gain_num = probe_bw_gain_num[bbr->cycle_index];
        bbr->pacing_gain_den = probe_bw_gain_den[bbr->cycle_index];
    }
}

/* ── ProbeRTT Check ───────────────────────────────────── */

void nxp_bbr_check_probe_rtt(nxp_bbr *bbr, uint64_t now_us) {
    /* Enter ProbeRTT if min RTT window has expired */
    if (bbr->state != NXP_BBR_PROBE_RTT && bbr->rt_prop_expired &&
        !bbr->app_limited) {
        /* Save current cwnd */
        bbr->prior_cwnd = bbr->cwnd;

        bbr->state = NXP_BBR_PROBE_RTT;
        bbr->pacing_gain_num = 100;
        bbr->pacing_gain_den = 100;
        bbr->probe_rtt_done = false;
    }

    if (bbr->state != NXP_BBR_PROBE_RTT) return;

    /* Reduce cwnd to minimum */
    uint64_t probe_cwnd = (uint64_t)NXP_BBR_PROBE_RTT_CWND_PKTS *
                           NXP_BBR_MAX_DATAGRAM_SIZE;
    if (bbr->cwnd > probe_cwnd) {
        bbr->cwnd = probe_cwnd;
    }

    /* Wait for at least 200ms at reduced cwnd */
    if (bbr->bytes_in_flight <= probe_cwnd) {
        if (!bbr->probe_rtt_done) {
            bbr->probe_rtt_done = true;
            bbr->probe_rtt_done_stamp = now_us;
        }

        if (bbr->probe_rtt_done &&
            (now_us - bbr->probe_rtt_done_stamp) >= NXP_BBR_PROBE_RTT_DURATION_US) {
            /* Done probing RTT - restore cwnd and return to ProbeBW */
            bbr->rt_prop_expired = false;
            bbr->cwnd = bbr->prior_cwnd > NXP_BBR_MIN_CWND
                ? bbr->prior_cwnd : NXP_BBR_MIN_CWND;

            if (bbr->filled_pipe) {
                bbr->state = NXP_BBR_PROBE_BW;
                bbr->cycle_index = 0;
                bbr->cycle_stamp = now_us;
                bbr->pacing_gain_num = probe_bw_gain_num[0];
                bbr->pacing_gain_den = probe_bw_gain_den[0];
                bbr->cwnd_gain_num = 200;
                bbr->cwnd_gain_den = 100;
            } else {
                bbr->state = NXP_BBR_STARTUP;
                bbr->pacing_gain_num = NXP_BBR_STARTUP_PACING_GAIN_NUM;
                bbr->pacing_gain_den = NXP_BBR_STARTUP_PACING_GAIN_DEN;
                bbr->cwnd_gain_num = NXP_BBR_STARTUP_CWND_GAIN_NUM;
                bbr->cwnd_gain_den = NXP_BBR_STARTUP_CWND_GAIN_DEN;
            }
        }
    }
}

/* ── Control Parameter Update ─────────────────────────── */

void nxp_bbr_update_control(nxp_bbr *bbr) {
    /* Pacing rate = btl_bw * pacing_gain */
    bbr->pacing_rate = apply_gain(bbr->btl_bw,
                                    bbr->pacing_gain_num, bbr->pacing_gain_den);

    /* Target cwnd = BDP * cwnd_gain */
    bbr->target_cwnd = bbr_inflight(bbr, bbr->cwnd_gain_num, bbr->cwnd_gain_den);
    if (bbr->target_cwnd < NXP_BBR_MIN_CWND) {
        bbr->target_cwnd = NXP_BBR_MIN_CWND;
    }

    /* Update cwnd (don't reduce below target in non-ProbeRTT states) */
    if (bbr->state != NXP_BBR_PROBE_RTT) {
        if (bbr->cwnd < bbr->target_cwnd) {
            bbr->cwnd = bbr->target_cwnd;
        }
        /* Also allow cwnd reduction if it's far above target */
        if (bbr->filled_pipe && bbr->cwnd > bbr->target_cwnd * 2) {
            bbr->cwnd = bbr->target_cwnd;
        }
    }

    /* Send quantum: max burst, capped at pacing_rate * 1ms or 2 packets */
    uint64_t quantum = bbr->pacing_rate / 1000; /* 1ms worth */
    if (quantum < (uint64_t)2 * NXP_BBR_MAX_DATAGRAM_SIZE) {
        quantum = (uint64_t)2 * NXP_BBR_MAX_DATAGRAM_SIZE;
    }
    if (quantum > 64 * 1024) {
        quantum = 64 * 1024; /* Cap at 64 KB */
    }
    bbr->send_quantum = quantum;
}

/* ── CC Interface Implementation ──────────────────────── */

static void *bbr_create(void) {
    nxp_bbr *bbr = (nxp_bbr *)calloc(1, sizeof(nxp_bbr));
    if (bbr != nullptr) {
        nxp_bbr_init_state(bbr);
    }
    return bbr;
}

static void bbr_destroy(void *state) {
    free(state);
}

static void bbr_on_sent(void *state, uint32_t sent_bytes, uint64_t now_us,
                          uint64_t bytes_in_flight) {
    nxp_bbr *bbr = (nxp_bbr *)state;
    bbr->bytes_in_flight = bytes_in_flight;

    /* Take delivery rate snapshot for this packet.
     * The caller should store the returned pkt_state with the sent packet.
     * Since we can't modify the sent_pkt struct here, the connection layer
     * will call nxp_dr_on_packet_sent directly. */
    (void)sent_bytes;
    (void)now_us;
}

static void bbr_on_ack(void *state, const nxp_cc_ack_info *info) {
    nxp_bbr *bbr = (nxp_bbr *)state;

    bbr->bytes_in_flight = info->bytes_in_flight;

    /* The delivery rate was already updated by the connection layer
     * (it called nxp_dr_on_packet_acked). Update the BBR model from ack_info. */
    nxp_bbr_update_model(bbr, info);

    /* Run state machine */
    nxp_bbr_check_startup_done(bbr);
    nxp_bbr_check_drain_done(bbr);
    nxp_bbr_update_probe_bw(bbr, info->now_us);
    nxp_bbr_check_probe_rtt(bbr, info->now_us);

    /* Update control parameters */
    nxp_bbr_update_control(bbr);
}

static void bbr_on_loss(void *state, const nxp_cc_loss_info *info) {
    nxp_bbr *bbr = (nxp_bbr *)state;
    bbr->bytes_in_flight = info->bytes_in_flight;
    bbr->loss_in_round += info->lost_bytes;

    /* In startup, persistent loss also indicates pipe is full */
    if (bbr->state == NXP_BBR_STARTUP && !bbr->filled_pipe) {
        /* If we lost a significant fraction, declare pipe full */
        if (bbr->bytes_in_flight > 0 &&
            bbr->loss_in_round > bbr->bytes_in_flight / 5) {
            bbr->filled_pipe = true;
            bbr->state = NXP_BBR_DRAIN;
            bbr->pacing_gain_num = NXP_BBR_DRAIN_PACING_GAIN_NUM;
            bbr->pacing_gain_den = NXP_BBR_DRAIN_PACING_GAIN_DEN;
        }
    }
}

static uint64_t bbr_get_cwnd(const void *state) {
    const nxp_bbr *bbr = (const nxp_bbr *)state;
    return bbr->cwnd;
}

static uint64_t bbr_get_pacing_rate(const void *state) {
    const nxp_bbr *bbr = (const nxp_bbr *)state;
    return bbr->pacing_rate;
}

static bool bbr_in_slow_start(const void *state) {
    const nxp_bbr *bbr = (const nxp_bbr *)state;
    return bbr->state == NXP_BBR_STARTUP;
}

static uint64_t bbr_get_send_quantum(const void *state) {
    const nxp_bbr *bbr = (const nxp_bbr *)state;
    return bbr->send_quantum;
}

/* ── Exported CC Operations ───────────────────────────── */

const nxp_cc_ops nxp_cc_bbr = {
    .name           = "BBR",
    .create         = bbr_create,
    .destroy        = bbr_destroy,
    .on_sent        = bbr_on_sent,
    .on_ack         = bbr_on_ack,
    .on_loss        = bbr_on_loss,
    .get_cwnd       = bbr_get_cwnd,
    .get_pacing_rate = bbr_get_pacing_rate,
    .in_slow_start  = bbr_in_slow_start,
    .get_send_quantum = bbr_get_send_quantum,
};
