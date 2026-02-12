/*
 * NXP Min-Heap Priority Queue - Implementation
 */
#include "priority_queue.h"

#include <stdlib.h>
#include <string.h>

#define PQ_PARENT(i) (((i) - 1) / 2)
#define PQ_LEFT(i)   (2 * (i) + 1)
#define PQ_RIGHT(i)  (2 * (i) + 2)

static void swap_entries(nxp_pq_entry *a, nxp_pq_entry *b) {
    nxp_pq_entry tmp = *a;
    *a = *b;
    *b = tmp;
}

static void sift_up(nxp_priority_queue *pq, uint32_t idx) {
    while (idx > 0) {
        uint32_t parent = PQ_PARENT(idx);
        if (pq->entries[parent].key <= pq->entries[idx].key) break;
        swap_entries(&pq->entries[parent], &pq->entries[idx]);
        idx = parent;
    }
}

static void sift_down(nxp_priority_queue *pq, uint32_t idx) {
    for (;;) {
        uint32_t smallest = idx;
        uint32_t left  = PQ_LEFT(idx);
        uint32_t right = PQ_RIGHT(idx);

        if (left < pq->count && pq->entries[left].key < pq->entries[smallest].key)
            smallest = left;
        if (right < pq->count && pq->entries[right].key < pq->entries[smallest].key)
            smallest = right;

        if (smallest == idx) break;

        swap_entries(&pq->entries[idx], &pq->entries[smallest]);
        idx = smallest;
    }
}

nxp_priority_queue *nxp_pq_create(uint32_t initial_capacity) {
    if (initial_capacity < 16) initial_capacity = 16;

    nxp_priority_queue *pq = (nxp_priority_queue *)malloc(sizeof(nxp_priority_queue));
    if (pq == nullptr) return nullptr;

    pq->entries = (nxp_pq_entry *)malloc(initial_capacity * sizeof(nxp_pq_entry));
    if (pq->entries == nullptr) {
        free(pq);
        return nullptr;
    }

    pq->count    = 0;
    pq->capacity = initial_capacity;
    return pq;
}

void nxp_pq_destroy(nxp_priority_queue *pq) {
    if (pq == nullptr) return;
    free(pq->entries);
    free(pq);
}

bool nxp_pq_push(nxp_priority_queue *pq, uint64_t key, void *value) {
    /* Grow if needed */
    if (pq->count >= pq->capacity) {
        uint32_t new_cap = pq->capacity * 2;
        nxp_pq_entry *new_entries = (nxp_pq_entry *)realloc(
            pq->entries, new_cap * sizeof(nxp_pq_entry));
        if (new_entries == nullptr) return false;
        pq->entries  = new_entries;
        pq->capacity = new_cap;
    }

    pq->entries[pq->count] = (nxp_pq_entry){ .key = key, .value = value };
    sift_up(pq, pq->count);
    pq->count++;
    return true;
}

bool nxp_pq_peek(const nxp_priority_queue *pq, uint64_t *key, void **value) {
    if (pq->count == 0) return false;
    if (key)   *key   = pq->entries[0].key;
    if (value) *value = pq->entries[0].value;
    return true;
}

bool nxp_pq_pop(nxp_priority_queue *pq, uint64_t *key, void **value) {
    if (pq->count == 0) return false;

    if (key)   *key   = pq->entries[0].key;
    if (value) *value = pq->entries[0].value;

    pq->count--;
    if (pq->count > 0) {
        pq->entries[0] = pq->entries[pq->count];
        sift_down(pq, 0);
    }

    return true;
}
