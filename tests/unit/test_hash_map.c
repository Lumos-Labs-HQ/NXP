/*
 * Unit tests for NXP Hash Map
 */
#include "test_framework.h"
#include "hash_map.h"

NXP_TEST(hmap_create_destroy) {
    nxp_hash_map *map = nxp_hash_map_create(16);
    NXP_ASSERT_NOT_NULL(map);
    NXP_ASSERT_EQ(nxp_hash_map_count(map), 0);
    nxp_hash_map_destroy(map);
}

NXP_TEST(hmap_put_get) {
    nxp_hash_map *map = nxp_hash_map_create(16);

    int val1 = 100, val2 = 200, val3 = 300;
    nxp_hash_map_put(map, 1, &val1);
    nxp_hash_map_put(map, 2, &val2);
    nxp_hash_map_put(map, 3, &val3);

    NXP_ASSERT_EQ(nxp_hash_map_count(map), 3);
    NXP_ASSERT_EQ(*(int *)nxp_hash_map_get(map, 1), 100);
    NXP_ASSERT_EQ(*(int *)nxp_hash_map_get(map, 2), 200);
    NXP_ASSERT_EQ(*(int *)nxp_hash_map_get(map, 3), 300);

    nxp_hash_map_destroy(map);
}

NXP_TEST(hmap_update) {
    nxp_hash_map *map = nxp_hash_map_create(16);

    int val1 = 100, val2 = 200;
    nxp_hash_map_put(map, 42, &val1);
    NXP_ASSERT_EQ(*(int *)nxp_hash_map_get(map, 42), 100);

    void *old = nxp_hash_map_put(map, 42, &val2);
    NXP_ASSERT_EQ(*(int *)old, 100);
    NXP_ASSERT_EQ(*(int *)nxp_hash_map_get(map, 42), 200);
    NXP_ASSERT_EQ(nxp_hash_map_count(map), 1);

    nxp_hash_map_destroy(map);
}

NXP_TEST(hmap_not_found) {
    nxp_hash_map *map = nxp_hash_map_create(16);
    NXP_ASSERT_NULL(nxp_hash_map_get(map, 999));
    NXP_ASSERT(!nxp_hash_map_contains(map, 999));
    nxp_hash_map_destroy(map);
}

NXP_TEST(hmap_remove) {
    nxp_hash_map *map = nxp_hash_map_create(16);

    int val = 42;
    nxp_hash_map_put(map, 10, &val);
    NXP_ASSERT(nxp_hash_map_contains(map, 10));

    void *removed = nxp_hash_map_remove(map, 10);
    NXP_ASSERT_EQ(*(int *)removed, 42);
    NXP_ASSERT(!nxp_hash_map_contains(map, 10));
    NXP_ASSERT_EQ(nxp_hash_map_count(map), 0);

    /* Remove non-existent key */
    NXP_ASSERT_NULL(nxp_hash_map_remove(map, 10));

    nxp_hash_map_destroy(map);
}

NXP_TEST(hmap_many_entries) {
    nxp_hash_map *map = nxp_hash_map_create(16);
    static int values[1000];

    /* Insert 1000 entries (triggers multiple resizes) */
    for (int i = 0; i < 1000; i++) {
        values[i] = i * 7;
        nxp_hash_map_put(map, (uint64_t)i, &values[i]);
    }

    NXP_ASSERT_EQ(nxp_hash_map_count(map), 1000);

    /* Verify all are retrievable */
    for (int i = 0; i < 1000; i++) {
        int *v = (int *)nxp_hash_map_get(map, (uint64_t)i);
        NXP_ASSERT_NOT_NULL(v);
        NXP_ASSERT_EQ(*v, i * 7);
    }

    /* Remove half */
    for (int i = 0; i < 500; i++) {
        nxp_hash_map_remove(map, (uint64_t)i);
    }
    NXP_ASSERT_EQ(nxp_hash_map_count(map), 500);

    /* Verify remaining */
    for (int i = 500; i < 1000; i++) {
        NXP_ASSERT(nxp_hash_map_contains(map, (uint64_t)i));
    }

    nxp_hash_map_destroy(map);
}

static bool count_iter(uint64_t key, void *value, void *user_data) {
    (void)key;
    (void)value;
    int *count = (int *)user_data;
    (*count)++;
    return true;
}

NXP_TEST(hmap_foreach) {
    nxp_hash_map *map = nxp_hash_map_create(16);
    int vals[5] = {1, 2, 3, 4, 5};

    for (int i = 0; i < 5; i++) {
        nxp_hash_map_put(map, (uint64_t)(i + 100), &vals[i]);
    }

    int count = 0;
    nxp_hash_map_foreach(map, count_iter, &count);
    NXP_ASSERT_EQ(count, 5);

    nxp_hash_map_destroy(map);
}

int main(void) {
    printf("=== Hash Map Tests ===\n");
    NXP_RUN_TEST(hmap_create_destroy);
    NXP_RUN_TEST(hmap_put_get);
    NXP_RUN_TEST(hmap_update);
    NXP_RUN_TEST(hmap_not_found);
    NXP_RUN_TEST(hmap_remove);
    NXP_RUN_TEST(hmap_many_entries);
    NXP_RUN_TEST(hmap_foreach);
    NXP_TEST_SUMMARY();
}
