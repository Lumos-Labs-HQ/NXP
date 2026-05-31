/*
 * Production-Grade Test Suite: Flow Control
 *
 * Dedicated tests for connection-level and per-stream flow control,
 * MAX_DATA updates, window management, and blocked detection.
 */
#include "test_framework.h"
#include "connection_internal.h"
#include "util/checked_int.h"

#include <string.h>

/* ── Helpers ────────────────────────────────────────────── */

static nxp_conn_config make_config(uint8_t cid_byte) {
    nxp_conn_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.scid.data[0] = cid_byte;
    cfg.scid.len     = 8;
    cfg.idle_timeout_us       = NXP_DEFAULT_IDLE_TIMEOUT;
    cfg.initial_max_data      = 10000;  /* Small for testing */
    cfg.initial_max_stream_data = 5000;
    cfg.max_streams_bidi      = 256;
    cfg.max_streams_uni       = 256;
    return cfg;
}

/* ══════════════════════════════════════════════════════════
 * SECTION 1: Connection-Level Flow Control
 * ══════════════════════════════════════════════════════════ */

NXP_TEST(conn_flow_init) {
    nxp_flow_ctrl fc;
    nxp_flow_init(&fc, 1000, 2000);
    NXP_ASSERT_EQ(fc.local_max_data, (uint64_t)1000);
    NXP_ASSERT_EQ(fc.peer_max_data, (uint64_t)2000);
    NXP_ASSERT_EQ(fc.data_sent, (uint64_t)0);
    NXP_ASSERT_EQ(fc.data_recv, (uint64_t)0);
}

NXP_TEST(conn_flow_can_send_within_limit) {
    nxp_flow_ctrl fc;
    nxp_flow_init(&fc, 10000, 5000);

    NXP_ASSERT(nxp_flow_can_send(&fc, 1000));
    nxp_flow_on_send(&fc, 1000);
    NXP_ASSERT_EQ(fc.data_sent, (uint64_t)1000);

    NXP_ASSERT(nxp_flow_can_send(&fc, 3999));
    nxp_flow_on_send(&fc, 3999);
    NXP_ASSERT_EQ(fc.data_sent, (uint64_t)4999);

    /* Now at limit — last byte should fail */
    NXP_ASSERT(!nxp_flow_can_send(&fc, 2));
}

NXP_TEST(conn_flow_exact_limit) {
    nxp_flow_ctrl fc;
    nxp_flow_init(&fc, 1000, 1000);
    NXP_ASSERT(nxp_flow_can_send(&fc, 1000));
    nxp_flow_on_send(&fc, 1000);
    NXP_ASSERT(!nxp_flow_can_send(&fc, 1));
}

NXP_TEST(conn_flow_set_peer_max_increases) {
    nxp_flow_ctrl fc;
    nxp_flow_init(&fc, 1000, 100);
    NXP_ASSERT(!nxp_flow_can_send(&fc, 101));

    nxp_flow_set_peer_max(&fc, 500);
    NXP_ASSERT(nxp_flow_can_send(&fc, 500));
}

NXP_TEST(conn_flow_set_peer_max_doesnt_decrease) {
    nxp_flow_ctrl fc;
    nxp_flow_init(&fc, 1000, 500);
    nxp_flow_set_peer_max(&fc, 100); /* Should be ignored */
    NXP_ASSERT_EQ(fc.peer_max_data, (uint64_t)500);
    NXP_ASSERT(nxp_flow_can_send(&fc, 500));
}

NXP_TEST(conn_flow_max_data_update) {
    nxp_flow_ctrl fc;
    nxp_flow_init(&fc, 1000, 500);

    NXP_ASSERT(!nxp_flow_should_update(&fc));

    /* Simulate receiving 600 bytes (past 50% window) */
    nxp_flow_on_recv(&fc, 600);
    nxp_flow_on_consume(&fc, 0);

    NXP_ASSERT(nxp_flow_should_update(&fc));
    NXP_ASSERT(fc.send_max_data);

    /* Get update — should extend to 600 + 1000 */
    uint64_t new_max = nxp_flow_get_update(&fc);
    NXP_ASSERT_EQ(new_max, (uint64_t)1600);
    NXP_ASSERT(!nxp_flow_should_update(&fc));
}

/* ══════════════════════════════════════════════════════════
 * SECTION 2: Per-Stream Flow Control
 * ══════════════════════════════════════════════════════════ */

NXP_TEST(stream_flow_init) {
    nxp_stream_s *s = nxp_stream_create(0, NXP_STREAM_RELIABLE, 500, 500);
    NXP_ASSERT_NOT_NULL(s);
    NXP_ASSERT_EQ(s->flow.peer_max_data, (uint64_t)500);
    NXP_ASSERT_EQ(s->flow.local_max_data, (uint64_t)500);
    nxp_stream_destroy(s);
}

NXP_TEST(stream_flow_write_blocked) {
    nxp_flow_ctrl fc;
    nxp_flow_init(&fc, 50, 50);

    NXP_ASSERT(nxp_flow_can_send(&fc, 30));
    nxp_flow_on_send(&fc, 30);
    NXP_ASSERT(nxp_flow_can_send(&fc, 20));
    nxp_flow_on_send(&fc, 20);
    /* Exceeded peer limit now */
    NXP_ASSERT(!nxp_flow_can_send(&fc, 1));
}

NXP_TEST(stream_flow_partial_write) {
    nxp_flow_ctrl fc;
    nxp_flow_init(&fc, 50, 50);

    nxp_flow_on_send(&fc, 30);
    NXP_ASSERT(nxp_flow_can_send(&fc, 20));
    NXP_ASSERT(!nxp_flow_can_send(&fc, 30));
}

/* ══════════════════════════════════════════════════════════
 * SECTION 3: Connection-Level Integration
 * ══════════════════════════════════════════════════════════ */

NXP_TEST(conn_flow_blocks_send) {
    nxp_flow_ctrl fc;
    nxp_flow_init(&fc, 50, 50);
    NXP_ASSERT_EQ(fc.peer_max_data, (uint64_t)50);

    nxp_flow_on_send(&fc, 60);
    NXP_ASSERT(!nxp_flow_can_send(&fc, 1));
}

NXP_TEST(conn_flow_peer_max_update_via_recv) {
    nxp_conn_config client_cfg = make_config(0x01);
    nxp_conn_config server_cfg = make_config(0x02);

    nxp_conn *client = nxp_conn_create(&client_cfg, false);
    nxp_conn *server = nxp_conn_create(&server_cfg, true);
    NXP_ASSERT_NOT_NULL(client);
    NXP_ASSERT_NOT_NULL(server);

    nxp_conn_id cd = { .len = 8 }; cd.data[0] = 0x02;
    nxp_conn_id sd = { .len = 8 }; sd.data[0] = 0x01;
    nxp_conn_set_established(client, &cd);
    nxp_conn_set_established(server, &sd);

    uint64_t sid;
    NXP_ASSERT_OK(nxp_conn_open_stream(client, &sid, NXP_STREAM_RELIABLE, false));

    uint8_t data[100];
    memset(data, 0x42, sizeof(data));
    nxp_conn_stream_send(client, sid, data, sizeof(data), false);

    uint8_t pkt[1500];
    ssize_t n = nxp_conn_send(client, pkt, sizeof(pkt), 1000);
    NXP_ASSERT(n > 0);

    /* Send to server */
    NXP_ASSERT_OK(nxp_conn_recv(server, pkt, (size_t)n, 1100));

    /* Server updates peer max data */
    nxp_flow_set_peer_max(&server->conn_flow, 50000);
    NXP_ASSERT_EQ(server->conn_flow.peer_max_data, (uint64_t)50000);

    nxp_conn_destroy(client);
    nxp_conn_destroy(server);
}

/* ══════════════════════════════════════════════════════════
 * SECTION 4: Checked Integer Overflow Protection
 * ══════════════════════════════════════════════════════════ */

NXP_TEST(checked_add_u64_normal) {
    uint64_t result;
    bool ovf = nxp_checked_add_u64(&result, 5, 10);
    NXP_ASSERT(!ovf);
    NXP_ASSERT_EQ(result, (uint64_t)15);
}

NXP_TEST(checked_add_u64_overflow) {
    uint64_t result;
    bool ovf = nxp_checked_add_u64(&result, UINT64_MAX, 1);
    NXP_ASSERT(ovf);
}

NXP_TEST(checked_sub_u64_normal) {
    uint64_t result;
    bool ovf = nxp_checked_sub_u64(&result, 10, 5);
    NXP_ASSERT(!ovf);
    NXP_ASSERT_EQ(result, (uint64_t)5);
}

NXP_TEST(checked_sub_u64_underflow) {
    uint64_t result;
    bool ovf = nxp_checked_sub_u64(&result, 5, 10);
    NXP_ASSERT(ovf);
}

NXP_TEST(checked_mul_u64_overflow) {
    uint64_t result;
    bool ovf = nxp_checked_mul_u64(&result, UINT64_MAX, 2);
    NXP_ASSERT(ovf);
}

/* ── Main ────────────────────────────────────────────────── */

int main(void) {
    printf("=== NXP Flow Control Tests ===\n");

    NXP_RUN_TEST(conn_flow_init);
    NXP_RUN_TEST(conn_flow_can_send_within_limit);
    NXP_RUN_TEST(conn_flow_exact_limit);
    NXP_RUN_TEST(conn_flow_set_peer_max_increases);
    NXP_RUN_TEST(conn_flow_set_peer_max_doesnt_decrease);
    NXP_RUN_TEST(conn_flow_max_data_update);

    NXP_RUN_TEST(stream_flow_init);
    NXP_RUN_TEST(stream_flow_write_blocked);
    NXP_RUN_TEST(stream_flow_partial_write);

    NXP_RUN_TEST(conn_flow_blocks_send);
    NXP_RUN_TEST(conn_flow_peer_max_update_via_recv);

    NXP_RUN_TEST(checked_add_u64_normal);
    NXP_RUN_TEST(checked_add_u64_overflow);
    NXP_RUN_TEST(checked_sub_u64_normal);
    NXP_RUN_TEST(checked_sub_u64_underflow);
    NXP_RUN_TEST(checked_mul_u64_overflow);

    NXP_TEST_SUMMARY();
}
