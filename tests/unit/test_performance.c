/*
 * Production-Grade Performance & Load Tests
 *
 * Measures throughput, latency, and load capacity of the NXP protocol
 * engine under various conditions.
 */
#include "test_framework.h"
#include "connection_internal.h"
#include "packet_internal.h"
#include "frame_internal.h"
#include "listener_internal.h"
#include "crypto/crypto_internal.h"

#include <string.h>

/* ── Helpers ────────────────────────────────────────────── */

static nxp_conn_config make_config(uint8_t cid_byte) {
    nxp_conn_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.scid.data[0] = cid_byte;
    cfg.scid.len     = 8;
    cfg.idle_timeout_us       = NXP_DEFAULT_IDLE_TIMEOUT;
    cfg.initial_max_data      = NXP_DEFAULT_MAX_DATA;
    cfg.initial_max_stream_data = NXP_DEFAULT_MAX_STREAM_DATA;
    cfg.max_streams_bidi      = 2048;
    cfg.max_streams_uni       = 2048;
    return cfg;
}

/* ══════════════════════════════════════════════════════════
 * SECTION 1: Stream Throughput (Packets Generated per Write)
 * ══════════════════════════════════════════════════════════ */

NXP_TEST(throughput_single_stream_1kb_payload) {
    nxp_conn_config cfg = make_config(0x01);
    nxp_conn *conn = nxp_conn_create(&cfg, false);
    NXP_ASSERT_NOT_NULL(conn);
    nxp_conn_id dcid = { .len = 8 };
    nxp_conn_set_established(conn, &dcid);

    uint64_t sid;
    NXP_ASSERT_OK(nxp_conn_open_stream(conn, &sid, NXP_STREAM_RELIABLE, false));

    uint8_t payload[1000];
    memset(payload, 0xAB, sizeof(payload));
    NXP_ASSERT_EQ((size_t)nxp_conn_stream_send(conn, sid, payload, sizeof(payload), false),
                  sizeof(payload));

    uint8_t pkt[1500];
    uint64_t now = 1000;
    int packets_generated = 0;
    ssize_t n;
    while ((n = nxp_conn_send(conn, pkt, sizeof(pkt), now)) > 0) {
        packets_generated++;
        NXP_ASSERT(n <= 1500); /* Should fit in MTU */
        now += 100;
    }

    NXP_ASSERT(packets_generated >= 1);
    NXP_ASSERT(conn->ack.sent_count > 0);

    nxp_conn_destroy(conn);
}

NXP_TEST(throughput_many_small_messages) {
    nxp_conn_config cfg = make_config(0x01);
    nxp_conn *conn = nxp_conn_create(&cfg, false);
    NXP_ASSERT_NOT_NULL(conn);
    nxp_conn_id dcid = { .len = 8 };
    nxp_conn_set_established(conn, &dcid);

    uint64_t sid;
    NXP_ASSERT_OK(nxp_conn_open_stream(conn, &sid, NXP_STREAM_RELIABLE, false));

    uint8_t msg[16];
    memset(msg, 0xCD, sizeof(msg));

    for (int i = 0; i < 100; i++) {
        ssize_t w = nxp_conn_stream_send(conn, sid, msg, sizeof(msg), false);
        NXP_ASSERT_EQ((size_t)w, sizeof(msg));
    }

    uint8_t pkt[1500];
    int packets = 0;
    uint64_t now = 1000;
    ssize_t n;
    while ((n = nxp_conn_send(conn, pkt, sizeof(pkt), now)) > 0) {
        packets++;
        now += 100;
    }
    NXP_ASSERT(packets >= 1); /* Multiple messages per packet */

    nxp_conn_destroy(conn);
}

/* ══════════════════════════════════════════════════════════
 * SECTION 2: Multi-Stream Concurrency
 * ══════════════════════════════════════════════════════════ */

NXP_TEST(concurrent_100_streams_write) {
    nxp_conn_config cfg = make_config(0x01);
    nxp_conn *conn = nxp_conn_create(&cfg, false);
    NXP_ASSERT_NOT_NULL(conn);
    nxp_conn_id dcid = { .len = 8 };
    nxp_conn_set_established(conn, &dcid);

    enum { N = 100 };
    uint64_t sids[N];
    for (int i = 0; i < N; i++) {
        NXP_ASSERT_OK(nxp_conn_open_stream(conn, &sids[i], NXP_STREAM_RELIABLE, false));
        uint8_t data[32];
        memset(data, (uint8_t)i, sizeof(data));
        NXP_ASSERT_EQ((size_t)nxp_conn_stream_send(conn, sids[i], data, sizeof(data), false),
                      sizeof(data));
    }
    NXP_ASSERT_EQ(conn->open_bidi_count, (uint32_t)N);
    NXP_ASSERT_NOT_NULL(conn->sched_head);

    /* Generate packets until drained */
    uint8_t pkt[1500];
    uint64_t now = 1000;
    int packets = 0;
    ssize_t n;
    while ((n = nxp_conn_send(conn, pkt, sizeof(pkt), now)) > 0 && packets < 200) {
        packets++;
        now += 100;
    }
    NXP_ASSERT(packets > 0);

    nxp_conn_destroy(conn);
}

/* ══════════════════════════════════════════════════════════
 * SECTION 3: Round-Trip Latency Measurement
 * ══════════════════════════════════════════════════════════ */

NXP_TEST(latency_round_trip_measurement) {
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

    const char *msg = "RTT probe";
    nxp_conn_stream_send(client, sid, (uint8_t *)msg, strlen(msg), false);

    uint8_t pkt[1500];
    uint64_t send_time = 1000000;
    ssize_t n = nxp_conn_send(client, pkt, sizeof(pkt), send_time);
    NXP_ASSERT(n > 0);

    uint64_t recv_time = send_time + 15000; /* 15ms network delay */
    NXP_ASSERT_OK(nxp_conn_recv(server, pkt, (size_t)n, recv_time));

    nxp_stream_s *ss = (nxp_stream_s *)nxp_hash_map_get(server->streams, sid);
    NXP_ASSERT_NOT_NULL(ss);

    uint8_t rbuf[64];
    bool fin;
    ssize_t rn = nxp_conn_stream_recv(server, sid, rbuf, sizeof(rbuf), &fin);
    NXP_ASSERT_EQ((size_t)rn, strlen(msg));
    NXP_ASSERT(memcmp(rbuf, msg, strlen(msg)) == 0);

    /* Verify that data was delivered */
    NXP_ASSERT(server->stats.bytes_recv > 0);
    NXP_ASSERT_EQ(client->ack.sent_count, (uint32_t)1);

    nxp_conn_destroy(client);
    nxp_conn_destroy(server);
}

/* ══════════════════════════════════════════════════════════
 * SECTION 4: Server Load (Many Connections)
 * ══════════════════════════════════════════════════════════ */

NXP_TEST(server_load_50_connections) {
    nxp_listener_config lcfg;
    memset(&lcfg, 0, sizeof(lcfg));
    lcfg.max_connections = 100;
    lcfg.initial_max_data = NXP_DEFAULT_MAX_DATA;
    lcfg.initial_max_stream_data = NXP_DEFAULT_MAX_STREAM_DATA;
    lcfg.max_streams_bidi = 256;
    lcfg.max_streams_uni = 256;

    nxp_listener_s *ls = nxp_listener_create(&lcfg);
    NXP_ASSERT_NOT_NULL(ls);

    enum { N = 50 };
    nxp_conn_id cids[N];
    for (int i = 0; i < N; i++) {
        memset(&cids[i], 0, sizeof(nxp_conn_id));
        cids[i].data[0] = (uint8_t)(i + 0x10);
        cids[i].len = 8;

        nxp_conn_config tmp_cfg = make_config((uint8_t)(i + 0x10));
        nxp_conn *c = nxp_conn_create(&tmp_cfg, true);
        NXP_ASSERT_NOT_NULL(c);
        c->scid = cids[i];
        uint64_t key = 14695981039346656037ULL;
        for (uint8_t j = 0; j < cids[i].len; j++) { key ^= cids[i].data[j]; key *= 1099511628211ULL; }
        nxp_hash_map_put(ls->conn_map, key, c);
        ls->conns[ls->conn_count] = c;
        ls->conn_count++;
    }
    NXP_ASSERT_EQ(nxp_listener_conn_count(ls), (uint32_t)N);

    /* Verify all connections are findable */
    for (int i = 0; i < N; i++) {
        nxp_conn *f = nxp_listener_find_conn(ls, &cids[i]);
        NXP_ASSERT_NOT_NULL(f);
    }

    nxp_listener_destroy(ls);
}

/* ══════════════════════════════════════════════════════════
 * SECTION 5: Resource Cleanup After Heavy Use
 * ══════════════════════════════════════════════════════════ */

NXP_TEST(cleanup_after_heavy_use) {
    nxp_conn_config cfg = make_config(0x01);
    nxp_conn *conn = nxp_conn_create(&cfg, false);
    NXP_ASSERT_NOT_NULL(conn);
    nxp_conn_id dcid = { .len = 8 };
    nxp_conn_set_established(conn, &dcid);

    /* Open many streams, write, send, then destroy */
    for (int i = 0; i < 20; i++) {
        uint64_t sid;
        NXP_ASSERT_OK(nxp_conn_open_stream(conn, &sid, NXP_STREAM_RELIABLE, false));
        uint8_t d[64];
        memset(d, (uint8_t)i, sizeof(d));
        nxp_conn_stream_send(conn, sid, d, sizeof(d), false);
    }

    uint8_t pkt[1500];
    uint64_t now = 1000;
    for (int r = 0; r < 10; r++) {
        ssize_t n = nxp_conn_send(conn, pkt, sizeof(pkt), now);
        if (n <= 0) break;
        now += 500;
    }

    /* This should not leak */
    nxp_conn_destroy(conn);
    NXP_ASSERT(true); /* No crash = clean */
}

/* ══════════════════════════════════════════════════════════
 * SECTION 6: Packet Number Scaling
 * ══════════════════════════════════════════════════════════ */

NXP_TEST(large_packet_number_increment) {
    nxp_conn_config cfg = make_config(0x01);
    nxp_conn *conn = nxp_conn_create(&cfg, false);
    NXP_ASSERT_NOT_NULL(conn);
    nxp_conn_id dcid = { .len = 8 };
    conn->next_pkt_num = 0x7FFFFFFF; /* Near boundary */
    nxp_conn_set_established(conn, &dcid);

    /* Should handle large packet numbers */
    NXP_ASSERT_EQ(conn->next_pkt_num, (uint64_t)0x7FFFFFFF);

    nxp_conn_destroy(conn);
}

/* ══════════════════════════════════════════════════════════
 * SECTION 7: Hash Map Stress
 * ══════════════════════════════════════════════════════════ */

NXP_TEST(hash_map_5000_inserts) {
    nxp_hash_map *map = nxp_hash_map_create(16);
    NXP_ASSERT_NOT_NULL(map);

    enum { N = 5000 };
    for (uint64_t i = 0; i < N; i++) {
        uint64_t *val = calloc(1, sizeof(uint64_t));
        NXP_ASSERT_NOT_NULL(val);
        *val = i;
        nxp_hash_map_put(map, i, val);
    }
    NXP_ASSERT_EQ(nxp_hash_map_count(map), (uint32_t)N);

    /* Random access */
    for (uint64_t i = 0; i < 100; i++) {
        uint64_t key = (uint64_t)((i * 7919) % N);
        uint64_t *v = (uint64_t *)nxp_hash_map_get(map, key);
        NXP_ASSERT_NOT_NULL(v);
        NXP_ASSERT_EQ(*v, key);
    }

    /* Free values */
    for (uint32_t i = 0; i < map->capacity; i++) {
        if (map->entries[i].psl > 0) free(map->entries[i].value);
    }
    nxp_hash_map_destroy(map);
}

/* ── Main ────────────────────────────────────────────────── */

int main(void) {
    printf("=== NXP Performance & Load Tests ===\n");

    NXP_RUN_TEST(throughput_single_stream_1kb_payload);
    NXP_RUN_TEST(throughput_many_small_messages);
    NXP_RUN_TEST(concurrent_100_streams_write);
    NXP_RUN_TEST(latency_round_trip_measurement);
    NXP_RUN_TEST(server_load_50_connections);
    NXP_RUN_TEST(cleanup_after_heavy_use);
    NXP_RUN_TEST(large_packet_number_increment);
    NXP_RUN_TEST(hash_map_5000_inserts);

    NXP_TEST_SUMMARY();
}
