/*
 * Unit tests: Stream management
 *
 * Phase 4: Tests stream create/destroy, write/read, fill_frame,
 * on_recv, on_ack, on_loss, and circular buffer behavior.
 */
#include "test_framework.h"
#include "connection_internal.h"
#include <string.h>

/* ── Test: Create and Destroy ─────────────────────────── */

NXP_TEST(stream_create_destroy) {
    nxp_stream_s *s = nxp_stream_create(0, NXP_STREAM_RELIABLE,
                                          NXP_DEFAULT_MAX_STREAM_DATA,
                                          NXP_DEFAULT_MAX_STREAM_DATA);
    NXP_ASSERT_NOT_NULL(s);
    NXP_ASSERT_EQ(s->id, (uint64_t)0);
    NXP_ASSERT_EQ(s->state, NXP_STREAM_OPEN);
    NXP_ASSERT_EQ(s->priority, (uint8_t)128);
    NXP_ASSERT_NOT_NULL(s->send.data);
    NXP_ASSERT_NOT_NULL(s->recv.data);
    NXP_ASSERT(!s->scheduled);

    nxp_stream_destroy(s);
}

/* ── Test: Write and Read Basic ───────────────────────── */

NXP_TEST(stream_write_read) {
    nxp_stream_s *s = nxp_stream_create(0, NXP_STREAM_RELIABLE,
                                          NXP_DEFAULT_MAX_STREAM_DATA,
                                          NXP_DEFAULT_MAX_STREAM_DATA);
    NXP_ASSERT_NOT_NULL(s);

    /* Write data to send buffer */
    const uint8_t data[] = "Hello, NXP streams!";
    ssize_t written = nxp_stream_write(s, data, sizeof(data) - 1, false);
    NXP_ASSERT_EQ((size_t)written, sizeof(data) - 1);

    /* Check unsent data */
    NXP_ASSERT_EQ(nxp_stream_unsent(s), sizeof(data) - 1);

    /* Simulate receiving data into recv buffer */
    nxp_frame_stream f = {
        .stream_id  = 0,
        .offset     = 0,
        .length     = sizeof(data) - 1,
        .has_offset = false,
        .has_length = true,
        .fin        = false,
        .data       = data,
    };
    NXP_ASSERT_OK(nxp_stream_on_recv(s, &f));

    /* Read back */
    uint8_t buf[64];
    bool fin = false;
    ssize_t nread = nxp_stream_read(s, buf, sizeof(buf), &fin);
    NXP_ASSERT_EQ((size_t)nread, sizeof(data) - 1);
    NXP_ASSERT(!fin);
    NXP_ASSERT(memcmp(buf, data, sizeof(data) - 1) == 0);

    nxp_stream_destroy(s);
}

/* ── Test: Write with FIN ─────────────────────────────── */

NXP_TEST(stream_write_fin) {
    nxp_stream_s *s = nxp_stream_create(0, NXP_STREAM_RELIABLE,
                                          NXP_DEFAULT_MAX_STREAM_DATA,
                                          NXP_DEFAULT_MAX_STREAM_DATA);
    NXP_ASSERT_NOT_NULL(s);

    const uint8_t data[] = "Final message";
    ssize_t written = nxp_stream_write(s, data, sizeof(data) - 1, true);
    NXP_ASSERT_EQ((size_t)written, sizeof(data) - 1);
    NXP_ASSERT(s->send.fin);

    /* Can't write after FIN */
    ssize_t w2 = nxp_stream_write(s, data, 1, false);
    NXP_ASSERT_EQ(w2, (ssize_t)-1);

    nxp_stream_destroy(s);
}

/* ── Test: Fill Frame ─────────────────────────────────── */

NXP_TEST(stream_fill_frame) {
    nxp_stream_s *s = nxp_stream_create(42, NXP_STREAM_RELIABLE,
                                           NXP_DEFAULT_MAX_STREAM_DATA,
                                           NXP_DEFAULT_MAX_STREAM_DATA);
    NXP_ASSERT_NOT_NULL(s);

    const uint8_t data[] = "Test payload for frame filling";
    (void)nxp_stream_write(s, data, sizeof(data) - 1, false);

    nxp_frame_stream out;
    bool ok = nxp_stream_fill_frame(s, &out, 1024);
    NXP_ASSERT(ok);
    NXP_ASSERT_EQ(out.stream_id, (uint64_t)42);
    NXP_ASSERT_EQ(out.offset, (uint64_t)0);
    NXP_ASSERT_EQ(out.length, sizeof(data) - 1);
    NXP_ASSERT(!out.fin);
    NXP_ASSERT(out.data != nullptr);
    NXP_ASSERT(memcmp(out.data, data, sizeof(data) - 1) == 0);

    /* No more unsent data */
    NXP_ASSERT_EQ(nxp_stream_unsent(s), (uint64_t)0);

    /* No more frames */
    NXP_ASSERT(!nxp_stream_fill_frame(s, &out, 1024));

    nxp_stream_destroy(s);
}

/* ── Test: Fill Frame with FIN ────────────────────────── */

NXP_TEST(stream_fill_frame_fin) {
    nxp_stream_s *s = nxp_stream_create(0, NXP_STREAM_RELIABLE,
                                          NXP_DEFAULT_MAX_STREAM_DATA,
                                          NXP_DEFAULT_MAX_STREAM_DATA);
    NXP_ASSERT_NOT_NULL(s);

    const uint8_t data[] = "bye";
    (void)nxp_stream_write(s, data, 3, true);

    nxp_frame_stream out;
    bool ok = nxp_stream_fill_frame(s, &out, 1024);
    NXP_ASSERT(ok);
    NXP_ASSERT_EQ(out.length, (uint64_t)3);
    NXP_ASSERT(out.fin);
    NXP_ASSERT(s->send.fin_sent);

    nxp_stream_destroy(s);
}

/* ── Test: Partial Fill (limited by max_data_len) ─────── */

NXP_TEST(stream_fill_frame_partial) {
    nxp_stream_s *s = nxp_stream_create(0, NXP_STREAM_RELIABLE,
                                          NXP_DEFAULT_MAX_STREAM_DATA,
                                          NXP_DEFAULT_MAX_STREAM_DATA);
    NXP_ASSERT_NOT_NULL(s);

    uint8_t data[200];
    memset(data, 0xAA, sizeof(data));
    (void)nxp_stream_write(s, data, sizeof(data), false);

    /* Only allow 50 bytes per frame */
    nxp_frame_stream out;
    bool ok = nxp_stream_fill_frame(s, &out, 50);
    NXP_ASSERT(ok);
    NXP_ASSERT_EQ(out.length, (uint64_t)50);
    NXP_ASSERT_EQ(out.offset, (uint64_t)0);

    /* Second frame should start at offset 50 */
    ok = nxp_stream_fill_frame(s, &out, 50);
    NXP_ASSERT(ok);
    NXP_ASSERT_EQ(out.offset, (uint64_t)50);
    NXP_ASSERT_EQ(out.length, (uint64_t)50);

    /* Still 100 bytes unsent */
    NXP_ASSERT_EQ(nxp_stream_unsent(s), (uint64_t)100);

    nxp_stream_destroy(s);
}

/* ── Test: Recv with FIN ──────────────────────────────── */

NXP_TEST(stream_recv_fin) {
    nxp_stream_s *s = nxp_stream_create(0, NXP_STREAM_RELIABLE,
                                          NXP_DEFAULT_MAX_STREAM_DATA,
                                          NXP_DEFAULT_MAX_STREAM_DATA);
    NXP_ASSERT_NOT_NULL(s);

    const uint8_t data[] = "final";
    nxp_frame_stream f = {
        .stream_id  = 0,
        .offset     = 0,
        .length     = 5,
        .has_offset = false,
        .has_length = true,
        .fin        = true,
        .data       = data,
    };
    NXP_ASSERT_OK(nxp_stream_on_recv(s, &f));
    NXP_ASSERT(s->recv.fin_received);
    NXP_ASSERT_EQ(s->recv.fin_offset, (uint64_t)5);

    /* Read and check FIN */
    uint8_t buf[64];
    bool fin = false;
    ssize_t nread = nxp_stream_read(s, buf, sizeof(buf), &fin);
    NXP_ASSERT_EQ((size_t)nread, (size_t)5);
    NXP_ASSERT(fin);

    nxp_stream_destroy(s);
}

/* ── Test: ACK advances acked_offset ──────────────────── */

NXP_TEST(stream_on_ack) {
    nxp_stream_s *s = nxp_stream_create(0, NXP_STREAM_RELIABLE,
                                          NXP_DEFAULT_MAX_STREAM_DATA,
                                          NXP_DEFAULT_MAX_STREAM_DATA);
    NXP_ASSERT_NOT_NULL(s);

    uint8_t data[100];
    memset(data, 0xBB, sizeof(data));
    (void)nxp_stream_write(s, data, 100, false);

    /* Simulate sending */
    nxp_frame_stream out;
    (void)nxp_stream_fill_frame(s, &out, 100);

    /* ACK the first 50 bytes */
    nxp_stream_on_ack(s, 0, 50, false);
    NXP_ASSERT_EQ(s->send.acked_offset, (uint64_t)50);

    /* ACK the rest */
    nxp_stream_on_ack(s, 50, 50, false);
    NXP_ASSERT_EQ(s->send.acked_offset, (uint64_t)100);

    nxp_stream_destroy(s);
}

/* ── Test: Loss rewinds sent_offset ───────────────────── */

NXP_TEST(stream_on_loss) {
    nxp_stream_s *s = nxp_stream_create(0, NXP_STREAM_RELIABLE,
                                          NXP_DEFAULT_MAX_STREAM_DATA,
                                          NXP_DEFAULT_MAX_STREAM_DATA);
    NXP_ASSERT_NOT_NULL(s);

    uint8_t data[100];
    memset(data, 0xCC, sizeof(data));
    (void)nxp_stream_write(s, data, 100, false);

    /* Simulate sending */
    nxp_frame_stream out;
    (void)nxp_stream_fill_frame(s, &out, 100);
    NXP_ASSERT_EQ(s->send.sent_offset, (uint64_t)100);
    NXP_ASSERT_EQ(nxp_stream_unsent(s), (uint64_t)0);

    /* Declare loss at offset 50 */
    nxp_stream_on_loss(s, 50, 50, false);
    NXP_ASSERT_EQ(s->send.sent_offset, (uint64_t)50);
    NXP_ASSERT_EQ(nxp_stream_unsent(s), (uint64_t)50);

    nxp_stream_destroy(s);
}

/* ── Test: Flow control rejection ─────────────────────── */

NXP_TEST(stream_recv_flow_control) {
    /* Create stream with 100-byte recv limit */
    nxp_stream_s *s = nxp_stream_create(0, NXP_STREAM_RELIABLE,
                                          NXP_DEFAULT_MAX_STREAM_DATA,
                                          100);
    NXP_ASSERT_NOT_NULL(s);

    /* Try to receive 200 bytes - should fail */
    uint8_t data[200];
    memset(data, 0xDD, sizeof(data));
    nxp_frame_stream f = {
        .stream_id  = 0,
        .offset     = 0,
        .length     = 200,
        .has_offset = false,
        .has_length = true,
        .fin        = false,
        .data       = data,
    };
    nxp_result r = nxp_stream_on_recv(s, &f);
    NXP_ASSERT(r.code == NXP_ERR_FLOW_CONTROL);

    nxp_stream_destroy(s);
}

/* ── Main ──────────────────────────────────────────────── */

int main(void) {
    printf("=== NXP Stream Tests (Phase 4) ===\n");

    NXP_RUN_TEST(stream_create_destroy);
    NXP_RUN_TEST(stream_write_read);
    NXP_RUN_TEST(stream_write_fin);
    NXP_RUN_TEST(stream_fill_frame);
    NXP_RUN_TEST(stream_fill_frame_fin);
    NXP_RUN_TEST(stream_fill_frame_partial);
    NXP_RUN_TEST(stream_recv_fin);
    NXP_RUN_TEST(stream_on_ack);
    NXP_RUN_TEST(stream_on_loss);
    NXP_RUN_TEST(stream_recv_flow_control);

    NXP_TEST_SUMMARY();
}
