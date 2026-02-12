/*
 * Unit tests: ACK tracker + Loss detection
 *
 * Phase 4: Tests received PN tracking, ACK frame generation,
 * RTT estimation, sent packet tracking, and loss detection.
 */
#include "test_framework.h"
#include "connection_internal.h"
#include <string.h>

/* ── Test: Init and Cleanup ───────────────────────────── */

NXP_TEST(ack_init_cleanup) {
    nxp_ack_state ack;
    nxp_ack_init(&ack);

    NXP_ASSERT_NOT_NULL(ack.sent);
    NXP_ASSERT_EQ(ack.sent_count, (uint32_t)0);
    NXP_ASSERT_EQ(ack.recv_range_count, (uint32_t)0);
    NXP_ASSERT(!ack.has_rtt);
    NXP_ASSERT_EQ(ack.loss_time, UINT64_MAX);

    nxp_ack_cleanup(&ack);
    NXP_ASSERT_NULL(ack.sent);
}

/* ── Test: Receive Tracking (sequential) ──────────────── */

NXP_TEST(ack_recv_sequential) {
    nxp_ack_state ack;
    nxp_ack_init(&ack);

    /* Receive packets 0, 1, 2, 3 in order */
    for (uint64_t i = 0; i < 4; i++) {
        nxp_ack_on_pkt_recv(&ack, i, 1000 + i * 100, true);
    }

    /* Should have a single contiguous range [0, 3] */
    NXP_ASSERT_EQ(ack.recv_range_count, (uint32_t)1);
    NXP_ASSERT_EQ(ack.recv_ranges[0].start, (uint64_t)0);
    NXP_ASSERT_EQ(ack.recv_ranges[0].end, (uint64_t)3);
    NXP_ASSERT_EQ(ack.largest_recv_pn, (uint64_t)3);
    NXP_ASSERT(ack.ack_needed);

    nxp_ack_cleanup(&ack);
}

/* ── Test: Receive Tracking (with gap) ────────────────── */

NXP_TEST(ack_recv_with_gap) {
    nxp_ack_state ack;
    nxp_ack_init(&ack);

    /* Receive 0, 1, 2, then 5, 6 (gap at 3, 4) */
    nxp_ack_on_pkt_recv(&ack, 0, 1000, true);
    nxp_ack_on_pkt_recv(&ack, 1, 1100, true);
    nxp_ack_on_pkt_recv(&ack, 2, 1200, true);
    nxp_ack_on_pkt_recv(&ack, 5, 1500, true);
    nxp_ack_on_pkt_recv(&ack, 6, 1600, true);

    /* Should have 2 ranges: [5,6] and [0,2] (descending by start) */
    NXP_ASSERT_EQ(ack.recv_range_count, (uint32_t)2);
    NXP_ASSERT_EQ(ack.recv_ranges[0].start, (uint64_t)5);
    NXP_ASSERT_EQ(ack.recv_ranges[0].end, (uint64_t)6);
    NXP_ASSERT_EQ(ack.recv_ranges[1].start, (uint64_t)0);
    NXP_ASSERT_EQ(ack.recv_ranges[1].end, (uint64_t)2);

    nxp_ack_cleanup(&ack);
}

/* ── Test: Receive with gap fill (merge) ──────────────── */

NXP_TEST(ack_recv_gap_fill) {
    nxp_ack_state ack;
    nxp_ack_init(&ack);

    /* Receive 0, 1, 3, 4 (gap at 2) */
    nxp_ack_on_pkt_recv(&ack, 0, 1000, true);
    nxp_ack_on_pkt_recv(&ack, 1, 1100, true);
    nxp_ack_on_pkt_recv(&ack, 3, 1300, true);
    nxp_ack_on_pkt_recv(&ack, 4, 1400, true);

    NXP_ASSERT_EQ(ack.recv_range_count, (uint32_t)2);

    /* Now fill the gap with pkt 2 */
    nxp_ack_on_pkt_recv(&ack, 2, 1500, true);

    /* Should merge into single range [0, 4] */
    NXP_ASSERT_EQ(ack.recv_range_count, (uint32_t)1);
    NXP_ASSERT_EQ(ack.recv_ranges[0].start, (uint64_t)0);
    NXP_ASSERT_EQ(ack.recv_ranges[0].end, (uint64_t)4);

    nxp_ack_cleanup(&ack);
}

/* ── Test: Build ACK Frame ────────────────────────────── */

NXP_TEST(ack_build_frame) {
    nxp_ack_state ack;
    nxp_ack_init(&ack);

    nxp_ack_on_pkt_recv(&ack, 0, 1000, true);
    nxp_ack_on_pkt_recv(&ack, 1, 1100, true);
    nxp_ack_on_pkt_recv(&ack, 2, 1200, true);
    nxp_ack_on_pkt_recv(&ack, 5, 1500, true);
    nxp_ack_on_pkt_recv(&ack, 6, 1600, true);

    nxp_frame_ack frame;
    bool ok = nxp_ack_build_frame(&ack, &frame, 2000);
    NXP_ASSERT(ok);
    NXP_ASSERT_EQ(frame.largest_acked, (uint64_t)6);
    NXP_ASSERT_EQ(frame.first_ack_range, (uint64_t)1);  /* [5,6] range is 1 */
    NXP_ASSERT_EQ(frame.range_count, (uint32_t)1);
    /* gap = 5 - 2 - 2 = 1, ack_range = 2 - 0 = 2 */
    NXP_ASSERT_EQ(frame.ranges[0].gap, (uint64_t)1);
    NXP_ASSERT_EQ(frame.ranges[0].ack_range, (uint64_t)2);

    nxp_ack_cleanup(&ack);
}

/* ── Test: Sent Packet Tracking ───────────────────────── */

NXP_TEST(ack_sent_tracking) {
    nxp_ack_state ack;
    nxp_ack_init(&ack);

    nxp_sent_pkt pkt = {
        .pkt_num       = 0,
        .sent_time     = 1000,
        .sent_bytes    = 100,
        .ack_eliciting = true,
        .in_flight     = true,
        .declared_lost = false,
        .frame_count   = 0,
    };

    nxp_ack_on_pkt_sent(&ack, &pkt);
    NXP_ASSERT_EQ(ack.sent_count, (uint32_t)1);
    NXP_ASSERT_EQ(ack.bytes_in_flight, (uint64_t)100);

    pkt.pkt_num = 1;
    pkt.sent_time = 1100;
    nxp_ack_on_pkt_sent(&ack, &pkt);
    NXP_ASSERT_EQ(ack.sent_count, (uint32_t)2);
    NXP_ASSERT_EQ(ack.bytes_in_flight, (uint64_t)200);

    nxp_ack_cleanup(&ack);
}

/* ── Test: ACK recv updates RTT ───────────────────────── */

static uint32_t g_acked_count;
static uint32_t g_lost_count;

static void test_on_ack(void *ctx, const nxp_sent_pkt *pkt) {
    (void)ctx; (void)pkt;
    g_acked_count++;
}
static void test_on_loss(void *ctx, const nxp_sent_pkt *pkt) {
    (void)ctx; (void)pkt;
    g_lost_count++;
}

NXP_TEST(ack_recv_updates_rtt) {
    nxp_ack_state ack;
    nxp_ack_init(&ack);

    /* Send packet 0 at time 1000 */
    nxp_sent_pkt pkt = {
        .pkt_num = 0, .sent_time = 1000, .sent_bytes = 50,
        .ack_eliciting = true, .in_flight = true,
    };
    nxp_ack_on_pkt_sent(&ack, &pkt);

    /* Receive ACK for packet 0 at time 11000 (10ms RTT) */
    nxp_frame_ack frame = {
        .largest_acked = 0,
        .ack_delay = 0,
        .first_ack_range = 0,
        .range_count = 0,
    };

    g_acked_count = 0;
    g_lost_count = 0;
    nxp_ack_on_ack_recv(&ack, &frame, 11000, test_on_ack, test_on_loss, nullptr);

    NXP_ASSERT_EQ(g_acked_count, (uint32_t)1);
    NXP_ASSERT(ack.has_rtt);
    NXP_ASSERT_EQ(ack.smoothed_rtt, (uint64_t)10000);  /* First sample */
    NXP_ASSERT_EQ(ack.latest_rtt, (uint64_t)10000);
    NXP_ASSERT_EQ(ack.sent_count, (uint32_t)0);  /* Acked packet removed */
    NXP_ASSERT_EQ(ack.bytes_in_flight, (uint64_t)0);

    nxp_ack_cleanup(&ack);
}

/* ── Test: Loss detection by reorder threshold ────────── */

NXP_TEST(ack_loss_by_reorder) {
    nxp_ack_state ack;
    nxp_ack_init(&ack);

    /* Send packets 0, 1, 2, 3, 4 */
    for (uint64_t i = 0; i < 5; i++) {
        nxp_sent_pkt pkt = {
            .pkt_num = i, .sent_time = 1000 + i * 100, .sent_bytes = 50,
            .ack_eliciting = true, .in_flight = true,
        };
        nxp_ack_on_pkt_sent(&ack, &pkt);
    }
    NXP_ASSERT_EQ(ack.bytes_in_flight, (uint64_t)250);

    /* Receive ACK for packet 4 only (largest=4, first_ack_range=0) */
    nxp_frame_ack frame = {
        .largest_acked = 4,
        .ack_delay = 0,
        .first_ack_range = 0,
        .range_count = 0,
    };

    g_acked_count = 0;
    g_lost_count = 0;
    nxp_ack_on_ack_recv(&ack, &frame, 2000, test_on_ack, test_on_loss, nullptr);

    /* Packet 4 acked. Packets 0 and 1 should be declared lost
     * (4 - 0 = 4 >= 3, 4 - 1 = 3 >= 3) */
    NXP_ASSERT_EQ(g_acked_count, (uint32_t)1);
    NXP_ASSERT_EQ(g_lost_count, (uint32_t)2);
    /* Remaining: packets 2 and 3 (not lost yet, within threshold) */
    NXP_ASSERT_EQ(ack.sent_count, (uint32_t)2);

    nxp_ack_cleanup(&ack);
}

/* ── Test: Loss timeout computation ───────────────────── */

NXP_TEST(ack_loss_timeout) {
    nxp_ack_state ack;
    nxp_ack_init(&ack);

    /* No packets in flight -> no timeout */
    NXP_ASSERT_EQ(nxp_ack_loss_timeout(&ack), UINT64_MAX);

    /* Send a packet */
    nxp_sent_pkt pkt = {
        .pkt_num = 0, .sent_time = 1000, .sent_bytes = 50,
        .ack_eliciting = true, .in_flight = true,
    };
    nxp_ack_on_pkt_sent(&ack, &pkt);

    /* Should have a PTO timeout */
    uint64_t timeout = nxp_ack_loss_timeout(&ack);
    NXP_ASSERT(timeout != UINT64_MAX);
    NXP_ASSERT(timeout > 1000);  /* Must be after the sent time */

    nxp_ack_cleanup(&ack);
}

/* ── Test: Build frame with no ranges ─────────────────── */

NXP_TEST(ack_build_empty) {
    nxp_ack_state ack;
    nxp_ack_init(&ack);

    nxp_frame_ack frame;
    bool ok = nxp_ack_build_frame(&ack, &frame, 1000);
    NXP_ASSERT(!ok);

    nxp_ack_cleanup(&ack);
}

/* ── Main ──────────────────────────────────────────────── */

int main(void) {
    printf("=== NXP ACK + Loss Detection Tests (Phase 4) ===\n");

    NXP_RUN_TEST(ack_init_cleanup);
    NXP_RUN_TEST(ack_recv_sequential);
    NXP_RUN_TEST(ack_recv_with_gap);
    NXP_RUN_TEST(ack_recv_gap_fill);
    NXP_RUN_TEST(ack_build_frame);
    NXP_RUN_TEST(ack_sent_tracking);
    NXP_RUN_TEST(ack_recv_updates_rtt);
    NXP_RUN_TEST(ack_loss_by_reorder);
    NXP_RUN_TEST(ack_loss_timeout);
    NXP_RUN_TEST(ack_build_empty);

    NXP_TEST_SUMMARY();
}
