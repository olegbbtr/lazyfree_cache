#include <inttypes.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "fallthrough_cache.h"


void ft_cache_init(struct fallthrough_cache *cache, struct lazyfree_impl impl, 
                   size_t num_entries, size_t entry_size,
                   ft_refill_t refill_cb, void *refill_opaque) {
    assert(entry_size <= PAGE_SIZE);
    memset(cache, 0, sizeof(*cache));
    cache->impl = impl;
    cache->entry_size = entry_size;
    cache->refill_cb = refill_cb;
    cache->refill_opaque = refill_opaque;

    cache->cache = impl.new(num_entries*PAGE_SIZE, impl.mmap_impl, impl.madv_impl);
    assert(cache->cache != NULL);
}

void ft_cache_destroy(struct fallthrough_cache* cache) {
    cache->impl.free(cache->cache);
}

static void put(struct fallthrough_cache* cache,
                uint64_t key,
                uint8_t *value) {
    uint8_t *page;

    cache->impl.write_lock(cache->cache, key, &page);
    memcpy(page, value, cache->entry_size);
    cache->impl.unlock(cache->cache, false);
}


static bool maybe_get(struct fallthrough_cache* cache, 
                      uint64_t key,
                      uint8_t *value) {

    uint8_t head;
    uint8_t *tail;
    bool ok = cache->impl.read_try_lock(cache->cache, key, &head, &tail);
    if (!ok) {
        // Not found
        return false;
    }
    value[0] = head;
    memcpy(value + 1, tail, cache->entry_size - 1);

    ok = true;
    if (cache->impl.read_lock_check != NULL && 
        !cache->impl.read_lock_check(cache->cache)) {
        printf("\nFALLTHROUGH READ LOCK CHECK FAILED\n");
        ok = false;
    }
    cache->impl.unlock(cache->cache, false);
    return ok;
}


void ft_cache_get(ft_cache_t* cache, uint64_t key, uint8_t *value) {
    if (maybe_get(cache, key, value)) {
        return;
    }
    cache->refill_cb(cache->refill_opaque, key, value);
    put(cache, key, value);
}


bool ft_cache_drop(ft_cache_t* cache, 
                            uint64_t key) {
    uint8_t head;
    uint8_t *tail;
    bool found = cache->impl.read_try_lock(cache->cache, key, &head, &tail);
    if (found) {
        cache->impl.unlock(cache->cache, true);
        return true;
    }
    return false;
}
                  
void ft_cache_report(ft_cache_t* cache) {
    cache->impl.stats(cache->cache, true);
    cache->impl.stats(cache->cache, false);
}

