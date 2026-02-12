/*
 * NXP Open-Addressing Hash Map
 *
 * Uses Robin Hood hashing with linear probing.
 * Keys are uint64_t (suitable for ConnectionID, StreamID lookups).
 */
#ifndef NXP_HASH_MAP_H
#define NXP_HASH_MAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct nxp_hash_entry {
    uint64_t key;
    void    *value;
    uint32_t psl;          /* Probe Sequence Length (0 = empty) */
} nxp_hash_entry;

typedef struct nxp_hash_map {
    nxp_hash_entry *entries;
    uint32_t        capacity;
    uint32_t        count;
    uint32_t        mask;      /* capacity - 1 (power of 2) */
} nxp_hash_map;

/* Create a hash map with initial capacity (rounded up to power of 2) */
[[nodiscard]] nxp_hash_map *nxp_hash_map_create(uint32_t initial_capacity);

/* Destroy the hash map (does NOT free values) */
void nxp_hash_map_destroy(nxp_hash_map *map);

/* Insert or update. Returns the previous value, or nullptr if new key. */
void *nxp_hash_map_put(nxp_hash_map *map, uint64_t key, void *value);

/* Lookup. Returns nullptr if not found. */
[[nodiscard]] void *nxp_hash_map_get(const nxp_hash_map *map, uint64_t key);

/* Remove and return the value, or nullptr if not found. */
void *nxp_hash_map_remove(nxp_hash_map *map, uint64_t key);

/* Check if key exists */
[[nodiscard]] bool nxp_hash_map_contains(const nxp_hash_map *map, uint64_t key);

/* Get number of entries */
[[nodiscard]] static inline uint32_t nxp_hash_map_count(const nxp_hash_map *map) {
    return map->count;
}

/* Iteration callback: return false to stop iterating */
typedef bool (*nxp_hash_map_iter_fn)(uint64_t key, void *value, void *user_data);

/* Iterate over all entries */
void nxp_hash_map_foreach(const nxp_hash_map *map, nxp_hash_map_iter_fn fn, void *user_data);

#endif /* NXP_HASH_MAP_H */
