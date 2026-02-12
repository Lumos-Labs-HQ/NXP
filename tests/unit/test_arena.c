/*
 * Unit tests for NXP Arena Allocator
 */
#include "test_framework.h"
#include "arena.h"

NXP_TEST(arena_create_destroy) {
    nxp_arena *arena = nxp_arena_create(0);
    NXP_ASSERT_NOT_NULL(arena);
    nxp_arena_destroy(arena);
}

NXP_TEST(arena_basic_alloc) {
    nxp_arena *arena = nxp_arena_create(4096);
    NXP_ASSERT_NOT_NULL(arena);

    void *p1 = nxp_arena_alloc(arena, 64, 8);
    NXP_ASSERT_NOT_NULL(p1);

    void *p2 = nxp_arena_alloc(arena, 128, 16);
    NXP_ASSERT_NOT_NULL(p2);
    NXP_ASSERT(p1 != p2);

    /* Verify alignment */
    NXP_ASSERT(((uintptr_t)p1 % 8) == 0);
    NXP_ASSERT(((uintptr_t)p2 % 16) == 0);

    nxp_arena_destroy(arena);
}

NXP_TEST(arena_type_safe_alloc) {
    nxp_arena *arena = nxp_arena_create(4096);

    typedef struct { uint64_t a; uint32_t b; } test_struct;

    test_struct *s = nxp_arena_new(arena, test_struct);
    NXP_ASSERT_NOT_NULL(s);

    test_struct *arr = nxp_arena_new_array(arena, test_struct, 10);
    NXP_ASSERT_NOT_NULL(arr);

    /* Write to verify no overlap */
    s->a = 0xDEADBEEF;
    s->b = 42;
    for (int i = 0; i < 10; i++) {
        arr[i].a = (uint64_t)i;
        arr[i].b = (uint32_t)(i * 2);
    }

    NXP_ASSERT_EQ(s->a, 0xDEADBEEF);
    NXP_ASSERT_EQ(arr[5].a, 5);

    nxp_arena_destroy(arena);
}

NXP_TEST(arena_cross_block) {
    /* Small block size to force multiple blocks */
    nxp_arena *arena = nxp_arena_create(128);
    NXP_ASSERT_NOT_NULL(arena);

    /* Allocate more than one block's worth */
    for (int i = 0; i < 100; i++) {
        void *p = nxp_arena_alloc(arena, 32, 8);
        NXP_ASSERT_NOT_NULL(p);
    }

    nxp_arena_destroy(arena);
}

NXP_TEST(arena_large_alloc) {
    nxp_arena *arena = nxp_arena_create(1024);

    /* Allocate larger than block size */
    void *p = nxp_arena_alloc(arena, 8192, 16);
    NXP_ASSERT_NOT_NULL(p);
    NXP_ASSERT(((uintptr_t)p % 16) == 0);

    nxp_arena_destroy(arena);
}

NXP_TEST(arena_reset) {
    nxp_arena *arena = nxp_arena_create(4096);

    void *p1 = nxp_arena_alloc(arena, 256, 8);
    NXP_ASSERT_NOT_NULL(p1);

    nxp_arena_reset(arena);

    /* After reset, should be able to allocate from the start */
    void *p2 = nxp_arena_alloc(arena, 256, 8);
    NXP_ASSERT_NOT_NULL(p2);

    nxp_arena_destroy(arena);
}

int main(void) {
    printf("=== Arena Allocator Tests ===\n");
    NXP_RUN_TEST(arena_create_destroy);
    NXP_RUN_TEST(arena_basic_alloc);
    NXP_RUN_TEST(arena_type_safe_alloc);
    NXP_RUN_TEST(arena_cross_block);
    NXP_RUN_TEST(arena_large_alloc);
    NXP_RUN_TEST(arena_reset);
    NXP_TEST_SUMMARY();
}
