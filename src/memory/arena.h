/*
 * NXP Arena Allocator
 *
 * Bump allocator with linked blocks. Ideal for per-connection lifetime:
 * allocate many objects, free them all at once when connection closes.
 */
#ifndef NXP_ARENA_H
#define NXP_ARENA_H

#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>
#include <stdbool.h>
#include "nxp/nxp_error.h"

#define NXP_ARENA_DEFAULT_BLOCK_SIZE (64u * 1024u)  /* 64 KiB */

typedef struct nxp_arena_block {
    struct nxp_arena_block *next;
    size_t                  capacity;
    size_t                  used;
    alignas(16) uint8_t     data[];
} nxp_arena_block;

typedef struct nxp_arena {
    nxp_arena_block *current;
    nxp_arena_block *blocks;            /* Head of all-blocks list */
    size_t           block_size;
    size_t           total_allocated;
    size_t           total_used;
} nxp_arena;

/* Create a new arena with the given default block size (0 = default 64K) */
[[nodiscard]] nxp_arena *nxp_arena_create(size_t block_size);

/* Destroy the arena, freeing all blocks */
void nxp_arena_destroy(nxp_arena *arena);

/* Allocate `size` bytes with `align` alignment from the arena */
[[nodiscard]] void *nxp_arena_alloc(nxp_arena *arena, size_t size, size_t align);

/* Reset the arena for reuse (keeps blocks allocated, resets pointers) */
void nxp_arena_reset(nxp_arena *arena);

/* Type-safe allocation macros */
#define nxp_arena_new(arena, T) \
    ((T *)nxp_arena_alloc((arena), sizeof(T), alignof(T)))

#define nxp_arena_new_array(arena, T, count) \
    ((T *)nxp_arena_alloc((arena), sizeof(T) * (count), alignof(T)))

#endif /* NXP_ARENA_H */
