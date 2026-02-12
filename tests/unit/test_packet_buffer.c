/*
 * Unit tests for NXP Packet Buffer Pool
 */
#include "test_framework.h"
#include "packet_buffer.h"
#include <string.h>

NXP_TEST(pktpool_create_destroy) {
    nxp_packet_pool *pool = nxp_packet_pool_create(64);
    NXP_ASSERT_NOT_NULL(pool);
    NXP_ASSERT_EQ(pool->free_count, 64);
    nxp_packet_pool_destroy(pool);
}

NXP_TEST(pktpool_get_put) {
    nxp_packet_pool *pool = nxp_packet_pool_create(8);

    nxp_packet_buf *buf = nxp_packet_pool_get(pool);
    NXP_ASSERT_NOT_NULL(buf);
    NXP_ASSERT_EQ(buf->len, 0);
    NXP_ASSERT_EQ(pool->free_count, 7);

    /* Write data */
    memcpy(buf->data, "test", 4);
    buf->len = 4;

    nxp_packet_pool_put(pool, buf);
    NXP_ASSERT_EQ(pool->free_count, 8);

    nxp_packet_pool_destroy(pool);
}

NXP_TEST(pktpool_exhaust) {
    nxp_packet_pool *pool = nxp_packet_pool_create(3);

    nxp_packet_buf *a = nxp_packet_pool_get(pool);
    nxp_packet_buf *b = nxp_packet_pool_get(pool);
    nxp_packet_buf *c = nxp_packet_pool_get(pool);
    NXP_ASSERT_NOT_NULL(a);
    NXP_ASSERT_NOT_NULL(b);
    NXP_ASSERT_NOT_NULL(c);

    nxp_packet_buf *d = nxp_packet_pool_get(pool);
    NXP_ASSERT_NULL(d);

    nxp_packet_pool_put(pool, b);
    nxp_packet_buf *e = nxp_packet_pool_get(pool);
    NXP_ASSERT_NOT_NULL(e);

    nxp_packet_pool_put(pool, a);
    nxp_packet_pool_put(pool, c);
    nxp_packet_pool_put(pool, e);
    nxp_packet_pool_destroy(pool);
}

NXP_TEST(pktpool_alignment) {
    nxp_packet_pool *pool = nxp_packet_pool_create(4);

    nxp_packet_buf *buf = nxp_packet_pool_get(pool);
    NXP_ASSERT_NOT_NULL(buf);

    /* Data should be 64-byte aligned (cache line) */
    NXP_ASSERT(((uintptr_t)buf->data % 64) == 0);

    nxp_packet_pool_put(pool, buf);
    nxp_packet_pool_destroy(pool);
}

int main(void) {
    printf("=== Packet Buffer Pool Tests ===\n");
    NXP_RUN_TEST(pktpool_create_destroy);
    NXP_RUN_TEST(pktpool_get_put);
    NXP_RUN_TEST(pktpool_exhaust);
    NXP_RUN_TEST(pktpool_alignment);
    NXP_TEST_SUMMARY();
}
