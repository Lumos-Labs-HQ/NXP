/*
 * Unit tests: Handshake state machine (Phase 5)
 *
 * Tests the 1-RTT handshake: key exchange, key derivation,
 * handshake state transitions, and encrypted round-trip.
 */
#include "test_framework.h"
#include "connection_internal.h"
#include "handshake_internal.h"
#include <string.h>

/* Portable memmem (GNU extension not available everywhere) */
static void *nxp_memmem(const void *hay, size_t hay_len,
                         const void *needle, size_t needle_len) {
    if (needle_len == 0) return (void *)hay;
    if (needle_len > hay_len) return nullptr;
    const uint8_t *h = (const uint8_t *)hay;
    const uint8_t *n = (const uint8_t *)needle;
    for (size_t i = 0; i <= hay_len - needle_len; i++) {
        if (memcmp(h + i, n, needle_len) == 0) return (void *)(h + i);
    }
    return nullptr;
}

/* ── Helper: create a test config ────────────────────── */

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

/* ── Test: Handshake Create/Destroy ──────────────────── */

NXP_TEST(hs_create_destroy) {
    nxp_handshake *hs = nxp_handshake_create(false);
    NXP_ASSERT_NOT_NULL(hs);
    NXP_ASSERT_EQ((int)hs->state, (int)NXP_HS_IDLE);
    NXP_ASSERT(!hs->is_server);

    /* Should have generated a keypair */
    bool has_pubkey = false;
    for (int i = 0; i < NXP_X25519_KEY_LEN; i++) {
        if (hs->local_pubkey[i] != 0) { has_pubkey = true; break; }
    }
    NXP_ASSERT(has_pubkey);

    nxp_handshake_destroy(hs);
}

/* ── Test: Client Start ──────────────────────────────── */

NXP_TEST(hs_client_start) {
    nxp_handshake *hs = nxp_handshake_create(false);
    NXP_ASSERT_NOT_NULL(hs);

    nxp_transport_params tp = {
        .initial_max_data = 1024 * 1024,
        .initial_max_stream_data = 256 * 1024,
        .max_streams_bidi = 128,
        .max_streams_uni = 64,
        .idle_timeout_us = 30000000,
    };
    nxp_handshake_set_local_params(hs, &tp);

    nxp_conn_id dcid;
    memset(&dcid, 0, sizeof(dcid));
    dcid.data[0] = 0x02;
    dcid.len = 8;

    NXP_ASSERT_OK(nxp_handshake_start_client(hs, &dcid));
    NXP_ASSERT_EQ((int)hs->state, (int)NXP_HS_WAIT_SERVER_HELLO);
    NXP_ASSERT(hs->initial_keys.available);
    NXP_ASSERT(nxp_handshake_has_data(hs));

    nxp_handshake_destroy(hs);
}

/* ── Test: Full Handshake (client <-> server) ────────── */

NXP_TEST(hs_full_exchange) {
    /* Create client and server handshake contexts */
    nxp_handshake *client = nxp_handshake_create(false);
    nxp_handshake *server = nxp_handshake_create(true);
    NXP_ASSERT_NOT_NULL(client);
    NXP_ASSERT_NOT_NULL(server);

    nxp_transport_params tp = {
        .initial_max_data = 1024 * 1024,
        .initial_max_stream_data = 256 * 1024,
        .max_streams_bidi = 128,
        .max_streams_uni = 64,
        .idle_timeout_us = 30000000,
    };
    nxp_handshake_set_local_params(client, &tp);
    nxp_handshake_set_local_params(server, &tp);

    /* Use server's SCID as client's DCID */
    nxp_conn_id server_cid;
    memset(&server_cid, 0, sizeof(server_cid));
    server_cid.data[0] = 0x02;
    server_cid.len = 8;

    /* Client starts handshake */
    NXP_ASSERT_OK(nxp_handshake_start_client(client, &server_cid));
    NXP_ASSERT_EQ((int)client->state, (int)NXP_HS_WAIT_SERVER_HELLO);

    /* Server starts handshake */
    NXP_ASSERT_OK(nxp_handshake_start_server(server, &server_cid));
    NXP_ASSERT_EQ((int)server->state, (int)NXP_HS_WAIT_CLIENT_HELLO);

    /* Client has ClientHello to send */
    uint8_t ch_buf[512];
    uint64_t ch_offset;
    size_t ch_len = nxp_handshake_fill_crypto(client, ch_buf, sizeof(ch_buf), &ch_offset);
    NXP_ASSERT(ch_len > 0);
    NXP_ASSERT_EQ(ch_buf[0], (uint8_t)NXP_HS_CLIENT_HELLO);

    /* Server processes ClientHello */
    NXP_ASSERT_OK(nxp_handshake_recv_crypto(server, NXP_CRYPTO_INITIAL,
                                             ch_buf, ch_len));
    NXP_ASSERT_EQ((int)server->state, (int)NXP_HS_COMPLETE);
    NXP_ASSERT(server->handshake_keys.available);
    NXP_ASSERT(server->app_keys.available);
    NXP_ASSERT(server->has_peer_params);

    /* Server has ServerHello to send */
    uint8_t sh_buf[512];
    uint64_t sh_offset;
    size_t sh_len = nxp_handshake_fill_crypto(server, sh_buf, sizeof(sh_buf), &sh_offset);
    NXP_ASSERT(sh_len > 0);
    NXP_ASSERT_EQ(sh_buf[0], (uint8_t)NXP_HS_SERVER_HELLO);

    /* Client processes ServerHello */
    NXP_ASSERT_OK(nxp_handshake_recv_crypto(client, NXP_CRYPTO_INITIAL,
                                             sh_buf, sh_len));
    NXP_ASSERT_EQ((int)client->state, (int)NXP_HS_WAIT_HANDSHAKE_DONE);
    NXP_ASSERT(client->handshake_keys.available);
    NXP_ASSERT(client->app_keys.available);

    /* Client receives HANDSHAKE_DONE */
    NXP_ASSERT_OK(nxp_handshake_on_handshake_done(client));
    NXP_ASSERT_EQ((int)client->state, (int)NXP_HS_COMPLETE);

    /* Both sides should have derived the same application keys */
    NXP_ASSERT(memcmp(client->app_keys.send.key,
                       server->app_keys.recv.key, 32) == 0);
    NXP_ASSERT(memcmp(client->app_keys.recv.key,
                       server->app_keys.send.key, 32) == 0);
    NXP_ASSERT(memcmp(client->app_keys.send.iv,
                       server->app_keys.recv.iv, 12) == 0);

    nxp_handshake_destroy(client);
    nxp_handshake_destroy(server);
}

/* ── Test: Encrypted Round-Trip (Connection Level) ───── */

NXP_TEST(hs_encrypted_round_trip) {
    /* Create client and server connections */
    nxp_conn_config client_cfg = make_config(0x01);
    nxp_conn_config server_cfg = make_config(0x02);

    nxp_conn *client = nxp_conn_create(&client_cfg, false);
    nxp_conn *server = nxp_conn_create(&server_cfg, true);
    NXP_ASSERT_NOT_NULL(client);
    NXP_ASSERT_NOT_NULL(server);

    /* Start handshakes */
    NXP_ASSERT_OK(nxp_conn_start_handshake(client, &server_cfg.scid));
    NXP_ASSERT_OK(nxp_conn_start_handshake(server, &server_cfg.scid));

    NXP_ASSERT_EQ((int)nxp_conn_get_state(client), (int)NXP_CONN_HANDSHAKE_INITIAL);
    NXP_ASSERT_EQ((int)nxp_conn_get_state(server), (int)NXP_CONN_HANDSHAKE_INITIAL);

    /* Client generates Initial packet (ClientHello) */
    uint8_t pkt1[NXP_PACKET_BUF_SIZE];
    ssize_t pkt1_len = nxp_conn_send(client, pkt1, sizeof(pkt1), 1000);
    NXP_ASSERT(pkt1_len > 0);

    /* Server receives Initial packet */
    NXP_ASSERT_OK(nxp_conn_recv(server, pkt1, (size_t)pkt1_len, 1100));

    /* Server should be ESTABLISHED now (processed ClientHello, derived keys) */
    NXP_ASSERT_EQ((int)nxp_conn_get_state(server), (int)NXP_CONN_ESTABLISHED);

    /* Server generates Handshake packet (ServerHello) */
    uint8_t pkt2[NXP_PACKET_BUF_SIZE];
    ssize_t pkt2_len = nxp_conn_send(server, pkt2, sizeof(pkt2), 1200);
    NXP_ASSERT(pkt2_len > 0);

    /* Client receives Handshake packet */
    NXP_ASSERT_OK(nxp_conn_recv(client, pkt2, (size_t)pkt2_len, 1300));

    /* Server generates HANDSHAKE_DONE packet (1-RTT) */
    uint8_t pkt3[NXP_PACKET_BUF_SIZE];
    ssize_t pkt3_len = nxp_conn_send(server, pkt3, sizeof(pkt3), 1400);
    NXP_ASSERT(pkt3_len > 0);

    /* Client receives HANDSHAKE_DONE - should now be ESTABLISHED */
    NXP_ASSERT_OK(nxp_conn_recv(client, pkt3, (size_t)pkt3_len, 1500));
    NXP_ASSERT_EQ((int)nxp_conn_get_state(client), (int)NXP_CONN_ESTABLISHED);

    /* Both sides should now have crypto enabled */
    NXP_ASSERT(client->crypto.available);
    NXP_ASSERT(server->crypto.available);

    /* --- Now test encrypted data exchange --- */

    /* Client opens a stream and sends data */
    uint64_t sid;
    NXP_ASSERT_OK(nxp_conn_open_stream(client, &sid, NXP_STREAM_RELIABLE, false));

    const uint8_t msg[] = "Encrypted NXP data!";
    ssize_t w = nxp_conn_stream_send(client, sid, msg, sizeof(msg) - 1, true);
    NXP_ASSERT_EQ((size_t)w, sizeof(msg) - 1);

    /* Client generates encrypted 1-RTT packet */
    uint8_t pkt4[NXP_PACKET_BUF_SIZE];
    ssize_t pkt4_len = nxp_conn_send(client, pkt4, sizeof(pkt4), 2000);
    NXP_ASSERT(pkt4_len > 0);

    /* The packet should be encrypted (not readable as plaintext) */
    /* Quick check: the raw bytes after the header should not contain our message */
    NXP_ASSERT(nxp_memmem(pkt4, (size_t)pkt4_len, msg, sizeof(msg) - 1) == nullptr);

    /* Server receives and decrypts */
    NXP_ASSERT_OK(nxp_conn_recv(server, pkt4, (size_t)pkt4_len, 2100));

    /* Server should have received the data */
    uint8_t recv_buf[64];
    bool fin = false;
    ssize_t nread = nxp_conn_stream_recv(server, sid, recv_buf, sizeof(recv_buf), &fin);
    NXP_ASSERT_EQ((size_t)nread, sizeof(msg) - 1);
    NXP_ASSERT(fin);
    NXP_ASSERT(memcmp(recv_buf, msg, sizeof(msg) - 1) == 0);

    nxp_conn_destroy(client);
    nxp_conn_destroy(server);
}

/* ── Test: Handshake Transport Parameters ────────────── */

NXP_TEST(hs_transport_params) {
    nxp_handshake *client = nxp_handshake_create(false);
    nxp_handshake *server = nxp_handshake_create(true);

    nxp_transport_params client_tp = {
        .initial_max_data = 2 * 1024 * 1024,
        .initial_max_stream_data = 512 * 1024,
        .max_streams_bidi = 100,
        .max_streams_uni = 50,
        .idle_timeout_us = 60000000,
    };
    nxp_transport_params server_tp = {
        .initial_max_data = 4 * 1024 * 1024,
        .initial_max_stream_data = 1024 * 1024,
        .max_streams_bidi = 200,
        .max_streams_uni = 100,
        .idle_timeout_us = 120000000,
    };
    nxp_handshake_set_local_params(client, &client_tp);
    nxp_handshake_set_local_params(server, &server_tp);

    nxp_conn_id dcid = { .len = 8 };
    dcid.data[0] = 0x55;

    NXP_ASSERT_OK(nxp_handshake_start_client(client, &dcid));
    NXP_ASSERT_OK(nxp_handshake_start_server(server, &dcid));

    /* Exchange messages */
    uint8_t buf[512];
    uint64_t off;
    size_t len = nxp_handshake_fill_crypto(client, buf, sizeof(buf), &off);
    NXP_ASSERT_OK(nxp_handshake_recv_crypto(server, NXP_CRYPTO_INITIAL, buf, len));

    len = nxp_handshake_fill_crypto(server, buf, sizeof(buf), &off);
    NXP_ASSERT_OK(nxp_handshake_recv_crypto(client, NXP_CRYPTO_INITIAL, buf, len));

    /* Verify transport params were exchanged */
    NXP_ASSERT(server->has_peer_params);
    NXP_ASSERT_EQ(server->peer_params.max_streams_bidi, (uint32_t)100);
    NXP_ASSERT_EQ(server->peer_params.max_streams_uni, (uint32_t)50);

    NXP_ASSERT(client->has_peer_params);
    NXP_ASSERT_EQ(client->peer_params.max_streams_bidi, (uint32_t)200);
    NXP_ASSERT_EQ(client->peer_params.max_streams_uni, (uint32_t)100);

    nxp_handshake_destroy(client);
    nxp_handshake_destroy(server);
}

/* ── Test: Phase 4 Tests Still Work (Plaintext) ──────── */

NXP_TEST(hs_phase4_compat) {
    /* Ensure Phase 4 plaintext mode still works */
    nxp_conn_config cfg = make_config(0x01);
    nxp_conn *conn = nxp_conn_create(&cfg, false);
    NXP_ASSERT_NOT_NULL(conn);
    NXP_ASSERT(!conn->crypto.available);

    nxp_conn_id dcid = { .len = 8 };
    nxp_conn_set_established(conn, &dcid);
    NXP_ASSERT_EQ((int)nxp_conn_get_state(conn), (int)NXP_CONN_ESTABLISHED);

    /* Should still work without crypto */
    uint64_t sid;
    NXP_ASSERT_OK(nxp_conn_open_stream(conn, &sid, NXP_STREAM_RELIABLE, false));

    const uint8_t msg[] = "plaintext works";
    (void)nxp_conn_stream_send(conn, sid, msg, sizeof(msg) - 1, false);

    uint8_t pkt[1500];
    ssize_t n = nxp_conn_send(conn, pkt, sizeof(pkt), 1000);
    NXP_ASSERT(n > 0);

    nxp_conn_destroy(conn);
}

/* ── Main ──────────────────────────────────────────────── */

int main(void) {
    printf("=== NXP Handshake Tests (Phase 5) ===\n");

    NXP_RUN_TEST(hs_create_destroy);
    NXP_RUN_TEST(hs_client_start);
    NXP_RUN_TEST(hs_full_exchange);
    NXP_RUN_TEST(hs_encrypted_round_trip);
    NXP_RUN_TEST(hs_transport_params);
    NXP_RUN_TEST(hs_phase4_compat);

    NXP_TEST_SUMMARY();
}
