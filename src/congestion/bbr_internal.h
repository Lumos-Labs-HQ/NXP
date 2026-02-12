/*
 * NXP BBR Congestion Control - Internal Header
 *
 * Phase 7: BBR v1 state machine.
 *
 * BBR models the network with two parameters:
 *   btl_bw  - bottleneck bandwidth (max delivery rate)
 *   rt_prop - round-trip propagation delay (min RTT)
 *
 * States: STARTUP -> DRAIN -> PROBE_BW <-> PROBE_RTT
 */
#ifndef NXP_BBR_INTERNAL_H
#define NXP_BBR_INTERNAL_H

#include "windowed_filter.h"
#include "delivery_rate.h"
#include "cc_interface.h"

#include <stdint.h>
#include <stdbool.h>

/* ── BBR Constants ────────────────────────────────────── */

/* Startup: high gain to quickly fill the pipe */
#define NXP_BBR_STARTUP_PACING_GAIN_NUM   277
#define NXP_BBR_STARTUP_PACING_GAIN_DEN   100
#define NXP_BBR_STARTUP_CWND_GAIN_NUM     200
#define NXP_BBR_STARTUP_CWND_GAIN_DEN     100

/* Drain: reciprocal of startup gain */
#define NXP_BBR_DRAIN_PACING_GAIN_NUM     100
#define NXP_BBR_DRAIN_PACING_GAIN_DEN     277

/* ProbeBW gain cycle (8 phases): UP, DOWN, then 6x CRUISE */
#define NXP_BBR_PROBE_BW_PHASES           8
#define NXP_BBR_PROBE_BW_UP_NUM           125
#define NXP_BBR_PROBE_BW_UP_DEN           100
#define NXP_BBR_PROBE_BW_DOWN_NUM         75
#define NXP_BBR_PROBE_BW_DOWN_DEN         100

/* ProbeRTT: min cwnd to probe for lower RTT */
#define NXP_BBR_PROBE_RTT_CWND_PKTS       4
#define NXP_BBR_PROBE_RTT_DURATION_US      200000ULL   /* 200ms */

/* Window sizes */
#define NXP_BBR_BW_WINDOW_ROUNDS           10
#define NXP_BBR_RTPROP_WINDOW_US           10000000ULL /* 10 seconds */

/* Pipe fullness detection in startup */
#define NXP_BBR_STARTUP_GROWTH_TARGET_NUM  125
#define NXP_BBR_STARTUP_GROWTH_TARGET_DEN  100
#define NXP_BBR_STARTUP_FULL_LOSS_COUNT    3

/* Min cwnd */
#define NXP_BBR_MIN_CWND                   (4 * 1200)  /* 4 packets */

/* Max datagram size for cwnd computations */
#define NXP_BBR_MAX_DATAGRAM_SIZE          1200

/* ── BBR States ───────────────────────────────────────── */

typedef enum nxp_bbr_state {
    NXP_BBR_STARTUP,
    NXP_BBR_DRAIN,
    NXP_BBR_PROBE_BW,
    NXP_BBR_PROBE_RTT,
} nxp_bbr_state;

/* ── BBR State ────────────────────────────────────────── */

typedef struct nxp_bbr {
    nxp_bbr_state state;

    /* The BBR model */
    nxp_windowed_filter bw_filter;   /* Max BW filter (rounds) */
    uint64_t            btl_bw;      /* Bottleneck bandwidth (bytes/sec) */
    uint64_t            rt_prop_us;  /* Min round-trip propagation (us) */
    uint64_t            rt_prop_stamp; /* When rt_prop was last updated */

    /* Pacing and cwnd */
    uint64_t pacing_rate;            /* bytes/second */
    uint64_t cwnd;                   /* bytes */
    uint64_t send_quantum;           /* max burst size (bytes) */
    uint64_t target_cwnd;            /* cwnd target for current state */

    /* Gain factors (numerator/denominator for integer math) */
    uint32_t pacing_gain_num;
    uint32_t pacing_gain_den;
    uint32_t cwnd_gain_num;
    uint32_t cwnd_gain_den;

    /* ProbeBW gain cycling */
    uint8_t  cycle_index;            /* 0-7, current phase */
    uint64_t cycle_stamp;            /* When current phase started */

    /* Startup pipe-filling detection */
    bool     filled_pipe;
    uint64_t full_bw;                /* BW when we last checked growth */
    uint32_t full_bw_count;          /* Rounds without significant growth */

    /* ProbeRTT state */
    bool     probe_rtt_done;
    uint64_t probe_rtt_done_stamp;
    bool     rt_prop_expired;

    /* Round tracking (updated from delivery rate via ack_info) */
    uint64_t round_count;
    bool     round_start;

    /* App-limited state (from delivery rate) */
    bool     app_limited;

    /* Bytes in flight tracking */
    uint64_t bytes_in_flight;
    uint64_t prior_cwnd;             /* Saved cwnd before ProbeRTT */

    /* Loss tracking */
    uint64_t loss_in_round;
    uint64_t loss_round_start;

    bool initialized;
} nxp_bbr;

/* ── Internal BBR Functions ───────────────────────────── */

void nxp_bbr_init_state(nxp_bbr *bbr);
void nxp_bbr_update_model(nxp_bbr *bbr, const nxp_cc_ack_info *info);
void nxp_bbr_update_control(nxp_bbr *bbr);
void nxp_bbr_check_startup_done(nxp_bbr *bbr);
void nxp_bbr_check_drain_done(nxp_bbr *bbr);
void nxp_bbr_update_probe_bw(nxp_bbr *bbr, uint64_t now_us);
void nxp_bbr_check_probe_rtt(nxp_bbr *bbr, uint64_t now_us);

#endif /* NXP_BBR_INTERNAL_H */
