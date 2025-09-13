#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "fallthrough_cache.h"

struct fallthrough_cache* fallthrough_cache_new(struct fallthrough_cache_opts opts) {
    struct fallthrough_cache *cache = malloc(sizeof(struct fallthrough_cache));
    cache->opts = opts;
    cache->cache = malloc(opts.cache_sizeof);
    opts.cache_init(cache->cache);
    assert(PAGE_SIZE % cache->opts.entry_size == 0);
    return cache;
}

void fallthrough_cache_free(struct fallthrough_cache* cache) {
    cache->opts.cache_destroy(cache->cache);
    free(cache->cache);
    free(cache);
}


static void put(struct fallthrough_cache* cache, uint64_t key, uint8_t *value) {
    uint64_t page_key = key / PAGE_SIZE;
    uint64_t page_offset = (key % PAGE_SIZE) * cache->opts.entry_size;

    uint8_t page[PAGE_SIZE];

    cache->opts.cache_write_lock(cache->cache, page_key, &page);
    memcpy(&page[page_offset], value, cache->opts.entry_size);
    cache->opts.cache_unlock(cache->cache, false);
}


static bool maybe_get(struct fallthrough_cache* cache, 
               uint64_t key, 
               uint8_t *value) {
    uint64_t page_id = key / PAGE_SIZE;
    uint64_t page_offset = (key % PAGE_SIZE) * cache->opts.entry_size;

    uint8_t head;
    uint8_t tail[PAGE_SIZE-1];
    bool ok = cache->opts.cache_read_lock_opt(cache->cache, key, &head, &tail);
    if (!ok) {
        // Not found
        return false;
    }
    if (page_offset == 0) {
        value[0] = head;
        memcpy(value + 1, tail, cache->opts.entry_size - 1);
    } else {
        memcpy(value, &tail[page_offset - 1], cache->opts.entry_size);
    }
    if (cache->opts.cache_read_lock_check(cache->cache)) {
        ok = true;
    }
    cache->opts.cache_unlock(cache->cache, false);
    return ok;
}


bool fallthrough_cache_get(struct fallthrough_cache* cache, uint64_t key, uint8_t *value) {
    if (maybe_get(cache, key, value)) {
        return true;
    }
    bool ok = cache->opts.repopulate(cache->opts.repopulate_data, key, value);
    if (!ok) {
        return false;
    }
    put(cache, key, value);
    return true;
}


void fallthrough_cache_drop(struct fallthrough_cache* cache, 
                            uint64_t key) {
    uint64_t page_key = key / PAGE_SIZE;
    cache->opts.cache_read_lock_opt(cache->cache, page_key, NULL, NULL);
    cache->opts.cache_unlock(cache->cache, true);
}

                             
