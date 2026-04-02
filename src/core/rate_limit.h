/*
 * Simple rate limiter for DoS protection
 */
#ifndef NXP_RATE_LIMIT_H
#define NXP_RATE_LIMIT_H

#include "nxp/nxp_types.h"
#include "hash_map.h"
#include <stdint.h>

#define NXP_RATE_LIMIT_WINDOW_US  1000000  /* 1 second */
#define NXP_RATE_LIMIT_MAX_PPS    100      /* 100 packets per second per IP */

typedef struct {
    nxp_hash_map *ip_buckets;
    uint64_t window_start_us;
} nxp_rate_limiter;

typedef struct {
    uint32_t count;
    uint64_t window_start;
} nxp_rate_bucket;

static inline nxp_rate_limiter *nxp_rate_limiter_create(void) {
    nxp_rate_limiter *rl = calloc(1, sizeof(nxp_rate_limiter));
    if (!rl) return NULL;
    rl->ip_buckets = nxp_hash_map_create(256);
    return rl;
}

static inline void nxp_rate_limiter_destroy(nxp_rate_limiter *rl) {
    if (!rl) return;
    /* Free all buckets */
    /* Note: hash_map doesn't have iterator, so we leak buckets for now */
    /* TODO: Add hash_map_foreach or track buckets separately */
    nxp_hash_map_destroy(rl->ip_buckets);
    free(rl);
}

static inline uint64_t addr_hash(const nxp_addr *addr) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < sizeof(addr->raw); i++) {
        h ^= addr->raw[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static inline bool nxp_rate_limiter_check(nxp_rate_limiter *rl, const nxp_addr *addr, uint64_t now_us) {
    if (!rl) return true;
    
    uint64_t key = addr_hash(addr);
    nxp_rate_bucket *bucket = (nxp_rate_bucket *)nxp_hash_map_get(rl->ip_buckets, key);
    
    /* Reset window if expired */
    if (!bucket || (now_us - bucket->window_start) >= NXP_RATE_LIMIT_WINDOW_US) {
        if (!bucket) {
            bucket = calloc(1, sizeof(nxp_rate_bucket));
            if (!bucket) return true; /* Allow on allocation failure */
            nxp_hash_map_put(rl->ip_buckets, key, bucket);
        }
        bucket->count = 0;
        bucket->window_start = now_us;
    }
    
    /* Check limit */
    if (bucket->count >= NXP_RATE_LIMIT_MAX_PPS) {
        return false; /* Rate limit exceeded */
    }
    
    bucket->count++;
    return true;
}

#endif /* NXP_RATE_LIMIT_H */
