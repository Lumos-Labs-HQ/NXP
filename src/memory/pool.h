/*
 * NXP Fixed-Size Object Pool
 *
 * Pre-allocates a contiguous block of fixed-size objects.
 * Alloc/free are O(1) via intrusive free list.
 */
#ifndef NXP_POOL_H
#define NXP_POOL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "nxp/nxp_error.h"

typedef struct nxp_pool {
    uint8_t  *backing;       /* Contiguous memory block */
    void     *free_list;     /* Intrusive free list head */
    size_t    obj_size;      /* Size of each object (aligned) */
    uint32_t  capacity;      /* Total number of objects */
    uint32_t  in_use;        /* Currently allocated count */
} nxp_pool;

/* Create a pool of `capacity` objects, each `obj_size` bytes.
 * obj_size must be >= sizeof(void*) */
[[nodiscard]] nxp_pool *nxp_pool_create(size_t obj_size, uint32_t capacity);

/* Destroy the pool */
void nxp_pool_destroy(nxp_pool *pool);

/* Allocate one object from the pool. Returns nullptr if exhausted. */
[[nodiscard]] void *nxp_pool_alloc(nxp_pool *pool);

/* Return an object to the pool */
void nxp_pool_free(nxp_pool *pool, void *obj);

/* Check if pool is empty */
[[nodiscard]] static inline bool nxp_pool_is_empty(const nxp_pool *pool) {
    return pool->in_use == pool->capacity;
}

/* Get number of available objects */
[[nodiscard]] static inline uint32_t nxp_pool_available(const nxp_pool *pool) {
    return pool->capacity - pool->in_use;
}

#endif /* NXP_POOL_H */
