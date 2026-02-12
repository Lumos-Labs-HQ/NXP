/*
 * Unit tests for NXP Fixed-Size Object Pool
 */
#include "test_framework.h"
#include "pool.h"

NXP_TEST(pool_create_destroy) {
    nxp_pool *pool = nxp_pool_create(64, 100);
    NXP_ASSERT_NOT_NULL(pool);
    NXP_ASSERT_EQ(nxp_pool_available(pool), 100);
    nxp_pool_destroy(pool);
}

NXP_TEST(pool_alloc_free) {
    nxp_pool *pool = nxp_pool_create(32, 10);

    void *p1 = nxp_pool_alloc(pool);
    NXP_ASSERT_NOT_NULL(p1);
    NXP_ASSERT_EQ(pool->in_use, 1);

    void *p2 = nxp_pool_alloc(pool);
    NXP_ASSERT_NOT_NULL(p2);
    NXP_ASSERT(p1 != p2);

    nxp_pool_free(pool, p1);
    NXP_ASSERT_EQ(pool->in_use, 1);

    nxp_pool_free(pool, p2);
    NXP_ASSERT_EQ(pool->in_use, 0);

    nxp_pool_destroy(pool);
}

NXP_TEST(pool_exhaust) {
    nxp_pool *pool = nxp_pool_create(16, 3);

    void *a = nxp_pool_alloc(pool);
    void *b = nxp_pool_alloc(pool);
    void *c = nxp_pool_alloc(pool);
    NXP_ASSERT_NOT_NULL(a);
    NXP_ASSERT_NOT_NULL(b);
    NXP_ASSERT_NOT_NULL(c);

    /* Pool should be exhausted */
    void *d = nxp_pool_alloc(pool);
    NXP_ASSERT_NULL(d);
    NXP_ASSERT(nxp_pool_is_empty(pool));

    /* Free one and re-allocate */
    nxp_pool_free(pool, b);
    void *e = nxp_pool_alloc(pool);
    NXP_ASSERT_NOT_NULL(e);

    nxp_pool_free(pool, a);
    nxp_pool_free(pool, c);
    nxp_pool_free(pool, e);
    nxp_pool_destroy(pool);
}

NXP_TEST(pool_zero_init) {
    nxp_pool *pool = nxp_pool_create(64, 5);

    uint8_t *p = (uint8_t *)nxp_pool_alloc(pool);
    NXP_ASSERT_NOT_NULL(p);

    /* Should be zeroed */
    for (int i = 0; i < 64; i++) {
        NXP_ASSERT_EQ(p[i], 0);
    }

    nxp_pool_free(pool, p);
    nxp_pool_destroy(pool);
}

int main(void) {
    printf("=== Object Pool Tests ===\n");
    NXP_RUN_TEST(pool_create_destroy);
    NXP_RUN_TEST(pool_alloc_free);
    NXP_RUN_TEST(pool_exhaust);
    NXP_RUN_TEST(pool_zero_init);
    NXP_TEST_SUMMARY();
}
