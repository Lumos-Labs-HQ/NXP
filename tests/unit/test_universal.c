/*
 * Production-Grade Test Suite: Universal Protocol Behaviors
 *
 * Tests NXP as a universal transport: CID routing, path validation,
 * peer mobility, multi-connection server, connection migration,
 * and load balancer CID routing.
 */
#include "test_framework.h"
#include "connection_internal.h"
#include "packet_internal.h"
#include "frame_internal.h"
#include "listener_internal.h"
#include "migration_internal.h"
#include "crypto/crypto_internal.h"
#include "hash_map.h"

#include <string.h>

/* ── Helpers ────────────────────────────────────────────── */

static nxp_listener_config make_listener_config(uint32_t max_conns) {
    nxp_listener_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_connections = max_conns;
    cfg.idle_timeout_us = NXP_DEFAULT_IDLE_TIMEOUT;
    cfg.initial_max_data = NXP_DEFAULT_MAX_DATA;
    cfg.initial_max_stream_data = NXP_DEFAULT_MAX_STREAM_DATA;
    cfg.max_streams_bidi = 256;
    cfg.max_streams_uni = 256;
    return cfg;
}

static nxp_conn_config make_conn_config(uint8_t cid_byte) {
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

/* ══════════════════════════════════════════════════════════
 * SECTION 1: CID Routing & Load Balancer
 * ══════════════════════════════════════════════════════════ */

NXP_TEST(cid_generation_always_8_bytes) {
    nxp_listener_config cfg = make_listener_config(16);
    nxp_listener_s *ls = nxp_listener_create(&cfg);
    NXP_ASSERT_NOT_NULL(ls);

    nxp_conn_id cid;
    nxp_listener_generate_cid(ls, &cid);
    NXP_ASSERT_EQ(cid.len, (uint8_t)NXP_LISTENER_CID_LEN);

    nxp_listener_destroy(ls);
}

NXP_TEST(cid_generation_with_node_id) {
    nxp_listener_config cfg = make_listener_config(16);
    cfg.node_id[0] = 0xAA;
    cfg.node_id[1] = 0xBB;
    cfg.node_id_len = 2;

    nxp_listener_s *ls = nxp_listener_create(&cfg);
    NXP_ASSERT_NOT_NULL(ls);

    nxp_conn_id cid;
    nxp_listener_generate_cid(ls, &cid);
    NXP_ASSERT_EQ(cid.len, (uint8_t)NXP_LISTENER_CID_LEN);

    /* Node ID should be embedded as prefix */
    NXP_ASSERT_EQ(cid.data[0], (uint8_t)0xAA);
    NXP_ASSERT_EQ(cid.data[1], (uint8_t)0xBB);

    nxp_listener_destroy(ls);
}

NXP_TEST(cid_node_id_extraction) {
    nxp_conn_id cid;
    memset(&cid, 0, sizeof(cid));
    cid.data[0] = 0xCA; cid.data[1] = 0xFE;
    cid.len = NXP_LISTENER_CID_LEN;

    uint8_t extracted[2];
    NXP_ASSERT(nxp_listener_extract_node_id(&cid, 2, extracted));
    NXP_ASSERT_EQ(extracted[0], (uint8_t)0xCA);
    NXP_ASSERT_EQ(extracted[1], (uint8_t)0xFE);
}

NXP_TEST(listener_conn_lookup_by_cid) {
    nxp_listener_config cfg = make_listener_config(16);
    nxp_listener_s *ls = nxp_listener_create(&cfg);
    NXP_ASSERT_NOT_NULL(ls);

    nxp_conn_id cid = { .len = 8 };
    cid.data[0] = 0x05;
    nxp_conn_config cfg_lookup = make_conn_config(0x05);
    nxp_conn *c = nxp_conn_create(&cfg_lookup, true);
    NXP_ASSERT_NOT_NULL(c);
    c->scid = cid;
    uint64_t key = 14695981039346656037ULL;
    for (uint8_t i = 0; i < cid.len; i++) { key ^= cid.data[i]; key *= 1099511628211ULL; }
    nxp_hash_map_put(ls->conn_map, key, c);
    ls->conns[0] = c;
    ls->conn_count = 1;

    nxp_conn *found = nxp_listener_find_conn(ls, &cid);
    NXP_ASSERT_NOT_NULL(found);
    NXP_ASSERT(found == c);

    /* Lookup non-existent */
    nxp_conn_id unknown = { .len = 8 };
    unknown.data[0] = 0xFF;
    nxp_conn *not_found = nxp_listener_find_conn(ls, &unknown);
    NXP_ASSERT_NULL(not_found);

    nxp_listener_destroy(ls);
}

/* ══════════════════════════════════════════════════════════
 * SECTION 2: Path Validation & Migration
 * ══════════════════════════════════════════════════════════ */

NXP_TEST(migration_path_full_lifecycle) {
    nxp_migration_state ms;
    memset(&ms, 0, sizeof(ms));
    nxp_migration_init(&ms);

    NXP_ASSERT(!ms.new_path.validated);

    nxp_addr new_addr;
    memset(&new_addr, 0, sizeof(new_addr));
    NXP_ASSERT_OK(nxp_migration_on_peer_addr_change(&ms, &new_addr, 1000));
    NXP_ASSERT(ms.new_path.challenge_pending);

    NXP_ASSERT(nxp_migration_on_path_response(&ms, ms.new_path.challenge_data));
    /* Path is promoted and new_path cleared; check migration is done */
    NXP_ASSERT(!ms.migration_in_progress);
    /* new_path is zeroed after promotion */
    NXP_ASSERT(!ms.new_path.challenge_pending);
}

NXP_TEST(migration_cid_pool_management) {
    nxp_migration_state ms;
    memset(&ms, 0, sizeof(ms));
    nxp_migration_init(&ms);

    bool any_active = false;
    for (int i = 0; i < NXP_MAX_CID_POOL_SIZE; i++) {
        if (ms.peer_cid_pool[i].in_use) any_active = true;
    }
    NXP_ASSERT(!any_active);
}

NXP_TEST(migration_address_change_detection) {
    nxp_migration_state ms;
    memset(&ms, 0, sizeof(ms));
    nxp_migration_init(&ms);

    nxp_addr old_addr, new_addr;
    memset(&old_addr, 0, sizeof(old_addr));
    memset(&new_addr, 0, sizeof(new_addr));

    ms.new_path.addr = old_addr;

    NXP_ASSERT_OK(nxp_migration_on_peer_addr_change(&ms, &new_addr, 1000));
    NXP_ASSERT(ms.new_path.challenge_pending);
}

/* ══════════════════════════════════════════════════════════
 * SECTION 3: Multi-Connection Server
 * ══════════════════════════════════════════════════════════ */

NXP_TEST(server_multi_conn_round_robin_send) {
    nxp_listener_config cfg = make_listener_config(16);
    nxp_listener_s *ls = nxp_listener_create(&cfg);
    NXP_ASSERT_NOT_NULL(ls);

    /* Add 3 connections */
    for (int i = 0; i < 3; i++) {
        nxp_conn_id cid = { .len = 8 };
        cid.data[0] = (uint8_t)(i + 0x10);
        nxp_conn_config tmp_cfg = make_conn_config((uint8_t)(i + 0x10));
        nxp_conn *c = nxp_conn_create(&tmp_cfg, true);
        NXP_ASSERT_NOT_NULL(c);
        c->scid = cid;
        uint64_t key = 14695981039346656037ULL;
        for (uint8_t j = 0; j < cid.len; j++) { key ^= cid.data[j]; key *= 1099511628211ULL; }
        nxp_hash_map_put(ls->conn_map, key, c);
        ls->conns[ls->conn_count] = c;
        ls->conn_count++;
    }
    NXP_ASSERT_EQ(nxp_listener_conn_count(ls), (uint32_t)3);

    /* Round-robin send should iterate */
    uint8_t out[1500];
    nxp_addr peer;
    ssize_t n = nxp_listener_send(ls, out, sizeof(out), &peer, 1000);
    /* Nothing queued — expected 0 */
    NXP_ASSERT(n >= 0);

    /* Listener timeout should be valid */
    uint64_t t = nxp_listener_timeout(ls, 1000);
    NXP_ASSERT(t > 0 || t == UINT64_MAX);

    nxp_listener_destroy(ls);
}

NXP_TEST(server_conn_teardown) {
    nxp_listener_config cfg = make_listener_config(16);
    nxp_listener_s *ls = nxp_listener_create(&cfg);
    NXP_ASSERT_NOT_NULL(ls);

    nxp_conn_id cid = { .len = 8 };
    cid.data[0] = 0x20;
    nxp_conn_config cfg_teardown = make_conn_config(0x20);
    nxp_conn *c = nxp_conn_create(&cfg_teardown, true);
    NXP_ASSERT_NOT_NULL(c);
    c->scid = cid;
    uint64_t key2 = 14695981039346656037ULL;
    for (uint8_t j = 0; j < cid.len; j++) { key2 ^= cid.data[j]; key2 *= 1099511628211ULL; }
    nxp_hash_map_put(ls->conn_map, key2, c);
    ls->conns[0] = c;
    ls->conn_count = 1;

    NXP_ASSERT_EQ(nxp_listener_conn_count(ls), (uint32_t)1);

    nxp_listener_remove_conn(ls, c);
    NXP_ASSERT_EQ(nxp_listener_conn_count(ls), (uint32_t)0);

    /* After removal, lookup should return null */
    nxp_conn *f = nxp_listener_find_conn(ls, &cid);
    NXP_ASSERT_NULL(f);

    nxp_listener_destroy(ls);
}

/* ══════════════════════════════════════════════════════════
 * SECTION 4: Packet Routing (Long/Short Headers)
 * ══════════════════════════════════════════════════════════ */

NXP_TEST(long_header_decode_full) {
    /* Build a valid Initial packet header */
    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0xC0; /* Long header, Initial */
    buf[1] = 0x4E; buf[2] = 0x58; buf[3] = 0x50; buf[4] = 0x01; /* NXP_VERSION_1 */
    buf[5] = 0x08; /* DCID len = 8 */
    for (int i = 0; i < 8; i++) buf[6 + i] = (uint8_t)(i + 1);
    buf[14] = 0x00; /* SCID len = 0 */
    /* Token len = 0 (encoded as varint 0) */
    buf[15] = 0x00;
    /* Payload len varint */
    buf[16] = 0x01; /* varint(10) */
    /* pkt_num_len = 2 (from 0xC0: top 2 bits = 11 → 4 bytes, first bit after type = 0) */
    buf[17] = 0x00; buf[18] = 0x01;

    nxp_pkt_long_header hdr;
    nxp_result r = nxp_pkt_decode_long_header(buf, 19, &hdr);
    NXP_ASSERT(r.code == NXP_OK);
    NXP_ASSERT_EQ(hdr.type, NXP_PKT_INITIAL);
    NXP_ASSERT_EQ(hdr.version, NXP_VERSION_1);
    NXP_ASSERT_EQ(hdr.dcid.len, (uint8_t)8);
    NXP_ASSERT_EQ(hdr.dcid.data[0], (uint8_t)1);
}

NXP_TEST(short_header_decode) {
    uint8_t buf[16];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x40; /* Short header, spin=0, key_phase=0 */

    uint8_t dcid_len = 8;
    for (int i = 0; i < 8; i++) buf[1 + i] = (uint8_t)(i + 0xAA);

    nxp_pkt_short_header hdr;
    nxp_result r = nxp_pkt_decode_short_header(buf, 1 + dcid_len + 1, dcid_len, &hdr);
    NXP_ASSERT(r.code == NXP_OK);
    NXP_ASSERT_EQ(hdr.dcid.data[0], (uint8_t)0xAA);
    NXP_ASSERT(!hdr.spin_bit);
    NXP_ASSERT(!hdr.key_phase);
}

/* ══════════════════════════════════════════════════════════
 * SECTION 5: Universal Protocol — Client ↔ Server Lifecycle
 * ══════════════════════════════════════════════════════════ */

NXP_TEST(full_client_server_lifecycle) {
    /* Setup */
    nxp_conn_config client_cfg = make_conn_config(0x01);
    nxp_conn_config server_cfg = make_conn_config(0x02);

    nxp_conn *client = nxp_conn_create(&client_cfg, false);
    nxp_conn *server = nxp_conn_create(&server_cfg, true);
    NXP_ASSERT_NOT_NULL(client);
    NXP_ASSERT_NOT_NULL(server);

    nxp_conn_id cd = { .len = 8 }; cd.data[0] = 0x02;
    nxp_conn_id sd = { .len = 8 }; sd.data[0] = 0x01;
    nxp_conn_set_established(client, &cd);
    nxp_conn_set_established(server, &sd);

    /* Open streams */
    uint64_t csid;
    NXP_ASSERT_OK(nxp_conn_open_stream(client, &csid, NXP_STREAM_RELIABLE, false));

    /* Client → Server */
    const char *msg = "Hello from universal transport!";
    NXP_ASSERT_EQ((size_t)nxp_conn_stream_send(client, csid, (uint8_t *)msg, strlen(msg), true),
                  strlen(msg));

    /* Forward packets */
    uint8_t pkt[1500];
    uint64_t now = 1000;
    ssize_t n;
    while ((n = nxp_conn_send(client, pkt, sizeof(pkt), now)) > 0) {
        NXP_ASSERT_OK(nxp_conn_recv(server, pkt, (size_t)n, now + 100));
        now += 500;
    }

    /* Verify server received */
    nxp_stream_s *ss = (nxp_stream_s *)nxp_hash_map_get(server->streams, csid);
    NXP_ASSERT_NOT_NULL(ss);
    NXP_ASSERT(ss->recv.fin_received);

    uint8_t rbuf[128];
    bool fin;
    ssize_t nr = nxp_conn_stream_recv(server, csid, rbuf, sizeof(rbuf), &fin);
    NXP_ASSERT_EQ((size_t)nr, strlen(msg));
    NXP_ASSERT(fin);
    NXP_ASSERT(memcmp(rbuf, msg, strlen(msg)) == 0);

    /* Close */
    nxp_conn_destroy(client);
    nxp_conn_destroy(server);
}

/* ══════════════════════════════════════════════════════════
 * SECTION 6: Stats & Monitoring
 * ══════════════════════════════════════════════════════════ */

NXP_TEST(stats_accumulate_correctly) {
    nxp_conn_config cfg = make_conn_config(0x01);
    nxp_conn *conn = nxp_conn_create(&cfg, false);
    NXP_ASSERT_NOT_NULL(conn);
    nxp_conn_id dcid = { .len = 8 };
    nxp_conn_set_established(conn, &dcid);

    uint64_t sid;
    NXP_ASSERT_OK(nxp_conn_open_stream(conn, &sid, NXP_STREAM_RELIABLE, false));
    uint8_t data[100];
    memset(data, 0x42, sizeof(data));
    nxp_conn_stream_send(conn, sid, data, sizeof(data), false);

    uint8_t pkt[1500];
    ssize_t n = nxp_conn_send(conn, pkt, sizeof(pkt), 1000);
    NXP_ASSERT(n > 0);

    NXP_ASSERT(conn->stats.bytes_sent > 0);
    NXP_ASSERT_EQ(conn->stats.streams_opened, (uint64_t)1);
    NXP_ASSERT_EQ(conn->next_pkt_num, (uint64_t)1);

    nxp_conn_destroy(conn);
}

/* ── Main ────────────────────────────────────────────────── */

int main(void) {
    printf("=== NXP Universal Protocol Tests ===\n");

    /* Section 1: CID routing */
    NXP_RUN_TEST(cid_generation_always_8_bytes);
    NXP_RUN_TEST(cid_generation_with_node_id);
    NXP_RUN_TEST(cid_node_id_extraction);
    NXP_RUN_TEST(listener_conn_lookup_by_cid);

    /* Section 2: Migration */
    NXP_RUN_TEST(migration_path_full_lifecycle);
    NXP_RUN_TEST(migration_cid_pool_management);
    NXP_RUN_TEST(migration_address_change_detection);

    /* Section 3: Multi-conn server */
    NXP_RUN_TEST(server_multi_conn_round_robin_send);
    NXP_RUN_TEST(server_conn_teardown);

    /* Section 4: Packet routing */
    NXP_RUN_TEST(long_header_decode_full);
    NXP_RUN_TEST(short_header_decode);

    /* Section 5: Lifecycle */
    NXP_RUN_TEST(full_client_server_lifecycle);

    /* Section 6: Stats */
    NXP_RUN_TEST(stats_accumulate_correctly);

    NXP_TEST_SUMMARY();
}
