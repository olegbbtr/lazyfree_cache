#include <inttypes.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "fallthrough_cache.h"
#include "cache.h"


void ft_cache_init(struct fallthrough_cache *cache, struct lazyfree_impl impl, 
                   ft_refill_t refill_cb, void *refill_opaque,
                   size_t num_entries, size_t entry_size) {
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

void ft_cache_get(ft_cache_t* cache, uint64_t key, uint8_t *value) {
    lazyfree_rlock_t lock = cache->impl.read_lock(cache->cache, key);
    assert(lock.head != NULL);
    if (LAZYFREE_LOCK_CHECK(lock)) {
        // Found
        
        lazyfree_read(value, lock, PAGE_SIZE-cache->entry_size, cache->entry_size);
        
        if (LAZYFREE_LOCK_CHECK(lock)) {
            // Check successful
            cache->impl.read_unlock(cache->cache, lock, false);
            return;
        }

        printf("\nFALLTHROUGH READ LOCK CHECK FAILED\n");
        exit(1);
    }

    // Cache miss
    cache->refill_cb(cache->refill_opaque, key, value);

    // Write lock
    uint8_t *page = cache->impl.write_upgrade(cache->cache, &lock);
    memcpy(page+PAGE_SIZE-cache->entry_size, value, cache->entry_size); // Write to the end of the page
    cache->impl.write_unlock(cache->cache, false);
}


bool ft_cache_drop(ft_cache_t* cache, 
                            uint64_t key) {
    lazyfree_rlock_t lock = cache->impl.read_lock(cache->cache, key);
    if (LAZYFREE_LOCK_CHECK(lock)) {
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

