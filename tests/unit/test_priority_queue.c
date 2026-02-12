/*
 * Unit tests for NXP Priority Queue (Min-Heap)
 */
#include "test_framework.h"
#include "priority_queue.h"

NXP_TEST(pq_create_destroy) {
    nxp_priority_queue *pq = nxp_pq_create(16);
    NXP_ASSERT_NOT_NULL(pq);
    NXP_ASSERT(nxp_pq_is_empty(pq));
    NXP_ASSERT_EQ(nxp_pq_count(pq), 0);
    nxp_pq_destroy(pq);
}

NXP_TEST(pq_push_pop_order) {
    nxp_priority_queue *pq = nxp_pq_create(16);

    /* Insert out of order */
    nxp_pq_push(pq, 50, (void *)50);
    nxp_pq_push(pq, 10, (void *)10);
    nxp_pq_push(pq, 30, (void *)30);
    nxp_pq_push(pq, 20, (void *)20);
    nxp_pq_push(pq, 40, (void *)40);

    NXP_ASSERT_EQ(nxp_pq_count(pq), 5);

    /* Should come out in order */
    uint64_t key;
    void *value;

    nxp_pq_pop(pq, &key, &value);
    NXP_ASSERT_EQ(key, 10);

    nxp_pq_pop(pq, &key, &value);
    NXP_ASSERT_EQ(key, 20);

    nxp_pq_pop(pq, &key, &value);
    NXP_ASSERT_EQ(key, 30);

    nxp_pq_pop(pq, &key, &value);
    NXP_ASSERT_EQ(key, 40);

    nxp_pq_pop(pq, &key, &value);
    NXP_ASSERT_EQ(key, 50);

    NXP_ASSERT(nxp_pq_is_empty(pq));
    nxp_pq_destroy(pq);
}

NXP_TEST(pq_peek) {
    nxp_priority_queue *pq = nxp_pq_create(16);

    nxp_pq_push(pq, 99, (void *)99);
    nxp_pq_push(pq, 5, (void *)5);

    uint64_t key;
    void *value;
    bool ok = nxp_pq_peek(pq, &key, &value);
    NXP_ASSERT(ok);
    NXP_ASSERT_EQ(key, 5);
    NXP_ASSERT_EQ(nxp_pq_count(pq), 2);  /* Peek doesn't remove */

    nxp_pq_destroy(pq);
}

NXP_TEST(pq_empty_pop) {
    nxp_priority_queue *pq = nxp_pq_create(16);

    uint64_t key;
    void *value;
    NXP_ASSERT(!nxp_pq_pop(pq, &key, &value));
    NXP_ASSERT(!nxp_pq_peek(pq, &key, &value));

    nxp_pq_destroy(pq);
}

NXP_TEST(pq_grow) {
    nxp_priority_queue *pq = nxp_pq_create(4);

    /* Insert more than initial capacity */
    for (uint64_t i = 100; i > 0; i--) {
        nxp_pq_push(pq, i, (void *)i);
    }
    NXP_ASSERT_EQ(nxp_pq_count(pq), 100);

    /* Verify ordering */
    uint64_t prev = 0;
    for (int i = 0; i < 100; i++) {
        uint64_t key;
        nxp_pq_pop(pq, &key, nullptr);
        NXP_ASSERT(key > prev);
        prev = key;
    }

    nxp_pq_destroy(pq);
}

int main(void) {
    printf("=== Priority Queue Tests ===\n");
    NXP_RUN_TEST(pq_create_destroy);
    NXP_RUN_TEST(pq_push_pop_order);
    NXP_RUN_TEST(pq_peek);
    NXP_RUN_TEST(pq_empty_pop);
    NXP_RUN_TEST(pq_grow);
    NXP_TEST_SUMMARY();
}
