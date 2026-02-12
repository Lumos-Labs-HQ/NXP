/*
 * NXP Arena Allocator - Implementation
 */
#include "arena.h"

#include <stdlib.h>
#include <string.h>

static size_t align_up(size_t value, size_t align) {
    return (value + align - 1) & ~(align - 1);
}

static nxp_arena_block *arena_block_new(size_t capacity) {
    nxp_arena_block *block = (nxp_arena_block *)malloc(
        sizeof(nxp_arena_block) + capacity
    );
    if (block == nullptr) {
        return nullptr;
    }
    block->next     = nullptr;
    block->capacity = capacity;
    block->used     = 0;
    return block;
}

nxp_arena *nxp_arena_create(size_t block_size) {
    if (block_size == 0) {
        block_size = NXP_ARENA_DEFAULT_BLOCK_SIZE;
    }

    nxp_arena *arena = (nxp_arena *)malloc(sizeof(nxp_arena));
    if (arena == nullptr) {
        return nullptr;
    }

    nxp_arena_block *first = arena_block_new(block_size);
    if (first == nullptr) {
        free(arena);
        return nullptr;
    }

    arena->current         = first;
    arena->blocks          = first;
    arena->block_size      = block_size;
    arena->total_allocated = block_size;
    arena->total_used      = 0;

    return arena;
}

void nxp_arena_destroy(nxp_arena *arena) {
    if (arena == nullptr) {
        return;
    }

    nxp_arena_block *block = arena->blocks;
    while (block != nullptr) {
        nxp_arena_block *next = block->next;
        free(block);
        block = next;
    }

    free(arena);
}

void *nxp_arena_alloc(nxp_arena *arena, size_t size, size_t align) {
    if (arena == nullptr || size == 0) {
        return nullptr;
    }

    nxp_arena_block *block = arena->current;
    size_t aligned_offset = align_up(block->used, align);

    /* Try current block */
    if (aligned_offset + size <= block->capacity) {
        void *ptr = block->data + aligned_offset;
        block->used = aligned_offset + size;
        arena->total_used += (aligned_offset - block->used + size);
        return ptr;
    }

    /* Need a new block - make it large enough for the requested size */
    size_t new_cap = arena->block_size;
    size_t needed = align_up(0, align) + size;
    if (needed > new_cap) {
        new_cap = needed;
    }

    nxp_arena_block *new_block = arena_block_new(new_cap);
    if (new_block == nullptr) {
        return nullptr;
    }

    /* Link new block */
    new_block->next  = arena->blocks;
    arena->blocks    = new_block;
    arena->current   = new_block;
    arena->total_allocated += new_cap;

    size_t off = align_up(0, align);
    new_block->used = off + size;
    arena->total_used += off + size;

    return new_block->data + off;
}

void nxp_arena_reset(nxp_arena *arena) {
    if (arena == nullptr) {
        return;
    }

    /* Reset all blocks to unused */
    nxp_arena_block *block = arena->blocks;
    while (block != nullptr) {
        block->used = 0;
        block = block->next;
    }

    arena->current    = arena->blocks;
    arena->total_used = 0;
}
