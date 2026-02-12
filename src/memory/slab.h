/*
 * NXP Slab Allocator
 *
 * Generic slab allocator that grows by adding new slabs.
 * Unlike the fixed pool, this can grow dynamically.
 */
#ifndef NXP_SLAB_H
#define NXP_SLAB_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct nxp_slab_chunk {
    struct nxp_slab_chunk *next;
    uint8_t               *data;
    uint32_t               capacity;
} nxp_slab_chunk;

typedef struct nxp_slab {
    nxp_slab_chunk *chunks;         /* Linked list of allocated chunks */
    void           *free_list;      /* Free list head */
    size_t          obj_size;       /* Aligned object size */
    uint32_t        chunk_count;    /* Objects per chunk */
    uint32_t        total_alloc;    /* Total objects allocated */
    uint32_t        in_use;         /* Currently in-use count */
} nxp_slab;

/* Create a slab allocator. `chunk_count` objects per chunk. */
[[nodiscard]] nxp_slab *nxp_slab_create(size_t obj_size, uint32_t chunk_count);

/* Destroy slab, freeing all chunks */
void nxp_slab_destroy(nxp_slab *slab);

/* Allocate one object (grows if needed) */
[[nodiscard]] void *nxp_slab_alloc(nxp_slab *slab);

/* Free one object back to the slab */
void nxp_slab_free(nxp_slab *slab, void *obj);

#endif /* NXP_SLAB_H */
