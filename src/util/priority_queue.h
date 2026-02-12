/*
 * NXP Min-Heap Priority Queue
 *
 * Used for timer management and stream scheduling.
 * Keys are uint64_t (timestamps or priorities).
 */
#ifndef NXP_PRIORITY_QUEUE_H
#define NXP_PRIORITY_QUEUE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct nxp_pq_entry {
    uint64_t key;       /* Sort key (lower = higher priority) */
    void    *value;     /* Associated data */
} nxp_pq_entry;

typedef struct nxp_priority_queue {
    nxp_pq_entry *entries;
    uint32_t      count;
    uint32_t      capacity;
} nxp_priority_queue;

/* Create a priority queue with initial capacity */
[[nodiscard]] nxp_priority_queue *nxp_pq_create(uint32_t initial_capacity);

/* Destroy the priority queue */
void nxp_pq_destroy(nxp_priority_queue *pq);

/* Push an entry (grows if needed) */
[[nodiscard]] bool nxp_pq_push(nxp_priority_queue *pq, uint64_t key, void *value);

/* Peek at the minimum element without removing it.
 * Returns false if empty. */
[[nodiscard]] bool nxp_pq_peek(const nxp_priority_queue *pq, uint64_t *key, void **value);

/* Pop the minimum element. Returns false if empty. */
bool nxp_pq_pop(nxp_priority_queue *pq, uint64_t *key, void **value);

/* Number of elements */
[[nodiscard]] static inline uint32_t nxp_pq_count(const nxp_priority_queue *pq) {
    return pq->count;
}

/* Is empty? */
[[nodiscard]] static inline bool nxp_pq_is_empty(const nxp_priority_queue *pq) {
    return pq->count == 0;
}

#endif /* NXP_PRIORITY_QUEUE_H */
