/*
 * NXP Connection Migration Tests
 *
 * Phase 6: Tests for CID pool management, path validation
 * (PATH_CHALLENGE/RESPONSE), and address change detection.
 */
#include "test_framework.h"
#include "core/migration_internal.h"
#include "core/frame_internal.h"
#include <string.h>

/* ── Test: Migration Init ────────────────────────────── */

NXP_TEST(migration_init) {
    nxp_migration_state ms;
    nxp_migration_init(&ms);

    NXP_ASSERT(ms.enabled);
    NXP_ASSERT_EQ((int)ms.peer_cid_count, 0);
    NXP_ASSERT_EQ((int)ms.migration_in_progress, 0);
    NXP_ASSERT_EQ((int)ms.send_path_challenge, 0);
    NXP_ASSERT_EQ((int)ms.send_path_response, 0);
    NXP_ASSERT_EQ(ms.local_cid_next_seq, (uint64_t)1);
    NXP_ASSERT_EQ(ms.active_cid_limit, (uint32_t)NXP_DEFAULT_ACTIVE_CID_LIMIT);
}

/* ── Test: Path Validation Success ───────────────────── */

NXP_TEST(path_validation_success) {
    nxp_migration_state ms;
    nxp_migration_init(&ms);

    /* Simulate address change */
    nxp_addr new_addr;
    memset(&new_addr, 0, sizeof(new_addr));
    new_addr.ip.v4[0] = 192;
    new_addr.ip.v4[1] = 168;
    new_addr.ip.v4[2] = 2;
    new_addr.ip.v4[3] = 1;

    NXP_ASSERT_OK(nxp_migration_on_peer_addr_change(&ms, &new_addr, 1000));
    NXP_ASSERT(ms.migration_in_progress);
    NXP_ASSERT(ms.send_path_challenge);
    NXP_ASSERT(ms.new_path.challenge_pending);

    /* Save the challenge data */
    uint8_t challenge[8];
    memcpy(challenge, ms.new_path.challenge_data, 8);

    /* Respond with matching data */
    NXP_ASSERT(nxp_migration_on_path_response(&ms, challenge));

    /* Path should be validated */
    NXP_ASSERT(!ms.migration_in_progress);
    NXP_ASSERT(ms.current_path.validated);
    NXP_ASSERT(memcmp(ms.current_path.addr.ip.v4, new_addr.ip.v4, 4) == 0);
}

/* ── Test: Path Validation Wrong Response ────────────── */

NXP_TEST(path_validation_wrong_response) {
    nxp_migration_state ms;
    nxp_migration_init(&ms);

    nxp_addr new_addr;
    memset(&new_addr, 0, sizeof(new_addr));
    new_addr.ip.v4[0] = 10;

    NXP_ASSERT_OK(nxp_migration_on_peer_addr_change(&ms, &new_addr, 1000));

    /* Wrong response data */
    uint8_t wrong_data[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    NXP_ASSERT(!nxp_migration_on_path_response(&ms, wrong_data));

    /* Migration should still be in progress */
    NXP_ASSERT(ms.migration_in_progress);
    NXP_ASSERT(ms.new_path.challenge_pending);
}

/* ── Test: Path Challenge Echo ───────────────────────── */

NXP_TEST(path_challenge_echo) {
    nxp_migration_state ms;
    nxp_migration_init(&ms);

    /* Receive a PATH_CHALLENGE from peer */
    uint8_t challenge[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    nxp_migration_on_path_challenge(&ms, challenge);

    NXP_ASSERT(ms.send_path_response);
    NXP_ASSERT(memcmp(ms.pending_response_data, challenge, 8) == 0);
}

/* ── Test: CID Pool Management ───────────────────────── */

NXP_TEST(cid_pool_management) {
    nxp_migration_state ms;
    nxp_migration_init(&ms);

    /* Add some CIDs from peer */
    nxp_conn_id cid1;
    memset(&cid1, 0, sizeof(cid1));
    cid1.data[0] = 0x11;
    cid1.len = 8;
    uint8_t token1[16] = {0};

    nxp_conn_id cid2;
    memset(&cid2, 0, sizeof(cid2));
    cid2.data[0] = 0x22;
    cid2.len = 8;
    uint8_t token2[16] = {0};

    NXP_ASSERT_OK(nxp_migration_on_new_connection_id(
        &ms, &cid1, 1, 0, token1));
    NXP_ASSERT_OK(nxp_migration_on_new_connection_id(
        &ms, &cid2, 2, 0, token2));

    NXP_ASSERT_EQ(ms.peer_cid_count, (uint32_t)2);

    /* Get unused CID */
    const nxp_cid_entry *entry = nxp_migration_get_unused_cid(&ms);
    NXP_ASSERT_NOT_NULL(entry);
    NXP_ASSERT_EQ(entry->cid.data[0], (uint8_t)0x11);

    /* Use it */
    nxp_migration_use_cid(&ms, entry->seq_num);

    /* Next unused should be the second one */
    entry = nxp_migration_get_unused_cid(&ms);
    NXP_ASSERT_NOT_NULL(entry);
    NXP_ASSERT_EQ(entry->cid.data[0], (uint8_t)0x22);
}

/* ── Test: CID Pool Retire Prior To ──────────────────── */

NXP_TEST(cid_retire_prior_to) {
    nxp_migration_state ms;
    nxp_migration_init(&ms);

    nxp_conn_id cid1, cid2, cid3;
    memset(&cid1, 0, sizeof(cid1)); cid1.data[0] = 0x01; cid1.len = 8;
    memset(&cid2, 0, sizeof(cid2)); cid2.data[0] = 0x02; cid2.len = 8;
    memset(&cid3, 0, sizeof(cid3)); cid3.data[0] = 0x03; cid3.len = 8;
    uint8_t token[16] = {0};

    NXP_ASSERT_OK(nxp_migration_on_new_connection_id(&ms, &cid1, 1, 0, token));
    NXP_ASSERT_OK(nxp_migration_on_new_connection_id(&ms, &cid2, 2, 0, token));

    /* Add cid3 with retire_prior_to = 2 (retires cid1) */
    NXP_ASSERT_OK(nxp_migration_on_new_connection_id(&ms, &cid3, 3, 2, token));

    /* cid1 should be retired */
    NXP_ASSERT(ms.peer_cid_pool[0].retired);

    /* Get unused should give cid2 or cid3 (not cid1) */
    const nxp_cid_entry *entry = nxp_migration_get_unused_cid(&ms);
    NXP_ASSERT_NOT_NULL(entry);
    NXP_ASSERT(entry->cid.data[0] != 0x01); /* Not the retired one */
}

/* ── Test: Generate NEW_CONNECTION_ID ────────────────── */

NXP_TEST(generate_new_cids) {
    nxp_migration_state ms;
    nxp_migration_init(&ms);

    nxp_conn_id base;
    memset(&base, 0, sizeof(base));
    base.data[0] = 0xAA;
    base.len = 8;

    uint32_t count = nxp_migration_generate_cids(&ms, &base);
    NXP_ASSERT_EQ(count, (uint32_t)NXP_DEFAULT_ACTIVE_CID_LIMIT);
    NXP_ASSERT_EQ(ms.pending_new_cid_count, (uint32_t)NXP_DEFAULT_ACTIVE_CID_LIMIT);

    /* Each generated CID should have correct length */
    for (uint32_t i = 0; i < ms.pending_new_cid_count; i++) {
        NXP_ASSERT_EQ(ms.pending_new_cids[i].cid.len, base.len);
        NXP_ASSERT(ms.pending_new_cids[i].seq_num >= 1);
    }

    /* Generating more shouldn't exceed limit */
    count = nxp_migration_generate_cids(&ms, &base);
    NXP_ASSERT_EQ(count, (uint32_t)0);
}

/* ── Test: Path Validation Timeout ───────────────────── */

NXP_TEST(path_validation_timeout) {
    nxp_migration_state ms;
    nxp_migration_init(&ms);

    nxp_addr new_addr;
    memset(&new_addr, 0, sizeof(new_addr));
    new_addr.ip.v4[0] = 172;

    NXP_ASSERT_OK(nxp_migration_on_peer_addr_change(&ms, &new_addr, 1000));

    /* Not timed out yet */
    uint64_t pto = 100000; /* 100ms */
    NXP_ASSERT(!nxp_migration_check_timeout(&ms, 100000, pto));

    /* Should time out after 3x PTO */
    NXP_ASSERT(nxp_migration_check_timeout(&ms, 1000 + 3 * pto + 1, pto));
    NXP_ASSERT(!ms.migration_in_progress);
}

/* ── Test: Migration Has Pending ─────────────────────── */

NXP_TEST(migration_has_pending) {
    nxp_migration_state ms;
    nxp_migration_init(&ms);

    NXP_ASSERT(!nxp_migration_has_pending(&ms));

    /* Add a path challenge */
    uint8_t challenge[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    nxp_migration_on_path_challenge(&ms, challenge);
    NXP_ASSERT(nxp_migration_has_pending(&ms));

    ms.send_path_response = false;
    NXP_ASSERT(!nxp_migration_has_pending(&ms));
}

/* ── Test: Frame Encode/Decode PATH_CHALLENGE ────────── */

NXP_TEST(frame_path_challenge_round_trip) {
    uint8_t data[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
    uint8_t buf[32];

    size_t encoded = nxp_frame_encode_path_challenge(data, buf, sizeof(buf));
    NXP_ASSERT(encoded > 0);

    nxp_frame frame;
    size_t decoded = nxp_frame_decode(buf, encoded, &frame);
    NXP_ASSERT(decoded > 0);
    NXP_ASSERT_EQ((int)frame.type, (int)NXP_FRAME_PATH_CHALLENGE);
    NXP_ASSERT(memcmp(frame.path_challenge.data, data, 8) == 0);
}

/* ── Test: Frame Encode/Decode NEW_CONNECTION_ID ─────── */

NXP_TEST(frame_new_connection_id_round_trip) {
    nxp_frame_new_connection_id ncid = {
        .seq_num = 42,
        .retire_prior_to = 10,
        .cid = { .data = {0xAA, 0xBB, 0xCC}, .len = 3 },
    };
    memset(ncid.stateless_reset_token, 0xFF, 16);

    uint8_t buf[128];
    size_t encoded = nxp_frame_encode_new_connection_id(&ncid, buf, sizeof(buf));
    NXP_ASSERT(encoded > 0);

    nxp_frame frame;
    size_t decoded = nxp_frame_decode(buf, encoded, &frame);
    NXP_ASSERT(decoded > 0);
    NXP_ASSERT_EQ((int)frame.type, (int)NXP_FRAME_NEW_CONNECTION_ID);
    NXP_ASSERT_EQ(frame.new_connection_id.seq_num, (uint64_t)42);
    NXP_ASSERT_EQ(frame.new_connection_id.retire_prior_to, (uint64_t)10);
    NXP_ASSERT_EQ(frame.new_connection_id.cid.len, (uint8_t)3);
}

/* ── main ─────────────────────────────────────────────── */

int main(void) {
    printf("=== NXP Connection Migration Tests ===\n\n");

    NXP_RUN_TEST(migration_init);
    NXP_RUN_TEST(path_validation_success);
    NXP_RUN_TEST(path_validation_wrong_response);
    NXP_RUN_TEST(path_challenge_echo);
    NXP_RUN_TEST(cid_pool_management);
    NXP_RUN_TEST(cid_retire_prior_to);
    NXP_RUN_TEST(generate_new_cids);
    NXP_RUN_TEST(path_validation_timeout);
    NXP_RUN_TEST(migration_has_pending);
    NXP_RUN_TEST(frame_path_challenge_round_trip);
    NXP_RUN_TEST(frame_new_connection_id_round_trip);

    NXP_TEST_SUMMARY();
}
