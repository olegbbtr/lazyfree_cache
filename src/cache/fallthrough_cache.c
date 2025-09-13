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
                                                void (*repopulate)(void *opaque, uint64_t key, uint8_t *value)) {
    struct fallthrough_cache *cache = malloc(sizeof(struct fallthrough_cache));
    cache->impl = impl;
    cache->entry_size = entry_size;
    cache->entries_per_page = PAGE_SIZE / entry_size;
    cache->repopulate = repopulate;
    cache->opaque = NULL;

    size_t entries = cache_size / entry_size;
    cache->present = bitset_new(entries);


    cache->cache = impl.new(cache_size);
    assert(cache->cache != NULL);
    return cache;
}

void fallthrough_cache_set_opaque(struct fallthrough_cache* cache, void *opaque) {
    cache->opaque = opaque;
}

void fallthrough_cache_free(struct fallthrough_cache* cache) {
    cache->impl.free(cache->cache);
    free(cache);
}

static void indirect_bitset_put(struct fallthrough_cache* cache, 
                                uint64_t page_key, 
                                uint64_t page_offset, 
                                bool value) {
    size_t bitset_offset = hmget(cache->page_bitset_offset, page_key);
    if (bitset_offset == 0 && page_key != 0) {
        cache->current_bitset_offset += cache->entries_per_page;
        bitset_offset = cache->current_bitset_offset;
        hmput(cache->page_bitset_offset, page_key, bitset_offset);
    }
    if (cache->verbose) {
        printf("BITSET PUT %zu %zu: %d\n", bitset_offset, page_offset, value);
    }
   
    bitset_put(cache->present, bitset_offset + page_offset, value);
}

static bool indirect_bitset_get(struct fallthrough_cache* cache, 
                                uint64_t page_key, 
                                uint64_t page_offset) {
    size_t bitset_offset = hmget(cache->page_bitset_offset, page_key);
    if (bitset_offset == 0 && page_key != 0) {
        return false;
    }
    bool result = bitset_get(cache->present, bitset_offset + page_offset);
    if (cache->verbose) {
        printf("BITSET GET %zu %zu: %d\n", bitset_offset, page_offset, result);
    }
   
    return result;
}


static void put(struct fallthrough_cache* cache, 
                uint64_t page_key, 
                uint64_t page_offset, 
                uint8_t *value) {
    uint8_t *page;

    cache->impl.write_lock(cache->cache, page_key, &page);
    memcpy(&page[page_offset * cache->entry_size], value, cache->entry_size);
    cache->impl.unlock(cache->cache, false);

    indirect_bitset_put(cache, page_key, page_offset, true);
}


static bool maybe_get(struct fallthrough_cache* cache, 
               uint64_t page_key, 
               uint64_t page_offset, 
               uint8_t *value) {
    if (cache->verbose) {
        printf("\nFALLTHROUGH GET: %zu %zu\n", page_key, page_offset);
    }

    bool present = indirect_bitset_get(cache, page_key, page_offset);
    if (!present) {
        if (cache->verbose) {
            printf("\nFALLTHROUGH NOT PRESENT: %zu %zu\n", page_key, page_offset);
        }
        return false;
    }

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

    uint64_t page_key = key / cache->entries_per_page;
    uint64_t page_offset = (key % cache->entries_per_page);
    if (maybe_get(cache, page_key, page_offset, value)) {
        // printf("FALLTHROUGH GET OK: %zu %zu\n", page_key, page_offset);
        return;
    }
    cache->repopulate(cache->opaque, key, value);
    put(cache, page_key, page_offset, value);
}


bool fallthrough_cache_drop(struct fallthrough_cache* cache, 
                            uint64_t key) {
    uint64_t page_key = key / cache->entries_per_page;
    uint64_t page_offset = (key % cache->entries_per_page);
    
    indirect_bitset_put(cache, page_key, page_offset, false);

    uint8_t head;
    uint8_t *tail;
    bool found = cache->impl.read_try_lock(cache->cache, page_key, &head, &tail);
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
