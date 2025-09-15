#include <inttypes.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "fallthrough_cache.h"
#include "cache.h"


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

static lazyfree_rlock_t try_read(struct fallthrough_cache* cache, 
                      uint64_t key,
                      uint8_t *value) {
    lazyfree_rlock_t lock;
    lock = cache->impl.read_lock(cache->cache, key);
    if (!lock.tail) {
        // Not found
        return lock;
    }
    
    value[0] = lock.head;
    memcpy(value + 1, lock.tail, cache->entry_size - 1);

    if (cache->impl.read_lock_check != NULL && 
        !cache->impl.read_lock_check(cache->cache, lock)) {
        printf("\nFALLTHROUGH READ LOCK CHECK FAILED\n");
    }
    return lock;
}


void ft_cache_get(ft_cache_t* cache, uint64_t key, uint8_t *value) {
    lazyfree_rlock_t lock = try_read(cache, key, value);
    if (lock.tail) {
        // Found
        cache->impl.read_unlock(cache->cache, lock, false);
        return;
    }

    // Cache miss
    cache->refill_cb(cache->refill_opaque, key, value);

    // Write lock
    uint8_t *page = cache->impl.write_upgrade(cache->cache, &lock);
    memcpy(page, value, cache->entry_size);
    cache->impl.write_unlock(cache->cache, false);
}


bool ft_cache_drop(ft_cache_t* cache, 
                            uint64_t key) {
    lazyfree_rlock_t lock;
    lock = cache->impl.read_lock(cache->cache, key);
    if (lock.tail) {
        cache->impl.read_unlock(cache->cache, lock, true);
        return true;
    }
    return false;
}
                  
void ft_cache_debug(ft_cache_t* cache, bool verbose) {
    struct lazyfree_stats stats = cache->impl.stats(cache->cache, verbose);
    printf("Lazyfree stats: total_pages=%zu, free_pages=%zu\n", 
           stats.total_pages, stats.free_pages);
}

