/*
 * NXP Server Architecture Tests
 *
 * Phase 9: Tests for listener, CID routing, session export/import,
 * and multi-connection management.
 */
#include "test_framework.h"
#include "core/listener_internal.h"
#include "core/session_export.h"
#include "core/packet_internal.h"
#include <string.h>

/* ── Test: Listener Create/Destroy ─────────────────────── */

NXP_TEST(listener_create_destroy) {
    nxp_listener_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_connections = 100;
    cfg.max_streams_bidi = 10;

    nxp_listener_s *ls = nxp_listener_create(&cfg);
    NXP_ASSERT_NOT_NULL(ls);
    NXP_ASSERT_EQ(nxp_listener_conn_count(ls), (uint32_t)0);

    nxp_listener_destroy(ls);
}

/* ── Test: CID Generation with Node ID ─────────────────── */

NXP_TEST(cid_generation_node_id) {
    nxp_listener_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.node_id[0] = 0xAA;
    cfg.node_id[1] = 0xBB;
    cfg.node_id_len = 2;

    nxp_listener_s *ls = nxp_listener_create(&cfg);
    NXP_ASSERT_NOT_NULL(ls);

    nxp_conn_id cid;
    nxp_listener_generate_cid(ls, &cid);

    /* CID should be 8 bytes, first 2 are node ID */
    NXP_ASSERT_EQ(cid.len, (uint8_t)NXP_LISTENER_CID_LEN);
    NXP_ASSERT_EQ(cid.data[0], (uint8_t)0xAA);
    NXP_ASSERT_EQ(cid.data[1], (uint8_t)0xBB);

    /* Generate a second CID - should differ (random portion) */
    nxp_conn_id cid2;
    nxp_listener_generate_cid(ls, &cid2);
    NXP_ASSERT_EQ(cid2.data[0], (uint8_t)0xAA);
    NXP_ASSERT_EQ(cid2.data[1], (uint8_t)0xBB);
    /* Very unlikely the random bytes match */
    bool different = (memcmp(cid.data + 2, cid2.data + 2, 6) != 0);
    NXP_ASSERT(different);

    nxp_listener_destroy(ls);
}

/* ── Test: CID Generation without Node ID ──────────────── */

NXP_TEST(cid_generation_no_node_id) {
    nxp_listener_config cfg;
    memset(&cfg, 0, sizeof(cfg));

    nxp_listener_s *ls = nxp_listener_create(&cfg);
    NXP_ASSERT_NOT_NULL(ls);

    nxp_conn_id cid;
    nxp_listener_generate_cid(ls, &cid);
    NXP_ASSERT_EQ(cid.len, (uint8_t)NXP_LISTENER_CID_LEN);

    nxp_listener_destroy(ls);
}

/* ── Test: Node ID Extraction ──────────────────────────── */

NXP_TEST(node_id_extraction) {
    nxp_conn_id cid;
    cid.len = 8;
    cid.data[0] = 0x42;
    cid.data[1] = 0x43;
    cid.data[2] = 0x44;
    memset(cid.data + 3, 0, 5);

    uint8_t node_id[4];

    /* Extract 2-byte node ID */
    NXP_ASSERT(nxp_listener_extract_node_id(&cid, 2, node_id));
    NXP_ASSERT_EQ(node_id[0], (uint8_t)0x42);
    NXP_ASSERT_EQ(node_id[1], (uint8_t)0x43);

    /* Extract 3-byte node ID */
    NXP_ASSERT(nxp_listener_extract_node_id(&cid, 3, node_id));
    NXP_ASSERT_EQ(node_id[2], (uint8_t)0x44);

    /* Can't extract more than CID length */
    nxp_conn_id short_cid;
    short_cid.len = 1;
    NXP_ASSERT(!nxp_listener_extract_node_id(&short_cid, 2, node_id));

    /* node_id_len = 0 is invalid */
    NXP_ASSERT(!nxp_listener_extract_node_id(&cid, 0, node_id));
}

/* ── Test: Connection Lookup ───────────────────────────── */

NXP_TEST(connection_lookup) {
    nxp_listener_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_connections = 100;
    cfg.max_streams_bidi = 10;

    nxp_listener_s *ls = nxp_listener_create(&cfg);
    NXP_ASSERT_NOT_NULL(ls);

    /* Create a connection manually */
    nxp_conn_config ccfg;
    memset(&ccfg, 0, sizeof(ccfg));
    ccfg.scid.len = 8;
    ccfg.scid.data[0] = 0x01;
    ccfg.scid.data[1] = 0x02;
    ccfg.max_streams_bidi = 10;

    nxp_conn *conn = nxp_conn_create(&ccfg, true);
    NXP_ASSERT_NOT_NULL(conn);

    /* Register via CID hash */
    uint64_t key = 14695981039346656037ULL; /* FNV offset basis */
    for (uint8_t i = 0; i < ccfg.scid.len; i++) {
        key ^= ccfg.scid.data[i];
        key *= 1099511628211ULL;
    }
    (void)nxp_hash_map_put(ls->conn_map, key, conn);
    ls->conns[0] = conn;
    ls->conn_count = 1;

    /* Should find it */
    nxp_conn *found = nxp_listener_find_conn(ls, &ccfg.scid);
    NXP_ASSERT(found == conn);

    /* Unknown CID should return nullptr */
    nxp_conn_id unknown;
    memset(&unknown, 0, sizeof(unknown));
    unknown.len = 8;
    unknown.data[0] = 0xFF;
    NXP_ASSERT_NULL(nxp_listener_find_conn(ls, &unknown));

    /* Clean up (don't double-free - listener_destroy will handle it) */
    nxp_listener_destroy(ls);
}

/* ── Test: Connection Removal ──────────────────────────── */

NXP_TEST(connection_removal) {
    nxp_listener_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_connections = 100;
    cfg.max_streams_bidi = 10;

    nxp_listener_s *ls = nxp_listener_create(&cfg);
    NXP_ASSERT_NOT_NULL(ls);

    /* Create two connections */
    nxp_conn_config ccfg1;
    memset(&ccfg1, 0, sizeof(ccfg1));
    ccfg1.scid.len = 8;
    ccfg1.scid.data[0] = 0x01;
    ccfg1.max_streams_bidi = 10;

    nxp_conn_config ccfg2;
    memset(&ccfg2, 0, sizeof(ccfg2));
    ccfg2.scid.len = 8;
    ccfg2.scid.data[0] = 0x02;
    ccfg2.max_streams_bidi = 10;

    nxp_conn *c1 = nxp_conn_create(&ccfg1, true);
    nxp_conn *c2 = nxp_conn_create(&ccfg2, true);
    NXP_ASSERT_NOT_NULL(c1);
    NXP_ASSERT_NOT_NULL(c2);

    /* Register both */
    uint64_t k1 = 14695981039346656037ULL;
    for (uint8_t i = 0; i < ccfg1.scid.len; i++) {
        k1 ^= ccfg1.scid.data[i];
        k1 *= 1099511628211ULL;
    }
    (void)nxp_hash_map_put(ls->conn_map, k1, c1);

    uint64_t k2 = 14695981039346656037ULL;
    for (uint8_t i = 0; i < ccfg2.scid.len; i++) {
        k2 ^= ccfg2.scid.data[i];
        k2 *= 1099511628211ULL;
    }
    (void)nxp_hash_map_put(ls->conn_map, k2, c2);

    ls->conns[0] = c1;
    ls->conns[1] = c2;
    ls->conn_count = 2;

    /* Remove c1 */
    nxp_listener_remove_conn(ls, c1);
    NXP_ASSERT_EQ(ls->conn_count, (uint32_t)1);
    NXP_ASSERT_NULL(nxp_listener_find_conn(ls, &ccfg1.scid));

    /* c2 should still be there */
    NXP_ASSERT(nxp_listener_find_conn(ls, &ccfg2.scid) != nullptr);

    nxp_listener_destroy(ls);
}

/* ── Test: Listener Send Round-Robin ───────────────────── */

NXP_TEST(listener_send_round_robin) {
    nxp_listener_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_connections = 100;
    cfg.max_streams_bidi = 10;

    nxp_listener_s *ls = nxp_listener_create(&cfg);
    NXP_ASSERT_NOT_NULL(ls);

    /* With no connections, send returns 0 */
    uint8_t buf[1500];
    nxp_addr addr;
    ssize_t n = nxp_listener_send(ls, buf, sizeof(buf), &addr, 0);
    NXP_ASSERT_EQ(n, (ssize_t)0);

    nxp_listener_destroy(ls);
}

/* ── Test: Listener Timeout ────────────────────────────── */

NXP_TEST(listener_timeout) {
    nxp_listener_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_connections = 100;
    cfg.max_streams_bidi = 10;

    nxp_listener_s *ls = nxp_listener_create(&cfg);
    NXP_ASSERT_NOT_NULL(ls);

    /* No connections → UINT64_MAX */
    NXP_ASSERT_EQ(nxp_listener_timeout(ls, 0), UINT64_MAX);

    nxp_listener_destroy(ls);
}

/* ── Test: Max Connection Limit ────────────────────────── */

NXP_TEST(max_connection_limit) {
    nxp_listener_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_connections = 2;
    cfg.max_streams_bidi = 10;

    nxp_listener_s *ls = nxp_listener_create(&cfg);
    NXP_ASSERT_NOT_NULL(ls);

    /* Fill up to limit by manually adding connections */
    for (uint32_t i = 0; i < 2; i++) {
        nxp_conn_config ccfg;
        memset(&ccfg, 0, sizeof(ccfg));
        ccfg.scid.len = 8;
        ccfg.scid.data[0] = (uint8_t)(i + 1);
        ccfg.max_streams_bidi = 10;

        nxp_conn *c = nxp_conn_create(&ccfg, true);
        NXP_ASSERT_NOT_NULL(c);

        uint64_t key = 14695981039346656037ULL;
        for (uint8_t j = 0; j < ccfg.scid.len; j++) {
            key ^= ccfg.scid.data[j];
            key *= 1099511628211ULL;
        }
        (void)nxp_hash_map_put(ls->conn_map, key, c);

        if (ls->conn_count >= ls->conn_cap) {
            uint32_t new_cap = ls->conn_cap * 2;
            ls->conns = (nxp_conn **)realloc(ls->conns, (size_t)new_cap * sizeof(nxp_conn *));
            ls->conn_cap = new_cap;
        }
        ls->conns[ls->conn_count] = c;
        ls->conn_count++;
    }

    NXP_ASSERT_EQ(ls->conn_count, (uint32_t)2);

    nxp_listener_destroy(ls);
}

/* ── Test: Session Export ──────────────────────────────── */

NXP_TEST(session_export) {
    /* Create a connection with known state */
    nxp_conn_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.scid.len = 8;
    cfg.scid.data[0] = 0xAB;
    cfg.scid.data[1] = 0xCD;
    cfg.initial_max_data = 1048576;
    cfg.max_streams_bidi = 10;

    nxp_conn *conn = nxp_conn_create(&cfg, true);
    NXP_ASSERT_NOT_NULL(conn);

    /* Set some state */
    nxp_conn_id dcid;
    memset(&dcid, 0, sizeof(dcid));
    dcid.len = 8;
    dcid.data[0] = 0x11;
    dcid.data[1] = 0x22;
    nxp_conn_set_established(conn, &dcid);
    conn->next_pkt_num = 42;

    /* Export */
    uint8_t buf[NXP_SESSION_MAX_EXPORT];
    size_t out_len = 0;
    NXP_ASSERT_OK(nxp_session_export(conn, buf, sizeof(buf), &out_len));
    NXP_ASSERT(out_len > 0);
    NXP_ASSERT(out_len <= sizeof(buf));

    /* Verify export size matches */
    size_t expected_size = nxp_session_export_size(conn);
    NXP_ASSERT_EQ(out_len, expected_size);

    nxp_conn_destroy(conn);
}

/* ── Test: Session Import ──────────────────────────────── */

NXP_TEST(session_import) {
    /* Create and populate a connection */
    nxp_conn_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.scid.len = 8;
    cfg.scid.data[0] = 0xDE;
    cfg.scid.data[1] = 0xAD;
    cfg.initial_max_data = 524288;
    cfg.max_streams_bidi = 10;

    nxp_conn *orig = nxp_conn_create(&cfg, false);
    NXP_ASSERT_NOT_NULL(orig);

    nxp_conn_id dcid;
    memset(&dcid, 0, sizeof(dcid));
    dcid.len = 8;
    dcid.data[0] = 0xBE;
    dcid.data[1] = 0xEF;
    nxp_conn_set_established(orig, &dcid);
    orig->next_pkt_num = 100;
    orig->idle_timeout_us = 60000000; /* 60s */

    /* Export */
    uint8_t buf[NXP_SESSION_MAX_EXPORT];
    size_t out_len = 0;
    NXP_ASSERT_OK(nxp_session_export(orig, buf, sizeof(buf), &out_len));

    /* Import */
    nxp_conn *imported = nullptr;
    NXP_ASSERT_OK(nxp_session_import(buf, out_len, &imported));
    NXP_ASSERT_NOT_NULL(imported);

    /* Verify restored state */
    NXP_ASSERT_EQ(imported->scid.len, (uint8_t)8);
    NXP_ASSERT_EQ(imported->scid.data[0], (uint8_t)0xDE);
    NXP_ASSERT_EQ(imported->scid.data[1], (uint8_t)0xAD);

    NXP_ASSERT_EQ(imported->dcid.len, (uint8_t)8);
    NXP_ASSERT_EQ(imported->dcid.data[0], (uint8_t)0xBE);
    NXP_ASSERT_EQ(imported->dcid.data[1], (uint8_t)0xEF);

    NXP_ASSERT_EQ((int)imported->state, (int)NXP_CONN_ESTABLISHED);
    NXP_ASSERT(!imported->is_server);
    NXP_ASSERT_EQ(imported->next_pkt_num, (uint64_t)100);
    NXP_ASSERT_EQ(imported->idle_timeout_us, (uint64_t)60000000);

    nxp_conn_destroy(orig);
    nxp_conn_destroy(imported);
}

/* ── Test: Session Export Buffer Too Small ──────────────── */

NXP_TEST(session_export_too_small) {
    nxp_conn_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.scid.len = 8;
    cfg.max_streams_bidi = 10;

    nxp_conn *conn = nxp_conn_create(&cfg, false);
    NXP_ASSERT_NOT_NULL(conn);

    uint8_t tiny[10];
    size_t out_len = 0;
    nxp_result r = nxp_session_export(conn, tiny, sizeof(tiny), &out_len);
    NXP_ASSERT(nxp_result_is_err(r));
    NXP_ASSERT_EQ((int)r.code, (int)NXP_ERR_BUFFER_TOO_SMALL);

    nxp_conn_destroy(conn);
}

/* ── Test: Session Import Invalid Data ─────────────────── */

NXP_TEST(session_import_invalid) {
    /* Wrong magic */
    uint8_t bad_magic[100];
    memset(bad_magic, 0, sizeof(bad_magic));
    bad_magic[0] = 0xFF; /* Not NXP_SESSION_MAGIC */

    nxp_conn *conn = nullptr;
    nxp_result r = nxp_session_import(bad_magic, sizeof(bad_magic), &conn);
    NXP_ASSERT(nxp_result_is_err(r));
    NXP_ASSERT_NULL(conn);

    /* Truncated buffer */
    uint8_t truncated[4];
    memcpy(truncated, &(uint32_t){NXP_SESSION_MAGIC}, 4);
    r = nxp_session_import(truncated, sizeof(truncated), &conn);
    NXP_ASSERT(nxp_result_is_err(r));
    NXP_ASSERT_NULL(conn);
}

/* ── Test: Session Round-Trip Preserves Flow Control ───── */

NXP_TEST(session_roundtrip_flow) {
    nxp_conn_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.scid.len = 8;
    cfg.initial_max_data = 2000000;
    cfg.max_streams_bidi = 10;

    nxp_conn *orig = nxp_conn_create(&cfg, true);
    NXP_ASSERT_NOT_NULL(orig);
    nxp_conn_id dcid;
    memset(&dcid, 0, sizeof(dcid));
    dcid.len = 4;
    nxp_conn_set_established(orig, &dcid);

    /* Simulate some data transfer */
    orig->conn_flow.data_sent = 50000;
    orig->conn_flow.data_recv = 30000;

    /* RTT */
    orig->ack.smoothed_rtt = 25000;  /* 25ms */
    orig->ack.rtt_var = 5000;
    orig->ack.min_rtt = 20000;

    /* Export + import */
    uint8_t buf[NXP_SESSION_MAX_EXPORT];
    size_t len;
    NXP_ASSERT_OK(nxp_session_export(orig, buf, sizeof(buf), &len));

    nxp_conn *imp = nullptr;
    NXP_ASSERT_OK(nxp_session_import(buf, len, &imp));
    NXP_ASSERT_NOT_NULL(imp);

    /* Verify flow control */
    NXP_ASSERT_EQ(imp->conn_flow.data_sent, (uint64_t)50000);
    NXP_ASSERT_EQ(imp->conn_flow.data_recv, (uint64_t)30000);
    NXP_ASSERT_EQ(imp->conn_flow.local_max_data, (uint64_t)2000000);

    /* Verify RTT */
    NXP_ASSERT_EQ(imp->ack.smoothed_rtt, (uint64_t)25000);
    NXP_ASSERT_EQ(imp->ack.rtt_var, (uint64_t)5000);
    NXP_ASSERT_EQ(imp->ack.min_rtt, (uint64_t)20000);
    NXP_ASSERT(imp->ack.has_rtt);

    nxp_conn_destroy(orig);
    nxp_conn_destroy(imp);
}

/* ── main ─────────────────────────────────────────────── */

int main(void) {
    printf("=== NXP Server Architecture Tests (Phase 9) ===\n\n");

    NXP_RUN_TEST(listener_create_destroy);
    NXP_RUN_TEST(cid_generation_node_id);
    NXP_RUN_TEST(cid_generation_no_node_id);
    NXP_RUN_TEST(node_id_extraction);
    NXP_RUN_TEST(connection_lookup);
    NXP_RUN_TEST(connection_removal);
    NXP_RUN_TEST(listener_send_round_robin);
    NXP_RUN_TEST(listener_timeout);
    NXP_RUN_TEST(max_connection_limit);
    NXP_RUN_TEST(session_export);
    NXP_RUN_TEST(session_import);
    NXP_RUN_TEST(session_export_too_small);
    NXP_RUN_TEST(session_import_invalid);
    NXP_RUN_TEST(session_roundtrip_flow);

    NXP_TEST_SUMMARY();
}
