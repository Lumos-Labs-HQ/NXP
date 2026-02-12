/*
 * NXP Slab Allocator - Implementation
 */
#include "slab.h"

#include <stdlib.h>
#include <string.h>

static size_t align_up(size_t value, size_t align) {
    return (value + align - 1) & ~(align - 1);
}

static bool slab_grow(nxp_slab *slab) {
    size_t data_size = slab->obj_size * slab->chunk_count;

    nxp_slab_chunk *chunk = (nxp_slab_chunk *)malloc(sizeof(nxp_slab_chunk));
    if (chunk == nullptr) {
        return false;
    }

    chunk->data = (uint8_t *)malloc(data_size);
    if (chunk->data == nullptr) {
        free(chunk);
        return false;
    }

    memset(chunk->data, 0, data_size);
    chunk->capacity = slab->chunk_count;

    /* Link into chunk list */
    chunk->next = slab->chunks;
    slab->chunks = chunk;

    /* Add all objects to free list */
    for (uint32_t i = slab->chunk_count; i > 0; i--) {
        void *slot = chunk->data + (i - 1) * slab->obj_size;
        *(void **)slot = slab->free_list;
        slab->free_list = slot;
    }

    slab->total_alloc += slab->chunk_count;
    return true;
}

nxp_slab *nxp_slab_create(size_t obj_size, uint32_t chunk_count) {
    if (chunk_count == 0) {
        return nullptr;
    }

    if (obj_size < sizeof(void *)) {
        obj_size = sizeof(void *);
    }
    obj_size = align_up(obj_size, sizeof(void *));

    nxp_slab *slab = (nxp_slab *)malloc(sizeof(nxp_slab));
    if (slab == nullptr) {
        return nullptr;
    }

    slab->chunks      = nullptr;
    slab->free_list   = nullptr;
    slab->obj_size    = obj_size;
    slab->chunk_count = chunk_count;
    slab->total_alloc = 0;
    slab->in_use      = 0;

    if (!slab_grow(slab)) {
        free(slab);
        return nullptr;
    }

    return slab;
}

void nxp_slab_destroy(nxp_slab *slab) {
    if (slab == nullptr) {
        return;
    }

    nxp_slab_chunk *chunk = slab->chunks;
    while (chunk != nullptr) {
        nxp_slab_chunk *next = chunk->next;
        free(chunk->data);
        free(chunk);
        chunk = next;
    }

    free(slab);
}

void *nxp_slab_alloc(nxp_slab *slab) {
    if (slab == nullptr) {
        return nullptr;
    }

    /* Grow if free list is empty */
    if (slab->free_list == nullptr) {
        if (!slab_grow(slab)) {
            return nullptr;
        }
    }

    void *obj = slab->free_list;
    slab->free_list = *(void **)obj;
    slab->in_use++;

    memset(obj, 0, slab->obj_size);
    return obj;
}

void nxp_slab_free(nxp_slab *slab, void *obj) {
    if (slab == nullptr || obj == nullptr) {
        return;
    }

    *(void **)obj = slab->free_list;
    slab->free_list = obj;
    slab->in_use--;
}
