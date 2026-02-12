/*
 * NXP Hash Map - Robin Hood Open-Addressing Implementation
 */
#include "hash_map.h"

#include <stdlib.h>
#include <string.h>

#define NXP_HASH_MAP_LOAD_FACTOR  75   /* Resize at 75% */
#define NXP_HASH_MAP_MIN_CAPACITY 16

static uint32_t next_power_of_2(uint32_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

/* FNV-1a style hash for uint64_t */
static uint32_t hash_u64(uint64_t key) {
    key = (~key) + (key << 21);
    key = key ^ (key >> 24);
    key = (key + (key << 3)) + (key << 8);
    key = key ^ (key >> 14);
    key = (key + (key << 2)) + (key << 4);
    key = key ^ (key >> 28);
    key = key + (key << 31);
    return (uint32_t)key;
}

nxp_hash_map *nxp_hash_map_create(uint32_t initial_capacity) {
    if (initial_capacity < NXP_HASH_MAP_MIN_CAPACITY) {
        initial_capacity = NXP_HASH_MAP_MIN_CAPACITY;
    }
    initial_capacity = next_power_of_2(initial_capacity);

    nxp_hash_map *map = (nxp_hash_map *)malloc(sizeof(nxp_hash_map));
    if (map == nullptr) {
        return nullptr;
    }

    map->entries = (nxp_hash_entry *)calloc(initial_capacity, sizeof(nxp_hash_entry));
    if (map->entries == nullptr) {
        free(map);
        return nullptr;
    }

    map->capacity = initial_capacity;
    map->count    = 0;
    map->mask     = initial_capacity - 1;

    return map;
}

void nxp_hash_map_destroy(nxp_hash_map *map) {
    if (map == nullptr) return;
    free(map->entries);
    free(map);
}

static bool hash_map_resize(nxp_hash_map *map) {
    uint32_t new_cap = map->capacity * 2;
    nxp_hash_entry *new_entries = (nxp_hash_entry *)calloc(new_cap, sizeof(nxp_hash_entry));
    if (new_entries == nullptr) {
        return false;
    }

    uint32_t new_mask = new_cap - 1;

    /* Reinsert all existing entries */
    for (uint32_t i = 0; i < map->capacity; i++) {
        if (map->entries[i].psl == 0) continue;

        nxp_hash_entry entry = map->entries[i];
        entry.psl = 1;

        uint32_t idx = hash_u64(entry.key) & new_mask;
        for (;;) {
            if (new_entries[idx].psl == 0) {
                new_entries[idx] = entry;
                break;
            }
            /* Robin Hood: swap if current entry has shorter PSL */
            if (new_entries[idx].psl < entry.psl) {
                nxp_hash_entry tmp = new_entries[idx];
                new_entries[idx] = entry;
                entry = tmp;
            }
            entry.psl++;
            idx = (idx + 1) & new_mask;
        }
    }

    free(map->entries);
    map->entries  = new_entries;
    map->capacity = new_cap;
    map->mask     = new_mask;
    return true;
}

void *nxp_hash_map_put(nxp_hash_map *map, uint64_t key, void *value) {
    /* Resize check */
    if (map->count * 100 >= map->capacity * NXP_HASH_MAP_LOAD_FACTOR) {
        if (!hash_map_resize(map)) {
            return nullptr; /* Failed to resize */
        }
    }

    nxp_hash_entry entry = { .key = key, .value = value, .psl = 1 };
    void *old_value = nullptr;

    uint32_t idx = hash_u64(key) & map->mask;
    for (;;) {
        if (map->entries[idx].psl == 0) {
            /* Empty slot */
            map->entries[idx] = entry;
            map->count++;
            return old_value;
        }

        if (map->entries[idx].key == key) {
            /* Key exists - update value */
            old_value = map->entries[idx].value;
            map->entries[idx].value = value;
            return old_value;
        }

        /* Robin Hood: swap if current has shorter PSL */
        if (map->entries[idx].psl < entry.psl) {
            nxp_hash_entry tmp = map->entries[idx];
            map->entries[idx] = entry;
            entry = tmp;
        }

        entry.psl++;
        idx = (idx + 1) & map->mask;
    }
}

void *nxp_hash_map_get(const nxp_hash_map *map, uint64_t key) {
    uint32_t idx = hash_u64(key) & map->mask;

    for (uint32_t psl = 1; ; psl++) {
        if (map->entries[idx].psl == 0) {
            return nullptr; /* Empty slot = not found */
        }
        if (map->entries[idx].psl < psl) {
            return nullptr; /* Robin Hood guarantee: won't be further */
        }
        if (map->entries[idx].key == key) {
            return map->entries[idx].value;
        }
        idx = (idx + 1) & map->mask;
    }
}

void *nxp_hash_map_remove(nxp_hash_map *map, uint64_t key) {
    uint32_t idx = hash_u64(key) & map->mask;

    for (uint32_t psl = 1; ; psl++) {
        if (map->entries[idx].psl == 0) {
            return nullptr;
        }
        if (map->entries[idx].psl < psl) {
            return nullptr;
        }
        if (map->entries[idx].key == key) {
            void *old = map->entries[idx].value;

            /* Backward shift deletion */
            for (;;) {
                uint32_t next = (idx + 1) & map->mask;
                if (map->entries[next].psl <= 1) {
                    map->entries[idx].psl = 0;
                    map->entries[idx].value = nullptr;
                    break;
                }
                map->entries[idx] = map->entries[next];
                map->entries[idx].psl--;
                idx = next;
            }

            map->count--;
            return old;
        }
        idx = (idx + 1) & map->mask;
    }
}

bool nxp_hash_map_contains(const nxp_hash_map *map, uint64_t key) {
    return nxp_hash_map_get(map, key) != nullptr;
}

void nxp_hash_map_foreach(const nxp_hash_map *map, nxp_hash_map_iter_fn fn, void *user_data) {
    for (uint32_t i = 0; i < map->capacity; i++) {
        if (map->entries[i].psl > 0) {
            if (!fn(map->entries[i].key, map->entries[i].value, user_data)) {
                return;
            }
        }
    }
}
