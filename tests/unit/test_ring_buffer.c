/*
 * Unit tests for NXP Lock-Free SPSC Ring Buffer
 */
#include "test_framework.h"
#include "ring_buffer.h"

NXP_TEST(rb_create_destroy) {
    nxp_ring_buffer *rb = nxp_ring_buffer_create(1024);
    NXP_ASSERT_NOT_NULL(rb);
    NXP_ASSERT(nxp_ring_buffer_is_empty(rb));
    NXP_ASSERT_EQ(nxp_ring_buffer_readable(rb), 0);
    nxp_ring_buffer_destroy(rb);
}

NXP_TEST(rb_write_read) {
    nxp_ring_buffer *rb = nxp_ring_buffer_create(256);

    uint8_t data[] = "Hello, NXP!";
    size_t written = nxp_ring_buffer_write(rb, data, sizeof(data));
    NXP_ASSERT_EQ(written, sizeof(data));
    NXP_ASSERT_EQ(nxp_ring_buffer_readable(rb), sizeof(data));

    uint8_t buf[64];
    size_t read = nxp_ring_buffer_read(rb, buf, sizeof(buf));
    NXP_ASSERT_EQ(read, sizeof(data));
    NXP_ASSERT(memcmp(buf, data, sizeof(data)) == 0);
    NXP_ASSERT(nxp_ring_buffer_is_empty(rb));

    nxp_ring_buffer_destroy(rb);
}

NXP_TEST(rb_wrap_around) {
    nxp_ring_buffer *rb = nxp_ring_buffer_create(32);  /* 32 bytes capacity */

    /* Fill most of it */
    uint8_t fill[24];
    memset(fill, 0xAA, sizeof(fill));
    size_t w = nxp_ring_buffer_write(rb, fill, sizeof(fill));
    NXP_ASSERT_EQ(w, 24);

    /* Read it back */
    uint8_t tmp[24];
    size_t r = nxp_ring_buffer_read(rb, tmp, sizeof(tmp));
    NXP_ASSERT_EQ(r, 24);

    /* Now write data that wraps around the end */
    uint8_t wrap_data[20];
    memset(wrap_data, 0xBB, sizeof(wrap_data));
    w = nxp_ring_buffer_write(rb, wrap_data, sizeof(wrap_data));
    NXP_ASSERT_EQ(w, 20);

    uint8_t out[20];
    r = nxp_ring_buffer_read(rb, out, sizeof(out));
    NXP_ASSERT_EQ(r, 20);
    NXP_ASSERT(memcmp(out, wrap_data, 20) == 0);

    nxp_ring_buffer_destroy(rb);
}

NXP_TEST(rb_full) {
    nxp_ring_buffer *rb = nxp_ring_buffer_create(16);

    uint8_t data[16];
    memset(data, 0xFF, sizeof(data));
    size_t w = nxp_ring_buffer_write(rb, data, sizeof(data));
    NXP_ASSERT_EQ(w, 16);

    /* Should have no space left */
    NXP_ASSERT_EQ(nxp_ring_buffer_writable(rb), 0);

    /* Writing more should return 0 */
    uint8_t more = 0x42;
    w = nxp_ring_buffer_write(rb, &more, 1);
    NXP_ASSERT_EQ(w, 0);

    nxp_ring_buffer_destroy(rb);
}

NXP_TEST(rb_partial_read) {
    nxp_ring_buffer *rb = nxp_ring_buffer_create(128);

    uint8_t data[50];
    for (int i = 0; i < 50; i++) data[i] = (uint8_t)i;
    nxp_ring_buffer_write(rb, data, 50);

    /* Read in parts */
    uint8_t buf1[20], buf2[30];
    size_t r1 = nxp_ring_buffer_read(rb, buf1, 20);
    size_t r2 = nxp_ring_buffer_read(rb, buf2, 30);
    NXP_ASSERT_EQ(r1, 20);
    NXP_ASSERT_EQ(r2, 30);

    NXP_ASSERT_EQ(buf1[0], 0);
    NXP_ASSERT_EQ(buf1[19], 19);
    NXP_ASSERT_EQ(buf2[0], 20);
    NXP_ASSERT_EQ(buf2[29], 49);

    nxp_ring_buffer_destroy(rb);
}

int main(void) {
    printf("=== Ring Buffer Tests ===\n");
    NXP_RUN_TEST(rb_create_destroy);
    NXP_RUN_TEST(rb_write_read);
    NXP_RUN_TEST(rb_wrap_around);
    NXP_RUN_TEST(rb_full);
    NXP_RUN_TEST(rb_partial_read);
    NXP_TEST_SUMMARY();
}
