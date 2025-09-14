#include "cache.h"
#include "bitset.h"


typedef void (*refill_cb_t)(void *opaque, uint64_t key, uint8_t *value);

struct fallthrough_cache {
    struct cache_impl impl;
    uint64_t entry_size;
    size_t entries_per_page;
    refill_cb_t refill_cb;
    void *opaque;

    void *cache;

    // struct indirect_bitset present;

    size_t hits;
    size_t misses;
    bool verbose;
};

struct fallthrough_cache* fallthrough_cache_new(struct cache_impl impl, 
                                                size_t cache_size,
                                                size_t entry_size,
                                                size_t entries_per_page,
                                                refill_cb_t refill_cb);

void fallthrough_cache_set_opaque(struct fallthrough_cache* cache, void *opaque);

void fallthrough_cache_free(struct fallthrough_cache* cache);

void fallthrough_cache_get(struct fallthrough_cache* cache, 
                           cache_key_t key, 
                           uint8_t *value);

// Returns true if found, false if not found.
bool fallthrough_cache_drop(struct fallthrough_cache* cache, cache_key_t key);


void fallthrough_cache_debug(struct fallthrough_cache* cache, bool verbose);
                

