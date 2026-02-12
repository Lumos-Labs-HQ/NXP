/*
 * Unit tests: Connection engine (Sans-I/O)
 *
 * Phase 4: Tests connection create/destroy, stream open, data send/recv
 * round-trip via packet encode/decode, scheduler, and connection close.
 */
#include "test_framework.h"
#include "connection_internal.h"
#include "packet_internal.h"
#include "frame_internal.h"
#include <string.h>

/* ── Helper: create a test connection pair ────────────── */

static nxp_conn_config make_config(uint8_t cid_byte) {
    nxp_conn_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.scid.data[0] = cid_byte;
    cfg.scid.len     = 8;
    cfg.idle_timeout_us       = NXP_DEFAULT_IDLE_TIMEOUT;
    cfg.initial_max_data      = NXP_DEFAULT_MAX_DATA;
    cfg.initial_max_stream_data = NXP_DEFAULT_MAX_STREAM_DATA;
    cfg.max_streams_bidi      = 256;
    cfg.max_streams_uni       = 256;
    return cfg;
}

/* ── Test: Create and Destroy ─────────────────────────── */

NXP_TEST(conn_create_destroy) {
    nxp_conn_config cfg = make_config(0x01);
    nxp_conn *conn = nxp_conn_create(&cfg, false);
    NXP_ASSERT_NOT_NULL(conn);
    NXP_ASSERT_EQ(nxp_conn_get_state(conn), NXP_CONN_IDLE);
    NXP_ASSERT(!conn->is_server);

    nxp_conn_destroy(conn);
}

/* ── Test: Set Established ────────────────────────────── */

NXP_TEST(conn_set_established) {
    nxp_conn_config cfg = make_config(0x01);
    nxp_conn *conn = nxp_conn_create(&cfg, false);
    NXP_ASSERT_NOT_NULL(conn);

    nxp_conn_id dcid;
    memset(&dcid, 0, sizeof(dcid));
    dcid.data[0] = 0x02;
    dcid.len     = 8;

    nxp_conn_set_established(conn, &dcid);
    NXP_ASSERT_EQ(nxp_conn_get_state(conn), NXP_CONN_ESTABLISHED);
    NXP_ASSERT_EQ(conn->dcid.data[0], (uint8_t)0x02);

    nxp_conn_destroy(conn);
}

/* ── Test: Open Stream ────────────────────────────────── */

NXP_TEST(conn_open_stream) {
    nxp_conn_config cfg = make_config(0x01);
    nxp_conn *conn = nxp_conn_create(&cfg, false);
    nxp_conn_id dcid = { .len = 8 };
    nxp_conn_set_established(conn, &dcid);

    uint64_t sid;
    NXP_ASSERT_OK(nxp_conn_open_stream(conn, &sid, NXP_STREAM_RELIABLE, false));
    NXP_ASSERT_EQ(conn->open_bidi_count, (uint32_t)1);

    /* Stream should be in the hash map */
    nxp_stream_s *s = (nxp_stream_s *)nxp_hash_map_get(conn->streams, sid);
    NXP_ASSERT_NOT_NULL(s);
    NXP_ASSERT_EQ(s->state, NXP_STREAM_OPEN);

    nxp_conn_destroy(conn);
}

/* ── Test: Stream Send and Generate Packet ────────────── */

NXP_TEST(conn_stream_send_generates_packet) {
    nxp_conn_config cfg = make_config(0x01);
    nxp_conn *conn = nxp_conn_create(&cfg, false);
    nxp_conn_id dcid = { .len = 8 };
    nxp_conn_set_established(conn, &dcid);

    /* Open stream and write data */
    uint64_t sid;
    NXP_ASSERT_OK(nxp_conn_open_stream(conn, &sid, NXP_STREAM_RELIABLE, false));

    const uint8_t msg[] = "Hello Phase 4!";
    ssize_t written = nxp_conn_stream_send(conn, sid, msg, sizeof(msg) - 1, false);
    NXP_ASSERT_EQ((size_t)written, sizeof(msg) - 1);

    /* Generate outgoing packet */
    uint8_t pkt_buf[1500];
    uint64_t now = 1000;
    ssize_t pkt_len = nxp_conn_send(conn, pkt_buf, sizeof(pkt_buf), now);
    NXP_ASSERT(pkt_len > 0);

    /* Packet should contain at least header + stream frame */
    NXP_ASSERT(pkt_len > (ssize_t)(1 + dcid.len + 1));

    /* Stats should be updated */
    NXP_ASSERT(conn->stats.bytes_sent > 0);
    NXP_ASSERT_EQ(conn->next_pkt_num, (uint64_t)1);

    nxp_conn_destroy(conn);
}

/* ── Test: Full Round-Trip (client -> server -> client) ── */

NXP_TEST(conn_round_trip) {
    /* Create client and server connections */
    nxp_conn_config client_cfg = make_config(0x01);
    nxp_conn_config server_cfg = make_config(0x02);

    nxp_conn *client = nxp_conn_create(&client_cfg, false);
    nxp_conn *server = nxp_conn_create(&server_cfg, true);
    NXP_ASSERT_NOT_NULL(client);
    NXP_ASSERT_NOT_NULL(server);

    /* Establish connections (skip handshake for Phase 4) */
    nxp_conn_id client_dcid = { .len = 8 };
    client_dcid.data[0] = 0x02;
    nxp_conn_set_established(client, &client_dcid);

    nxp_conn_id server_dcid = { .len = 8 };
    server_dcid.data[0] = 0x01;
    nxp_conn_set_established(server, &server_dcid);

    /* Client opens a stream and sends data */
    uint64_t sid;
    NXP_ASSERT_OK(nxp_conn_open_stream(client, &sid, NXP_STREAM_RELIABLE, false));

    const uint8_t msg[] = "NXP round-trip test!";
    ssize_t w = nxp_conn_stream_send(client, sid, msg, sizeof(msg) - 1, true);
    NXP_ASSERT_EQ((size_t)w, sizeof(msg) - 1);

    /* Client generates a packet */
    uint8_t pkt_buf[1500];
    uint64_t now = 10000;
    ssize_t pkt_len = nxp_conn_send(client, pkt_buf, sizeof(pkt_buf), now);
    NXP_ASSERT(pkt_len > 0);

    /* Server receives the packet */
    NXP_ASSERT_OK(nxp_conn_recv(server, pkt_buf, (size_t)pkt_len, now + 100));

    /* Server should have created the stream and received data */
    nxp_stream_s *ss = (nxp_stream_s *)nxp_hash_map_get(server->streams, sid);
    NXP_ASSERT_NOT_NULL(ss);

    uint8_t recv_buf[64];
    bool fin = false;
    ssize_t nread = nxp_conn_stream_recv(server, sid, recv_buf, sizeof(recv_buf), &fin);
    NXP_ASSERT_EQ((size_t)nread, sizeof(msg) - 1);
    NXP_ASSERT(fin);
    NXP_ASSERT(memcmp(recv_buf, msg, sizeof(msg) - 1) == 0);

    nxp_conn_destroy(client);
    nxp_conn_destroy(server);
}

/* ── Test: Multiple Streams ───────────────────────────── */

NXP_TEST(conn_multiple_streams) {
    nxp_conn_config cfg = make_config(0x01);
    nxp_conn *conn = nxp_conn_create(&cfg, false);
    nxp_conn_id dcid = { .len = 8 };
    nxp_conn_set_established(conn, &dcid);

    /* Open 8 streams */
    uint64_t sids[8];
    for (int i = 0; i < 8; i++) {
        NXP_ASSERT_OK(nxp_conn_open_stream(conn, &sids[i], NXP_STREAM_RELIABLE, false));
    }
    NXP_ASSERT_EQ(conn->open_bidi_count, (uint32_t)8);

    /* Write data to each stream */
    for (int i = 0; i < 8; i++) {
        uint8_t data[32];
        memset(data, (uint8_t)i, sizeof(data));
        ssize_t w = nxp_conn_stream_send(conn, sids[i], data, sizeof(data), false);
        NXP_ASSERT_EQ((size_t)w, sizeof(data));
    }

    /* Generate packets - should contain stream data from multiple streams */
    uint8_t pkt_buf[1500];
    ssize_t total_sent = 0;
    for (int round = 0; round < 10; round++) {
        ssize_t n = nxp_conn_send(conn, pkt_buf, sizeof(pkt_buf), 1000 + (uint64_t)round * 100);
        if (n <= 0) break;
        total_sent += n;
    }
    NXP_ASSERT(total_sent > 0);

    nxp_conn_destroy(conn);
}

/* ── Test: Connection Close ───────────────────────────── */

NXP_TEST(conn_close) {
    nxp_conn_config cfg = make_config(0x01);
    nxp_conn *conn = nxp_conn_create(&cfg, false);
    nxp_conn_id dcid = { .len = 8 };
    nxp_conn_set_established(conn, &dcid);

    NXP_ASSERT_OK(nxp_conn_close(conn, 0));
    NXP_ASSERT(conn->send_conn_close);

    /* Generate the close packet */
    uint8_t pkt_buf[1500];
    ssize_t n = nxp_conn_send(conn, pkt_buf, sizeof(pkt_buf), 1000);
    NXP_ASSERT(n > 0);
    NXP_ASSERT_EQ(nxp_conn_get_state(conn), NXP_CONN_CLOSING);

    nxp_conn_destroy(conn);
}

/* ── Test: Idle Timeout ───────────────────────────────── */

NXP_TEST(conn_idle_timeout) {
    nxp_conn_config cfg = make_config(0x01);
    nxp_conn *conn = nxp_conn_create(&cfg, false);
    nxp_conn_id dcid = { .len = 8 };
    nxp_conn_set_established(conn, &dcid);
    conn->last_activity_us = 1000;

    /* Way past the idle timeout */
    nxp_conn_on_timeout(conn, 1000 + NXP_DEFAULT_IDLE_TIMEOUT + 1);
    NXP_ASSERT_EQ(nxp_conn_get_state(conn), NXP_CONN_CLOSED);

    nxp_conn_destroy(conn);
}

/* ── Test: Scheduler Round-Robin ──────────────────────── */

NXP_TEST(conn_scheduler_round_robin) {
    nxp_conn_config cfg = make_config(0x01);
    nxp_conn *conn = nxp_conn_create(&cfg, false);
    nxp_conn_id dcid = { .len = 8 };
    nxp_conn_set_established(conn, &dcid);

    /* Open 3 streams with data */
    uint64_t sids[3];
    for (int i = 0; i < 3; i++) {
        NXP_ASSERT_OK(nxp_conn_open_stream(conn, &sids[i], NXP_STREAM_RELIABLE, false));
        uint8_t data[64];
        memset(data, (uint8_t)i, sizeof(data));
        (void)nxp_conn_stream_send(conn, sids[i], data, sizeof(data), false);
    }

    /* The scheduler should be non-empty */
    NXP_ASSERT_NOT_NULL(conn->sched_head);

    /* Generate a packet - it should use streams round-robin */
    uint8_t pkt_buf[1500];
    ssize_t n = nxp_conn_send(conn, pkt_buf, sizeof(pkt_buf), 1000);
    NXP_ASSERT(n > 0);

    nxp_conn_destroy(conn);
}

/* ── Test: Nothing to send returns 0 ──────────────────── */

NXP_TEST(conn_send_nothing) {
    nxp_conn_config cfg = make_config(0x01);
    nxp_conn *conn = nxp_conn_create(&cfg, false);
    nxp_conn_id dcid = { .len = 8 };
    nxp_conn_set_established(conn, &dcid);

    /* Reset ack state so no ACK needed */
    conn->ack.ack_needed = false;
    conn->ack.recv_range_count = 0;

    uint8_t pkt_buf[1500];
    ssize_t n = nxp_conn_send(conn, pkt_buf, sizeof(pkt_buf), 1000);
    NXP_ASSERT_EQ(n, (ssize_t)0);

    nxp_conn_destroy(conn);
}

/* ── Main ──────────────────────────────────────────────── */

int main(void) {
    printf("=== NXP Connection Tests (Phase 4) ===\n");

    NXP_RUN_TEST(conn_create_destroy);
    NXP_RUN_TEST(conn_set_established);
    NXP_RUN_TEST(conn_open_stream);
    NXP_RUN_TEST(conn_stream_send_generates_packet);
    NXP_RUN_TEST(conn_round_trip);
    NXP_RUN_TEST(conn_multiple_streams);
    NXP_RUN_TEST(conn_close);
    NXP_RUN_TEST(conn_idle_timeout);
    NXP_RUN_TEST(conn_scheduler_round_robin);
    NXP_RUN_TEST(conn_send_nothing);

    NXP_TEST_SUMMARY();
}
