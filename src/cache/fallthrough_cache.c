#include <inttypes.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "fallthrough_cache.h"


#include "stb_ds.h"

struct fallthrough_cache* fallthrough_cache_new(struct cache_impl impl, 
                                                size_t cache_size,
                                                size_t entry_size,
                                                size_t entries_per_page,
                                                void (*repopulate)(void *opaque, uint64_t key, uint8_t *value)) {
    assert(entries_per_page * entry_size <= PAGE_SIZE);

    struct fallthrough_cache *cache = malloc(sizeof(struct fallthrough_cache));
    memset(cache, 0, sizeof(struct fallthrough_cache));
    cache->impl = impl;
    cache->entry_size = entry_size;
    cache->entries_per_page = entries_per_page;
    cache->repopulate = repopulate;
    cache->opaque = NULL;

    size_t entries = cache_size / entry_size;
    indirect_bitset_new(&cache->present, entries, cache->entries_per_page);


    cache->cache = impl.new(cache_size);
    assert(cache->cache != NULL);
    return cache;
}

void fallthrough_cache_set_opaque(struct fallthrough_cache* cache, void *opaque) {
    cache->opaque = opaque;
}

void fallthrough_cache_free(struct fallthrough_cache* cache) {
    cache->impl.free(cache->cache);
    indirect_bitset_destroy(&cache->present);
    free(cache);
}

static void put(struct fallthrough_cache* cache,
                uint64_t key,
                uint8_t *value) {
    uint64_t page_key = key / cache->entries_per_page;
    uint64_t page_offset = key % cache->entries_per_page;
    
    uint8_t *page;

    cache->impl.write_lock(cache->cache, page_key, &page);
    memcpy(&page[page_offset * cache->entry_size], value, cache->entry_size);
    cache->impl.unlock(cache->cache, false);

    indirect_bitset_put(&cache->present, key, true);
}


static bool maybe_get(struct fallthrough_cache* cache, 
                      uint64_t key,
                      uint8_t *value) {
    if (cache->verbose) {
        printf("\nFALLTHROUGH GET: %zu\n", key);
    }

    bool present = indirect_bitset_get(&cache->present, key);
    if (!present) {
        return false;
    }
    
    uint64_t page_key = key / cache->entries_per_page;
    uint64_t page_offset = key % cache->entries_per_page;

    uint8_t head;
    uint8_t *tail;
    bool ok = cache->impl.read_try_lock(cache->cache, page_key, &head, &tail);
    if (!ok) {
        // Not found
        if (cache->verbose) {
            printf("\nFALLTHROUGH NOT FOUND: %zu %zu\n", page_key, page_offset);
        }
        return false;
    }
    if (page_offset == 0) {
        value[0] = head;
        memcpy(value + 1, tail, cache->entry_size - 1);
    } else {
        size_t tail_offset = page_offset * cache->entry_size - 1;
        memcpy(value, &tail[tail_offset], cache->entry_size);
    }
    
    ok = true;
    if (cache->impl.read_lock_check != NULL && 
        !cache->impl.read_lock_check(cache->cache)) {
        printf("\nFALLTHROUGH READ LOCK CHECK FAILED\n");
        ok = false;
    }
    cache->impl.unlock(cache->cache, false);
    return ok;
}


void fallthrough_cache_get(struct fallthrough_cache* cache, uint64_t key, uint8_t *value) {

    if (maybe_get(cache, key, value)) {
        // printf("FALLTHROUGH GET OK: %zu %zu\n", page_key, page_offset);
        return;
    }
    cache->repopulate(cache->opaque, key, value);
    put(cache, key, value);
}


bool fallthrough_cache_drop(struct fallthrough_cache* cache, 
                            uint64_t key) {
    indirect_bitset_put(&cache->present, key, false);

    uint8_t head;
    uint8_t *tail;
    bool found = cache->impl.read_try_lock(cache->cache, key / cache->entries_per_page, &head, &tail);
    if (found) {
        cache->impl.unlock(cache->cache, true);
        return true;
    }
    return false;
}

                             
void fallthrough_cache_debug(struct fallthrough_cache* cache, bool verbose) {
    cache->verbose = verbose;
    cache->impl.debug(cache->cache, verbose);
}
