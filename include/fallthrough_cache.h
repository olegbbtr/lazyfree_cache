#include "cache.h"
#include "bitset.h"

struct fallthrough_cache {
    struct cache_impl impl;
    uint64_t entry_size;
    size_t entries_per_page;
    void (*repopulate)(void *opaque, uint64_t key, uint8_t *value);
    void *opaque;

    void *cache;

    bitset_t present;
    struct{ uint64_t key; uint64_t value; } *page_bitset_offset;
    size_t current_bitset_offset;

    size_t hits;
    size_t misses;
    bool verbose;
};

struct fallthrough_cache* fallthrough_cache_new(struct cache_impl impl, 
                                                size_t cache_size,
                                                size_t entry_size,
                                                void (*repopulate)(void *opaque, uint64_t key, uint8_t *value));

void fallthrough_cache_set_opaque(struct fallthrough_cache* cache, void *opaque);

void fallthrough_cache_free(struct fallthrough_cache* cache);

void fallthrough_cache_get(struct fallthrough_cache* cache, 
                           cache_key_t key, 
                           uint8_t *value);

// Returns true if found, false if not found.
bool fallthrough_cache_drop(struct fallthrough_cache* cache, cache_key_t key);


void fallthrough_cache_debug(struct fallthrough_cache* cache, bool verbose);
                

