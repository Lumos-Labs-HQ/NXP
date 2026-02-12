/*
 * NXP Packet Buffer Pool
 *
 * Pre-allocated cache-line-aligned MTU-sized buffers for UDP datagrams.
 * Uses a lock-free Treiber stack (single-threaded version for now).
 */
#ifndef NXP_PACKET_BUFFER_H
#define NXP_PACKET_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>
#include <stdbool.h>
#include "nxp/nxp_types.h"

#define NXP_PKT_BUF_DATA_SIZE  NXP_PACKET_BUF_SIZE

typedef struct nxp_packet_buf {
    alignas(64) uint8_t        data[NXP_PKT_BUF_DATA_SIZE];
    size_t                     len;
    nxp_addr                   addr;
    uint64_t                   timestamp_us;
    struct nxp_packet_buf     *next;     /* Free list linkage */
} nxp_packet_buf;

typedef struct nxp_packet_pool {
    nxp_packet_buf  *free_list;
    uint32_t         free_count;
    nxp_packet_buf  *backing;           /* Contiguous allocation */
    uint32_t         capacity;
} nxp_packet_pool;

/* Create a packet buffer pool with `capacity` buffers */
[[nodiscard]] nxp_packet_pool *nxp_packet_pool_create(uint32_t capacity);

/* Destroy the pool */
void nxp_packet_pool_destroy(nxp_packet_pool *pool);

/* Get a buffer from the pool (returns nullptr if exhausted) */
[[nodiscard]] nxp_packet_buf *nxp_packet_pool_get(nxp_packet_pool *pool);

/* Return a buffer to the pool */
void nxp_packet_pool_put(nxp_packet_pool *pool, nxp_packet_buf *buf);

#endif /* NXP_PACKET_BUFFER_H */
