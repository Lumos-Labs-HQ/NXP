/*
 * NXP Built-in Features Tests
 *
 * Phase 8: Tests for heartbeat, per-stream rate limiting,
 * backpressure handling, and auto-reconnect scaffolding.
 */
#include "test_framework.h"
#include "core/heartbeat_internal.h"
#include "core/stream_rate.h"
#include "core/connection_internal.h"
#include <string.h>

/* ── Test: Heartbeat Init ──────────────────────────────── */

NXP_TEST(heartbeat_init) {
    nxp_heartbeat hb;
    nxp_heartbeat_init(&hb, 5000000); /* 5s interval */

    NXP_ASSERT(hb.enabled);
    NXP_ASSERT_EQ(hb.interval_us, (uint64_t)5000000);
    NXP_ASSERT_EQ(hb.consecutive_misses, (uint32_t)0);
    NXP_ASSERT(!nxp_heartbeat_is_dead(&hb));
}

/* ── Test: Heartbeat Disabled ──────────────────────────── */

NXP_TEST(heartbeat_disabled) {
    nxp_heartbeat hb;
    nxp_heartbeat_init(&hb, 0);

    NXP_ASSERT(!hb.enabled);
    NXP_ASSERT(!nxp_heartbeat_is_dead(&hb));
    NXP_ASSERT_EQ(nxp_heartbeat_next_timeout(&hb), UINT64_MAX);
}

/* ── Test: Heartbeat Sends Probe ───────────────────────── */

NXP_TEST(heartbeat_sends_probe) {
    nxp_heartbeat hb;
    nxp_heartbeat_init(&hb, 1000000); /* 1s interval */

    /* At t=0, no heartbeat needed yet */
    nxp_heartbeat_check(&hb, 0);
    /* First check: time to send (last_sent=0, interval passed) */
    NXP_ASSERT(hb.send_heartbeat);
    hb.send_heartbeat = false; /* Clear after "sending" */

    /* At t=500000 (0.5s), not yet time */
    nxp_heartbeat_check(&hb, 500000);
    NXP_ASSERT(!hb.send_heartbeat);

    /* At t=1000001 (>1s), time for next heartbeat */
    nxp_heartbeat_check(&hb, 1000001);
    NXP_ASSERT(hb.send_heartbeat);
}

/* ── Test: Heartbeat Echo ──────────────────────────────── */

NXP_TEST(heartbeat_echo) {
    nxp_heartbeat hb;
    nxp_heartbeat_init(&hb, 1000000);

    /* Receive a heartbeat from peer */
    nxp_heartbeat_on_recv(&hb, 42, 5000);

    NXP_ASSERT(hb.send_echo);
    NXP_ASSERT_EQ(hb.echo_timestamp, (uint64_t)42);
    NXP_ASSERT_EQ(hb.consecutive_misses, (uint32_t)0);
}

/* ── Test: Heartbeat Dead Detection ────────────────────── */

NXP_TEST(heartbeat_dead_detection) {
    nxp_heartbeat hb;
    nxp_heartbeat_init(&hb, 1000000); /* 1s */

    /* Send probe, no response - 3 consecutive misses = dead */
    for (uint32_t i = 0; i < NXP_HEARTBEAT_MISS_THRESHOLD + 1; i++) {
        uint64_t t = (uint64_t)i * 1100000;
        nxp_heartbeat_check(&hb, t);
        hb.send_heartbeat = false;
    }

    NXP_ASSERT(nxp_heartbeat_is_dead(&hb));
}

/* ── Test: Heartbeat Response Resets Misses ─────────────── */

NXP_TEST(heartbeat_response_resets) {
    nxp_heartbeat hb;
    nxp_heartbeat_init(&hb, 1000000);

    /* Send 2 probes without response */
    nxp_heartbeat_check(&hb, 0);
    hb.send_heartbeat = false;
    nxp_heartbeat_check(&hb, 1100000);
    hb.send_heartbeat = false;

    NXP_ASSERT_EQ(hb.consecutive_misses, (uint32_t)1);

    /* Now receive a response - resets counter */
    nxp_heartbeat_on_recv(&hb, 0, 1500000);
    NXP_ASSERT_EQ(hb.consecutive_misses, (uint32_t)0);
    NXP_ASSERT(!nxp_heartbeat_is_dead(&hb));
}

/* ── Test: Stream Rate Init ────────────────────────────── */

NXP_TEST(stream_rate_init) {
    nxp_stream_rate r;
    nxp_stream_rate_init(&r);

    NXP_ASSERT(!r.enabled);
    NXP_ASSERT(nxp_stream_rate_can_send(&r, 9999)); /* Always ok when disabled */
}

/* ── Test: Stream Rate Limiting ────────────────────────── */

NXP_TEST(stream_rate_limiting) {
    nxp_stream_rate r;
    nxp_stream_rate_init(&r);

    /* 100 KB/s rate limit */
    nxp_stream_rate_set(&r, 100000);
    NXP_ASSERT(r.enabled);
    NXP_ASSERT(r.max_burst > 0);

    /* Initialize time */
    nxp_stream_rate_update(&r, 0);

    /* Can send within burst */
    NXP_ASSERT(nxp_stream_rate_can_send(&r, 1200));

    /* Drain tokens */
    uint64_t budget = nxp_stream_rate_budget(&r);
    while (budget >= 1200) {
        nxp_stream_rate_on_send(&r, 1200);
        budget = nxp_stream_rate_budget(&r);
    }

    /* Should be blocked now */
    NXP_ASSERT(!nxp_stream_rate_can_send(&r, 1200));

    /* After time passes, tokens refill */
    nxp_stream_rate_update(&r, 100000); /* 100ms at 100KB/s = 10KB */
    NXP_ASSERT(nxp_stream_rate_can_send(&r, 1200));
}

/* ── Test: Stream Rate Budget ──────────────────────────── */

NXP_TEST(stream_rate_budget) {
    nxp_stream_rate r;
    nxp_stream_rate_init(&r);

    /* Unlimited when disabled */
    NXP_ASSERT_EQ(nxp_stream_rate_budget(&r), UINT64_MAX);

    /* Set to 1 MB/s */
    nxp_stream_rate_set(&r, 1000000);
    nxp_stream_rate_update(&r, 0);

    /* Budget should equal max_burst initially */
    uint64_t b = nxp_stream_rate_budget(&r);
    NXP_ASSERT(b > 0);
    NXP_ASSERT(b <= r.max_burst);
}

/* ── Test: Connection Heartbeat Integration ────────────── */

NXP_TEST(connection_heartbeat) {
    nxp_conn_config config;
    memset(&config, 0, sizeof(config));
    config.scid.len = 8;
    config.max_streams_bidi = 10;

    nxp_conn *conn = nxp_conn_create(&config, false);
    NXP_ASSERT_NOT_NULL(conn);

    /* Enable heartbeat */
    nxp_conn_set_heartbeat(conn, 1000000); /* 1s */
    NXP_ASSERT(conn->heartbeat.enabled);
    NXP_ASSERT_EQ(conn->heartbeat.interval_us, (uint64_t)1000000);

    /* Heartbeat timer should be in the timeout */
    nxp_conn_set_established(conn, &config.scid);
    uint64_t t = nxp_conn_timeout(conn, 0);
    NXP_ASSERT(t < UINT64_MAX);

    nxp_conn_destroy(conn);
}

/* ── Test: Connection Stream Rate Limit ────────────────── */

NXP_TEST(connection_stream_rate) {
    nxp_conn_config config;
    memset(&config, 0, sizeof(config));
    config.scid.len = 8;
    config.max_streams_bidi = 10;
    config.initial_max_data = 1048576;
    config.initial_max_stream_data = 262144;

    nxp_conn *conn = nxp_conn_create(&config, false);
    NXP_ASSERT_NOT_NULL(conn);
    nxp_conn_set_established(conn, &config.scid);

    /* Open a stream */
    uint64_t stream_id;
    NXP_ASSERT_OK(nxp_conn_open_stream(conn, &stream_id,
                                         NXP_STREAM_RELIABLE, false));

    /* Set rate limit */
    NXP_ASSERT_OK(nxp_conn_set_stream_rate(conn, stream_id, 50000));

    /* Verify rate limit is set on the stream */
    nxp_stream_s *s = (nxp_stream_s *)nxp_hash_map_get(conn->streams, stream_id);
    NXP_ASSERT_NOT_NULL(s);
    NXP_ASSERT(s->rate_limit.enabled);
    NXP_ASSERT_EQ(s->rate_limit.rate_bps, (uint64_t)50000);

    nxp_conn_destroy(conn);
}

/* ── Test: Auto-Reconnect Config ───────────────────────── */

NXP_TEST(auto_reconnect_config) {
    nxp_conn_config config;
    memset(&config, 0, sizeof(config));
    config.scid.len = 8;

    nxp_conn *conn = nxp_conn_create(&config, false);
    NXP_ASSERT_NOT_NULL(conn);

    /* Disabled by default */
    NXP_ASSERT(!conn->auto_reconnect);

    /* Enable */
    nxp_conn_set_auto_reconnect(conn, true, 5);
    NXP_ASSERT(conn->auto_reconnect);
    NXP_ASSERT_EQ(conn->max_reconnect_attempts, (uint32_t)5);

    /* Disable */
    nxp_conn_set_auto_reconnect(conn, false, 0);
    NXP_ASSERT(!conn->auto_reconnect);

    nxp_conn_destroy(conn);
}

/* ── Test: Writable Callback ───────────────────────────── */

static int writable_called = 0;
static uint64_t writable_stream_id = 0;

static void on_writable(void *user_data, uint64_t stream_id) {
    (void)user_data;
    writable_called++;
    writable_stream_id = stream_id;
}

NXP_TEST(writable_callback) {
    nxp_conn_config config;
    memset(&config, 0, sizeof(config));
    config.scid.len = 8;
    config.max_streams_bidi = 10;
    config.initial_max_data = 1048576;
    config.initial_max_stream_data = 262144;

    nxp_conn *conn = nxp_conn_create(&config, false);
    NXP_ASSERT_NOT_NULL(conn);
    nxp_conn_set_established(conn, &config.scid);

    /* Open a stream */
    uint64_t stream_id;
    NXP_ASSERT_OK(nxp_conn_open_stream(conn, &stream_id,
                                         NXP_STREAM_RELIABLE, false));

    /* Register writable callback */
    writable_called = 0;
    NXP_ASSERT_OK(nxp_conn_set_on_writable(conn, stream_id, on_writable, nullptr));

    /* Mark stream as blocked */
    nxp_stream_s *s = (nxp_stream_s *)nxp_hash_map_get(conn->streams, stream_id);
    NXP_ASSERT_NOT_NULL(s);
    s->blocked = true;

    /* Simulate on_writable being called when stream becomes unblocked */
    if (s->blocked && s->on_writable != nullptr) {
        s->blocked = false;
        s->on_writable(s->writable_user_data, stream_id);
    }

    NXP_ASSERT_EQ(writable_called, 1);
    NXP_ASSERT_EQ(writable_stream_id, stream_id);

    nxp_conn_destroy(conn);
}

/* ── main ─────────────────────────────────────────────── */

int main(void) {
    printf("=== NXP Built-in Features Tests (Phase 8) ===\n\n");

    NXP_RUN_TEST(heartbeat_init);
    NXP_RUN_TEST(heartbeat_disabled);
    NXP_RUN_TEST(heartbeat_sends_probe);
    NXP_RUN_TEST(heartbeat_echo);
    NXP_RUN_TEST(heartbeat_dead_detection);
    NXP_RUN_TEST(heartbeat_response_resets);
    NXP_RUN_TEST(stream_rate_init);
    NXP_RUN_TEST(stream_rate_limiting);
    NXP_RUN_TEST(stream_rate_budget);
    NXP_RUN_TEST(connection_heartbeat);
    NXP_RUN_TEST(connection_stream_rate);
    NXP_RUN_TEST(auto_reconnect_config);
    NXP_RUN_TEST(writable_callback);

    NXP_TEST_SUMMARY();
}
