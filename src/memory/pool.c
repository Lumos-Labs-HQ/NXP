/*
 * NXP Fixed-Size Object Pool - Implementation
 */
#include "pool.h"

#include <stdlib.h>
#include <string.h>

static size_t align_up(size_t value, size_t align) {
    return (value + align - 1) & ~(align - 1);
}

nxp_pool *nxp_pool_create(size_t obj_size, uint32_t capacity) {
    if (capacity == 0) {
        return nullptr;
    }

    /* Ensure obj_size can hold a free-list pointer */
    if (obj_size < sizeof(void *)) {
        obj_size = sizeof(void *);
    }

    /* Align to pointer size */
    obj_size = align_up(obj_size, sizeof(void *));

    nxp_pool *pool = (nxp_pool *)malloc(sizeof(nxp_pool));
    if (pool == nullptr) {
        return nullptr;
    }

    pool->backing = (uint8_t *)malloc(obj_size * capacity);
    if (pool->backing == nullptr) {
        free(pool);
        return nullptr;
    }

    memset(pool->backing, 0, obj_size * capacity);

    pool->obj_size = obj_size;
    pool->capacity = capacity;
    pool->in_use   = 0;

    /* Build the free list (each slot points to the next) */
    pool->free_list = nullptr;
    for (uint32_t i = capacity; i > 0; i--) {
        void *slot = pool->backing + (i - 1) * obj_size;
        *(void **)slot = pool->free_list;
        pool->free_list = slot;
    }

    return pool;
}

void nxp_pool_destroy(nxp_pool *pool) {
    if (pool == nullptr) {
        return;
    }
    free(pool->backing);
    free(pool);
}

void *nxp_pool_alloc(nxp_pool *pool) {
    if (pool == nullptr || pool->free_list == nullptr) {
        return nullptr;
    }

    void *obj = pool->free_list;
    pool->free_list = *(void **)obj;
    pool->in_use++;

    /* Zero the object before returning */
    memset(obj, 0, pool->obj_size);
    return obj;
}

void nxp_pool_free(nxp_pool *pool, void *obj) {
    if (pool == nullptr || obj == nullptr) {
        return;
    }

#ifdef NXP_DEBUG
    /* Validate obj is within backing memory */
    uint8_t *p = (uint8_t *)obj;
    if (p < pool->backing || p >= pool->backing + pool->obj_size * pool->capacity) {
        return; /* Object not from this pool */
    }
#endif

    *(void **)obj = pool->free_list;
    pool->free_list = obj;
    pool->in_use--;
}
