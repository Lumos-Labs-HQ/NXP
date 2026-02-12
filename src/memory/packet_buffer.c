/*
 * NXP Packet Buffer Pool - Implementation
 */
#include "packet_buffer.h"

#include <stdlib.h>
#include <string.h>

nxp_packet_pool *nxp_packet_pool_create(uint32_t capacity) {
    if (capacity == 0) {
        return nullptr;
    }

    nxp_packet_pool *pool = (nxp_packet_pool *)malloc(sizeof(nxp_packet_pool));
    if (pool == nullptr) {
        return nullptr;
    }

    /* Use aligned allocation to honor alignas(64) on packet data */
    size_t alloc_size = capacity * sizeof(nxp_packet_buf);
    /* aligned_alloc requires size to be a multiple of alignment */
    alloc_size = (alloc_size + 63) & ~(size_t)63;
#ifdef _WIN32
    pool->backing = (nxp_packet_buf *)_aligned_malloc(alloc_size, 64);
#else
    pool->backing = (nxp_packet_buf *)aligned_alloc(64, alloc_size);
#endif
    if (pool->backing == nullptr) {
        free(pool);
        return nullptr;
    }
    memset(pool->backing, 0, alloc_size);

    pool->capacity   = capacity;
    pool->free_count = capacity;

    /* Build free list */
    pool->free_list = nullptr;
    for (uint32_t i = capacity; i > 0; i--) {
        nxp_packet_buf *buf = &pool->backing[i - 1];
        buf->next = pool->free_list;
        buf->len  = 0;
        pool->free_list = buf;
    }

    return pool;
}

void nxp_packet_pool_destroy(nxp_packet_pool *pool) {
    if (pool == nullptr) {
        return;
    }
#ifdef _WIN32
    _aligned_free(pool->backing);
#else
    free(pool->backing);
#endif
    free(pool);
}

nxp_packet_buf *nxp_packet_pool_get(nxp_packet_pool *pool) {
    if (pool == nullptr || pool->free_list == nullptr) {
        return nullptr;
    }

    nxp_packet_buf *buf = pool->free_list;
    pool->free_list = buf->next;
    pool->free_count--;

    buf->next         = nullptr;
    buf->len          = 0;
    buf->timestamp_us = 0;
    memset(&buf->addr, 0, sizeof(buf->addr));

    return buf;
}

void nxp_packet_pool_put(nxp_packet_pool *pool, nxp_packet_buf *buf) {
    if (pool == nullptr || buf == nullptr) {
        return;
    }

    buf->next = pool->free_list;
    pool->free_list = buf;
    pool->free_count++;
}
