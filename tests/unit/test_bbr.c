/*
 * NXP BBR Congestion Control Tests
 *
 * Phase 7: Tests for windowed filter, delivery rate estimation,
 * pacing, and the BBR state machine.
 */
#include "test_framework.h"
#include "congestion/windowed_filter.h"
#include "congestion/delivery_rate.h"
#include "congestion/pacing.h"
#include "congestion/bbr_internal.h"
#include "congestion/cc_interface.h"
#include <string.h>

/* ── Test: Windowed Min Filter ─────────────────────────── */

NXP_TEST(windowed_min_filter) {
    nxp_windowed_filter f;
    nxp_wf_init(&f, 10, false); /* min filter, window=10 */

    nxp_wf_update(&f, 100, 1);
    NXP_ASSERT_EQ(nxp_wf_get(&f), (uint64_t)100);

    nxp_wf_update(&f, 50, 2);
    NXP_ASSERT_EQ(nxp_wf_get(&f), (uint64_t)50);

    /* Value of 200 should not change min */
    nxp_wf_update(&f, 200, 3);
    NXP_ASSERT_EQ(nxp_wf_get(&f), (uint64_t)50);

    /* New min */
    nxp_wf_update(&f, 30, 4);
    NXP_ASSERT_EQ(nxp_wf_get(&f), (uint64_t)30);

    /* After window expires, old min should be evicted */
    nxp_wf_update(&f, 80, 15); /* time=15, window=10, so t=4 value expires */
    NXP_ASSERT_EQ(nxp_wf_get(&f), (uint64_t)80);
}

/* ── Test: Windowed Max Filter ─────────────────────────── */

NXP_TEST(windowed_max_filter) {
    nxp_windowed_filter f;
    nxp_wf_init(&f, 10, true); /* max filter, window=10 */

    nxp_wf_update(&f, 100, 1);
    NXP_ASSERT_EQ(nxp_wf_get(&f), (uint64_t)100);

    nxp_wf_update(&f, 200, 2);
    NXP_ASSERT_EQ(nxp_wf_get(&f), (uint64_t)200);

    nxp_wf_update(&f, 150, 3);
    NXP_ASSERT_EQ(nxp_wf_get(&f), (uint64_t)200);

    /* After window expires */
    nxp_wf_update(&f, 120, 13);
    NXP_ASSERT_EQ(nxp_wf_get(&f), (uint64_t)120);
}

/* ── Test: Delivery Rate Basic ─────────────────────────── */

NXP_TEST(delivery_rate_basic) {
    nxp_delivery_rate dr;
    nxp_dr_init(&dr);

    /* Send packet 1 at t=0 */
    nxp_dr_pkt_state pkt1;
    nxp_dr_on_packet_sent(&dr, 1200, 0, &pkt1);

    /* Send packet 2 at t=1000 (1ms) */
    nxp_dr_pkt_state pkt2;
    nxp_dr_on_packet_sent(&dr, 1200, 1000, &pkt2);

    /* ACK packet 1 at t=10000 (10ms) - simulates 10ms RTT */
    nxp_dr_on_packet_acked(&dr, &pkt1, 1200, 10000);

    NXP_ASSERT(dr.delivered == 1200);
    NXP_ASSERT(dr.rate_sample_bps > 0);

    /* ACK packet 2 at t=11000 */
    nxp_dr_on_packet_acked(&dr, &pkt2, 1200, 11000);

    NXP_ASSERT(dr.delivered == 2400);
    NXP_ASSERT(dr.rate_sample_bps > 0);
}

/* ── Test: Delivery Rate Round Counting ────────────────── */

NXP_TEST(delivery_rate_rounds) {
    nxp_delivery_rate dr;
    nxp_dr_init(&dr);

    NXP_ASSERT_EQ(dr.round_count, (uint64_t)0);

    /* Send and ACK to advance rounds */
    nxp_dr_pkt_state pkt;
    nxp_dr_on_packet_sent(&dr, 1200, 0, &pkt);
    nxp_dr_on_packet_acked(&dr, &pkt, 1200, 10000);

    /* After first ACK, round_count should advance */
    NXP_ASSERT(dr.round_count >= 1);
}

/* ── Test: Delivery Rate App Limited ───────────────────── */

NXP_TEST(delivery_rate_app_limited) {
    nxp_delivery_rate dr;
    nxp_dr_init(&dr);

    NXP_ASSERT(!dr.app_limited);
    nxp_dr_set_app_limited(&dr);
    NXP_ASSERT(dr.app_limited);
    nxp_dr_clear_app_limited(&dr);
    NXP_ASSERT(!dr.app_limited);
}

/* ── Test: Pacer Init ──────────────────────────────────── */

NXP_TEST(pacer_init) {
    nxp_pacer p;
    nxp_pacer_init(&p);

    /* Not enabled yet (rate = 0) */
    NXP_ASSERT(!p.enabled);

    /* Can always send when disabled */
    NXP_ASSERT(nxp_pacer_can_send(&p, 1200));
}

/* ── Test: Pacer Rate Limiting ─────────────────────────── */

NXP_TEST(pacer_rate_limiting) {
    nxp_pacer p;
    nxp_pacer_init(&p);

    /* Set rate to 1 MB/s */
    nxp_pacer_set_rate(&p, 1000000);
    NXP_ASSERT(p.enabled);

    /* Update at t=0 (initializes) */
    nxp_pacer_update(&p, 0);

    /* Can send initially (have burst tokens) */
    NXP_ASSERT(nxp_pacer_can_send(&p, 1200));

    /* Send a bunch to drain tokens */
    for (int i = 0; i < 10; i++) {
        nxp_pacer_on_send(&p, 1200);
    }

    /* Tokens should be exhausted */
    NXP_ASSERT(!nxp_pacer_can_send(&p, 1200));

    /* After some time, tokens refill */
    nxp_pacer_update(&p, 100000); /* 100ms at 1MB/s = 100KB of tokens */
    NXP_ASSERT(nxp_pacer_can_send(&p, 1200));
}

/* ── Test: Pacer Next Send Time ────────────────────────── */

NXP_TEST(pacer_next_send_time) {
    nxp_pacer p;
    nxp_pacer_init(&p);

    nxp_pacer_set_rate(&p, 1000000);
    nxp_pacer_update(&p, 0);

    /* Next send time should be 0 when we have tokens */
    NXP_ASSERT_EQ(nxp_pacer_next_send_time(&p, 1200), (uint64_t)0);

    /* Drain all tokens */
    for (int i = 0; i < 10; i++) {
        nxp_pacer_on_send(&p, 1200);
    }

    /* Next send time should be > 0 */
    uint64_t next = nxp_pacer_next_send_time(&p, 1200);
    NXP_ASSERT(next > 0);
}

/* ── Test: BBR Init ────────────────────────────────────── */

NXP_TEST(bbr_init) {
    nxp_bbr bbr;
    nxp_bbr_init_state(&bbr);

    NXP_ASSERT(bbr.initialized);
    NXP_ASSERT_EQ((int)bbr.state, (int)NXP_BBR_STARTUP);
    NXP_ASSERT(bbr.cwnd > 0);
    NXP_ASSERT(!bbr.filled_pipe);
}

/* ── Test: BBR Via CC Interface ────────────────────────── */

NXP_TEST(bbr_cc_interface) {
    void *state = nxp_cc_bbr.create();
    NXP_ASSERT_NOT_NULL(state);

    /* Initial cwnd should be 10 packets */
    uint64_t cwnd = nxp_cc_bbr.get_cwnd(state);
    NXP_ASSERT_EQ(cwnd, (uint64_t)(10 * 1200));

    /* Should be in slow start initially */
    NXP_ASSERT(nxp_cc_bbr.in_slow_start(state));

    /* Initial pacing rate = 0 (no BW sample yet) */
    uint64_t rate = nxp_cc_bbr.get_pacing_rate(state);
    NXP_ASSERT_EQ(rate, (uint64_t)0);

    nxp_cc_bbr.destroy(state);
}

/* ── Test: BBR Processes ACKs ──────────────────────────── */

NXP_TEST(bbr_processes_acks) {
    void *state = nxp_cc_bbr.create();
    NXP_ASSERT_NOT_NULL(state);

    /* Simulate sending */
    nxp_cc_bbr.on_sent(state, 1200, 0, 1200);

    /* Simulate an ACK with delivery rate of 10 MB/s */
    nxp_cc_ack_info ack = {
        .pkt_num = 0,
        .acked_bytes = 1200,
        .sent_time = 0,
        .now_us = 10000,
        .rtt_us = 10000,       /* 10ms RTT */
        .min_rtt_us = 10000,
        .bytes_in_flight = 0,
        .delivery_rate_bps = 10000000, /* 10 MB/s */
        .round_count = 1,
        .is_app_limited = false,
        .round_start = true,
    };
    nxp_cc_bbr.on_ack(state, &ack);

    /* After an ACK with BW, pacing rate should be set */
    uint64_t rate = nxp_cc_bbr.get_pacing_rate(state);
    NXP_ASSERT(rate > 0);

    /* cwnd should be updated */
    uint64_t cwnd = nxp_cc_bbr.get_cwnd(state);
    NXP_ASSERT(cwnd >= (uint64_t)(4 * 1200)); /* At least min cwnd */

    nxp_cc_bbr.destroy(state);
}

/* ── Test: BBR Startup to Drain ────────────────────────── */

NXP_TEST(bbr_startup_to_drain) {
    nxp_bbr bbr;
    nxp_bbr_init_state(&bbr);

    NXP_ASSERT_EQ((int)bbr.state, (int)NXP_BBR_STARTUP);

    /* Feed BBR with steady BW that doesn't grow (simulates plateau) */
    for (uint64_t round = 1; round <= 10; round++) {
        nxp_cc_ack_info ack = {
            .now_us = round * 10000,
            .rtt_us = 10000,
            .min_rtt_us = 10000,
            .bytes_in_flight = 50000,
            .delivery_rate_bps = 1000000,  /* constant 1 MB/s */
            .round_count = round,
            .is_app_limited = false,
            .round_start = true,
        };

        nxp_bbr_update_model(&bbr, &ack);
        nxp_bbr_check_startup_done(&bbr);
        nxp_bbr_check_drain_done(&bbr);
        nxp_bbr_update_control(&bbr);
    }

    /* After several rounds of no BW growth, should exit startup */
    NXP_ASSERT(bbr.filled_pipe);
    NXP_ASSERT((int)bbr.state == (int)NXP_BBR_DRAIN ||
               (int)bbr.state == (int)NXP_BBR_PROBE_BW);
}

/* ── Test: BBR Drain to ProbeBW ────────────────────────── */

NXP_TEST(bbr_drain_to_probe_bw) {
    nxp_bbr bbr;
    nxp_bbr_init_state(&bbr);

    /* Force into drain state */
    bbr.filled_pipe = true;
    bbr.state = NXP_BBR_DRAIN;
    bbr.pacing_gain_num = NXP_BBR_DRAIN_PACING_GAIN_NUM;
    bbr.pacing_gain_den = NXP_BBR_DRAIN_PACING_GAIN_DEN;

    /* Set some BW/RTT so BDP is computable */
    bbr.btl_bw = 1000000;     /* 1 MB/s */
    bbr.rt_prop_us = 10000;   /* 10ms */

    /* bytes_in_flight > BDP: still draining */
    bbr.bytes_in_flight = 100000;
    nxp_bbr_check_drain_done(&bbr);
    NXP_ASSERT_EQ((int)bbr.state, (int)NXP_BBR_DRAIN);

    /* bytes_in_flight <= BDP: transition to ProbeBW */
    /* BDP = 1000000 * 10000 / 1000000 = 10000 bytes */
    bbr.bytes_in_flight = 10000;
    nxp_bbr_check_drain_done(&bbr);
    NXP_ASSERT_EQ((int)bbr.state, (int)NXP_BBR_PROBE_BW);
}

/* ── Test: BBR ProbeBW Gain Cycling ────────────────────── */

NXP_TEST(bbr_probe_bw_cycling) {
    nxp_bbr bbr;
    nxp_bbr_init_state(&bbr);

    /* Force into ProbeBW */
    bbr.filled_pipe = true;
    bbr.state = NXP_BBR_PROBE_BW;
    bbr.btl_bw = 1000000;
    bbr.rt_prop_us = 10000;
    bbr.cycle_index = 0;
    bbr.cycle_stamp = 0;
    bbr.pacing_gain_num = 125;
    bbr.pacing_gain_den = 100;

    /* Advance through one cycle (each phase ~ 1 RTT = 10ms) */
    uint8_t prev_index = bbr.cycle_index;
    nxp_bbr_update_probe_bw(&bbr, 15000); /* > 10ms after stamp=0 */
    NXP_ASSERT(bbr.cycle_index != prev_index || bbr.cycle_index == 1);

    /* Cycle should eventually wrap around */
    for (int i = 0; i < 8; i++) {
        nxp_bbr_update_probe_bw(&bbr, bbr.cycle_stamp + 15000);
    }
    /* After 8 advances, we should have wrapped */
    NXP_ASSERT(bbr.cycle_index < 8);
}

/* ── Test: BBR Control Update ──────────────────────────── */

NXP_TEST(bbr_control_update) {
    nxp_bbr bbr;
    nxp_bbr_init_state(&bbr);

    bbr.btl_bw = 10000000;   /* 10 MB/s */
    bbr.rt_prop_us = 20000;  /* 20ms */

    nxp_bbr_update_control(&bbr);

    /* Pacing rate = btl_bw * startup_gain = 10M * 2.77 = ~27.7 MB/s */
    NXP_ASSERT(bbr.pacing_rate > 0);
    NXP_ASSERT(bbr.pacing_rate > bbr.btl_bw); /* startup gain > 1.0 */

    /* cwnd should be at least min cwnd */
    NXP_ASSERT(bbr.cwnd >= NXP_BBR_MIN_CWND);

    /* BDP = 10M * 20000 / 1e6 = 200000 bytes */
    /* Target cwnd = BDP * 2 (startup cwnd_gain) = 400000 */
    NXP_ASSERT(bbr.target_cwnd >= 200000);
}

/* ── Test: BBR Loss in Startup ─────────────────────────── */

NXP_TEST(bbr_loss_in_startup) {
    void *state = nxp_cc_bbr.create();
    NXP_ASSERT_NOT_NULL(state);
    nxp_bbr *bbr = (nxp_bbr *)state;

    NXP_ASSERT_EQ((int)bbr->state, (int)NXP_BBR_STARTUP);

    /* Simulate heavy loss: > 20% of bytes_in_flight */
    bbr->bytes_in_flight = 100000;
    nxp_cc_loss_info loss = {
        .lost_bytes = 25000,  /* 25% loss */
        .now_us = 10000,
        .bytes_in_flight = 75000,
    };
    nxp_cc_bbr.on_loss(state, &loss);

    /* Heavy loss should exit startup */
    NXP_ASSERT(bbr->filled_pipe);
    NXP_ASSERT_EQ((int)bbr->state, (int)NXP_BBR_DRAIN);

    nxp_cc_bbr.destroy(state);
}

/* ── main ─────────────────────────────────────────────── */

int main(void) {
    printf("=== NXP BBR Congestion Control Tests ===\n\n");

    NXP_RUN_TEST(windowed_min_filter);
    NXP_RUN_TEST(windowed_max_filter);
    NXP_RUN_TEST(delivery_rate_basic);
    NXP_RUN_TEST(delivery_rate_rounds);
    NXP_RUN_TEST(delivery_rate_app_limited);
    NXP_RUN_TEST(pacer_init);
    NXP_RUN_TEST(pacer_rate_limiting);
    NXP_RUN_TEST(pacer_next_send_time);
    NXP_RUN_TEST(bbr_init);
    NXP_RUN_TEST(bbr_cc_interface);
    NXP_RUN_TEST(bbr_processes_acks);
    NXP_RUN_TEST(bbr_startup_to_drain);
    NXP_RUN_TEST(bbr_drain_to_probe_bw);
    NXP_RUN_TEST(bbr_probe_bw_cycling);
    NXP_RUN_TEST(bbr_control_update);
    NXP_RUN_TEST(bbr_loss_in_startup);

    NXP_TEST_SUMMARY();
}
