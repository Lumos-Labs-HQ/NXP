/*
 * NXP Session Ticket + Retry Token Tests
 *
 * Phase 6: Tests for session ticket create/validate, 0-RTT key
 * derivation, replay protection, and retry token create/validate.
 */
#include "test_framework.h"
#include "crypto/crypto_internal.h"
#include "core/handshake_internal.h"
#include "nxp/nxp_types.h"
#include <string.h>

/* ── Test: Session Ticket Round-Trip ─────────────────── */

NXP_TEST(session_ticket_round_trip) {
    /* Set up a server key and resumption secret */
    uint8_t server_key[32];
    memset(server_key, 0xAB, sizeof(server_key));

    uint8_t resumption_secret[NXP_HASH_LEN];
    memset(resumption_secret, 0xCD, sizeof(resumption_secret));

    uint8_t tp_data[] = {0x01, 0x02, 0x03, 0x04};
    uint64_t now_us = 1000000; /* 1 second */

    /* Create ticket */
    uint8_t ticket[512];
    size_t ticket_len = 0;
    NXP_ASSERT_OK(nxp_session_ticket_create(
        server_key, resumption_secret, NXP_AEAD_AES_256_GCM,
        tp_data, sizeof(tp_data), now_us,
        ticket, sizeof(ticket), &ticket_len));
    NXP_ASSERT(ticket_len > 0);

    /* Validate ticket */
    uint8_t out_secret[NXP_HASH_LEN];
    nxp_aead_algo out_algo;
    uint8_t out_tp[256];
    size_t out_tp_len = 0;

    NXP_ASSERT_OK(nxp_session_ticket_validate(
        server_key, ticket, ticket_len, now_us + 100,
        out_secret, &out_algo, out_tp, sizeof(out_tp), &out_tp_len));

    /* Check outputs match */
    NXP_ASSERT(memcmp(out_secret, resumption_secret, NXP_HASH_LEN) == 0);
    NXP_ASSERT_EQ((int)out_algo, (int)NXP_AEAD_AES_256_GCM);
    NXP_ASSERT_EQ((int)out_tp_len, (int)sizeof(tp_data));
    NXP_ASSERT(memcmp(out_tp, tp_data, sizeof(tp_data)) == 0);
}

/* ── Test: Session Ticket Expiry ─────────────────────── */

NXP_TEST(session_ticket_expiry) {
    uint8_t server_key[32];
    memset(server_key, 0x11, sizeof(server_key));

    uint8_t resumption_secret[NXP_HASH_LEN];
    memset(resumption_secret, 0x22, sizeof(resumption_secret));

    uint64_t now_us = 1000000;

    uint8_t ticket[512];
    size_t ticket_len = 0;
    NXP_ASSERT_OK(nxp_session_ticket_create(
        server_key, resumption_secret, NXP_AEAD_AES_256_GCM,
        nullptr, 0, now_us,
        ticket, sizeof(ticket), &ticket_len));

    /* Validate with time far in the future (ticket should be expired) */
    uint8_t out_secret[NXP_HASH_LEN];
    nxp_aead_algo out_algo;
    uint64_t expired_time = now_us + 3601ULL * 1000000ULL; /* > 1 hour */

    nxp_result r = nxp_session_ticket_validate(
        server_key, ticket, ticket_len, expired_time,
        out_secret, &out_algo, nullptr, 0, nullptr);
    NXP_ASSERT_EQ(r.code, NXP_ERR_TOKEN_INVALID);
}

/* ── Test: Session Ticket Wrong Key ──────────────────── */

NXP_TEST(session_ticket_wrong_key) {
    uint8_t server_key[32];
    memset(server_key, 0x33, sizeof(server_key));

    uint8_t wrong_key[32];
    memset(wrong_key, 0x44, sizeof(wrong_key));

    uint8_t resumption_secret[NXP_HASH_LEN];
    memset(resumption_secret, 0x55, sizeof(resumption_secret));

    uint8_t ticket[512];
    size_t ticket_len = 0;
    NXP_ASSERT_OK(nxp_session_ticket_create(
        server_key, resumption_secret, NXP_AEAD_AES_256_GCM,
        nullptr, 0, 1000000,
        ticket, sizeof(ticket), &ticket_len));

    /* Try to validate with wrong key */
    uint8_t out_secret[NXP_HASH_LEN];
    nxp_aead_algo out_algo;
    nxp_result r = nxp_session_ticket_validate(
        wrong_key, ticket, ticket_len, 1000100,
        out_secret, &out_algo, nullptr, 0, nullptr);
    NXP_ASSERT_EQ(r.code, NXP_ERR_TOKEN_INVALID);
}

/* ── Test: 0-RTT Key Derivation ──────────────────────── */

NXP_TEST(zero_rtt_key_derivation) {
    uint8_t resumption_secret[NXP_HASH_LEN];
    memset(resumption_secret, 0xEE, sizeof(resumption_secret));

    nxp_crypto_state zero_rtt;
    NXP_ASSERT(nxp_crypto_derive_zero_rtt_keys(
        resumption_secret, NXP_AEAD_AES_256_GCM, &zero_rtt));

    NXP_ASSERT(zero_rtt.available);
    NXP_ASSERT_EQ((int)zero_rtt.algo, (int)NXP_AEAD_AES_256_GCM);
    NXP_ASSERT_EQ((int)zero_rtt.send.key_len, 32);

    /* send and recv should be the same (both from client_early_secret) */
    NXP_ASSERT(memcmp(zero_rtt.send.key, zero_rtt.recv.key, 32) == 0);
    NXP_ASSERT(memcmp(zero_rtt.send.iv, zero_rtt.recv.iv, NXP_AEAD_IV_LEN) == 0);

    /* Keys should not be all-zero */
    uint8_t zeros[32] = {0};
    NXP_ASSERT(memcmp(zero_rtt.send.key, zeros, 32) != 0);
}

/* ── Test: 0-RTT Encrypt/Decrypt ─────────────────────── */

NXP_TEST(zero_rtt_encrypt_decrypt) {
    uint8_t resumption_secret[NXP_HASH_LEN];
    memset(resumption_secret, 0xAA, sizeof(resumption_secret));

    nxp_crypto_state zero_rtt;
    NXP_ASSERT(nxp_crypto_derive_zero_rtt_keys(
        resumption_secret, NXP_AEAD_AES_256_GCM, &zero_rtt));

    /* Encrypt some data */
    uint8_t pt[] = "Hello 0-RTT early data!";
    uint8_t ct[128];
    uint8_t nonce[NXP_AEAD_IV_LEN];
    nxp_crypto_make_nonce(zero_rtt.send.iv, 0, nonce);

    ssize_t ct_len = nxp_aead_encrypt(zero_rtt.algo,
        zero_rtt.send.key, zero_rtt.send.key_len,
        nonce, nullptr, 0, pt, sizeof(pt), ct);
    NXP_ASSERT(ct_len > 0);

    /* Decrypt (server uses recv keys, which are same as send for 0-RTT) */
    uint8_t decrypted[128];
    ssize_t pt_len = nxp_aead_decrypt(zero_rtt.algo,
        zero_rtt.recv.key, zero_rtt.recv.key_len,
        nonce, nullptr, 0, ct, (size_t)ct_len, decrypted);
    NXP_ASSERT(pt_len > 0);
    NXP_ASSERT_EQ((size_t)pt_len, sizeof(pt));
    NXP_ASSERT(memcmp(decrypted, pt, sizeof(pt)) == 0);
}

/* ── Test: Resumption Secret Derivation ──────────────── */

NXP_TEST(resumption_secret_derivation) {
    uint8_t master_secret[NXP_HASH_LEN];
    memset(master_secret, 0xBB, sizeof(master_secret));

    uint8_t transcript[] = "fake transcript data for testing";
    uint8_t resumption[NXP_HASH_LEN];

    NXP_ASSERT(nxp_crypto_derive_resumption_secret(
        master_secret, transcript, sizeof(transcript), resumption));

    /* Should be deterministic */
    uint8_t resumption2[NXP_HASH_LEN];
    NXP_ASSERT(nxp_crypto_derive_resumption_secret(
        master_secret, transcript, sizeof(transcript), resumption2));
    NXP_ASSERT(memcmp(resumption, resumption2, NXP_HASH_LEN) == 0);

    /* Different inputs should produce different output */
    uint8_t different_master[NXP_HASH_LEN];
    memset(different_master, 0xCC, sizeof(different_master));
    uint8_t resumption3[NXP_HASH_LEN];
    NXP_ASSERT(nxp_crypto_derive_resumption_secret(
        different_master, transcript, sizeof(transcript), resumption3));
    NXP_ASSERT(memcmp(resumption, resumption3, NXP_HASH_LEN) != 0);
}

/* ── Test: Strike Register (Replay Protection) ───────── */

NXP_TEST(strike_register) {
    nxp_strike_register *reg = nxp_strike_register_create();
    NXP_ASSERT_NOT_NULL(reg);

    uint8_t nonce1[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    uint8_t nonce2[] = {12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};

    /* First use should succeed */
    NXP_ASSERT(nxp_strike_register_check_and_add(reg, nonce1, sizeof(nonce1), 1000));

    /* Second use of same nonce should fail (replay) */
    NXP_ASSERT(!nxp_strike_register_check_and_add(reg, nonce1, sizeof(nonce1), 1001));

    /* Different nonce should succeed */
    NXP_ASSERT(nxp_strike_register_check_and_add(reg, nonce2, sizeof(nonce2), 1002));

    nxp_strike_register_destroy(reg);
}

/* ── Test: Retry Token Round-Trip ────────────────────── */

NXP_TEST(retry_token_round_trip) {
    uint8_t server_key[32];
    memset(server_key, 0x77, sizeof(server_key));

    nxp_addr client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.family = 2; /* AF_INET */
    client_addr.port = 12345;
    client_addr.ip.v4[0] = 192;
    client_addr.ip.v4[1] = 168;
    client_addr.ip.v4[2] = 1;
    client_addr.ip.v4[3] = 100;

    nxp_conn_id dcid;
    memset(&dcid, 0, sizeof(dcid));
    dcid.data[0] = 0xAA;
    dcid.data[1] = 0xBB;
    dcid.len = 8;

    uint64_t now_us = 5000000;

    /* Create token */
    uint8_t token[256];
    size_t token_len = 0;
    NXP_ASSERT_OK(nxp_retry_token_create(
        server_key, &client_addr, &dcid, now_us,
        token, sizeof(token), &token_len));
    NXP_ASSERT(token_len > 0);

    /* Validate token */
    NXP_ASSERT_OK(nxp_retry_token_validate(
        server_key, &client_addr, &dcid, token, token_len, now_us + 1000));
}

/* ── Test: Retry Token Expiry ────────────────────────── */

NXP_TEST(retry_token_expiry) {
    uint8_t server_key[32];
    memset(server_key, 0x88, sizeof(server_key));

    nxp_addr addr;
    memset(&addr, 0, sizeof(addr));
    nxp_conn_id dcid;
    memset(&dcid, 0, sizeof(dcid));
    dcid.len = 4;

    uint64_t now_us = 1000000;

    uint8_t token[256];
    size_t token_len = 0;
    NXP_ASSERT_OK(nxp_retry_token_create(
        server_key, &addr, &dcid, now_us,
        token, sizeof(token), &token_len));

    /* Validate after expiry (>10 seconds) */
    uint64_t expired = now_us + 11ULL * 1000000ULL;
    nxp_result r = nxp_retry_token_validate(
        server_key, &addr, &dcid, token, token_len, expired);
    NXP_ASSERT_EQ(r.code, NXP_ERR_TOKEN_INVALID);
}

/* ── Test: Retry Token Wrong Address ─────────────────── */

NXP_TEST(retry_token_wrong_address) {
    uint8_t server_key[32];
    memset(server_key, 0x99, sizeof(server_key));

    nxp_addr addr1;
    memset(&addr1, 0, sizeof(addr1));
    addr1.ip.v4[0] = 10;
    addr1.ip.v4[1] = 0;
    addr1.ip.v4[2] = 0;
    addr1.ip.v4[3] = 1;

    nxp_addr addr2;
    memset(&addr2, 0, sizeof(addr2));
    addr2.ip.v4[0] = 10;
    addr2.ip.v4[1] = 0;
    addr2.ip.v4[2] = 0;
    addr2.ip.v4[3] = 2;

    nxp_conn_id dcid;
    memset(&dcid, 0, sizeof(dcid));
    dcid.len = 4;

    uint8_t token[256];
    size_t token_len = 0;
    NXP_ASSERT_OK(nxp_retry_token_create(
        server_key, &addr1, &dcid, 1000000,
        token, sizeof(token), &token_len));

    /* Validate with different address */
    nxp_result r = nxp_retry_token_validate(
        server_key, &addr2, &dcid, token, token_len, 1000100);
    NXP_ASSERT_EQ(r.code, NXP_ERR_TOKEN_INVALID);
}

/* ── Test: Handshake Produces Resumption Secret ──────── */

NXP_TEST(handshake_resumption_secret) {
    /* Do a full handshake and check that resumption_secret is populated */
    /* We use the handshake API directly */
    nxp_handshake *client = nxp_handshake_create(false);
    NXP_ASSERT_NOT_NULL(client);
    nxp_handshake *server = nxp_handshake_create(true);
    NXP_ASSERT_NOT_NULL(server);

    nxp_transport_params tp = {
        .initial_max_data = 1048576,
        .initial_max_stream_data = 262144,
        .max_streams_bidi = 100,
        .max_streams_uni = 50,
        .idle_timeout_us = 30000000,
    };
    nxp_handshake_set_local_params(client, &tp);
    nxp_handshake_set_local_params(server, &tp);

    nxp_conn_id server_cid;
    memset(&server_cid, 0, sizeof(server_cid));
    server_cid.data[0] = 0xBB;
    server_cid.len = 8;

    NXP_ASSERT_OK(nxp_handshake_start_client(client, &server_cid));
    NXP_ASSERT_OK(nxp_handshake_start_server(server, &server_cid));

    /* ClientHello -> Server */
    uint8_t buf[512];
    uint64_t off;
    size_t len = nxp_handshake_fill_crypto(client, buf, sizeof(buf), &off);
    NXP_ASSERT(len > 0);
    NXP_ASSERT_OK(nxp_handshake_recv_crypto(server, NXP_CRYPTO_INITIAL, buf, len));

    /* ServerHello -> Client */
    len = nxp_handshake_fill_crypto(server, buf, sizeof(buf), &off);
    NXP_ASSERT(len > 0);
    NXP_ASSERT_OK(nxp_handshake_recv_crypto(client, NXP_CRYPTO_INITIAL, buf, len));

    /* HANDSHAKE_DONE -> Client */
    NXP_ASSERT_OK(nxp_handshake_on_handshake_done(client));

    /* Both should have resumption secrets */
    NXP_ASSERT(server->has_resumption_secret);
    NXP_ASSERT(client->has_resumption_secret);

    /* Resumption secrets should match (derived from same transcript + master) */
    NXP_ASSERT(memcmp(server->resumption_secret,
                       client->resumption_secret, NXP_HASH_LEN) == 0);

    /* They should not be all zeros */
    uint8_t zeros[NXP_HASH_LEN] = {0};
    NXP_ASSERT(memcmp(server->resumption_secret, zeros, NXP_HASH_LEN) != 0);

    nxp_handshake_destroy(client);
    nxp_handshake_destroy(server);
}

/* ── main ─────────────────────────────────────────────── */

int main(void) {
    printf("=== NXP Session Ticket + Retry Token Tests ===\n\n");

    NXP_RUN_TEST(session_ticket_round_trip);
    NXP_RUN_TEST(session_ticket_expiry);
    NXP_RUN_TEST(session_ticket_wrong_key);
    NXP_RUN_TEST(zero_rtt_key_derivation);
    NXP_RUN_TEST(zero_rtt_encrypt_decrypt);
    NXP_RUN_TEST(resumption_secret_derivation);
    NXP_RUN_TEST(strike_register);
    NXP_RUN_TEST(retry_token_round_trip);
    NXP_RUN_TEST(retry_token_expiry);
    NXP_RUN_TEST(retry_token_wrong_address);
    NXP_RUN_TEST(handshake_resumption_secret);

    NXP_TEST_SUMMARY();
}
