#include <string.h>
#include <stdlib.h>

#include "arena_cache.h"

struct arena_cache {
    void *arena;
    size_t last_key;
    size_t key_offset;
};

lazyfree_cache_t arena_cache_new(size_t cache_size, lazyfree_mmap_impl_t mmap_impl, lazyfree_madv_impl_t) {
    struct arena_cache *cache = malloc(sizeof(struct arena_cache));
    memset(cache, 0, sizeof(*cache));
    
    cache->arena = mmap_impl(cache_size);
    return (lazyfree_cache_t) cache;
}

void arena_cache_free(lazyfree_cache_t lfcache) {
    struct arena_cache *cache = (struct arena_cache *) lfcache;
    free(cache->arena);
    free(cache);
}


// == Read Lock API ==

lazyfree_rlock_t arena_cache_read_lock(lazyfree_cache_t cache, lazyfree_key_t key) {
    return (lazyfree_rlock_t){ .tail = NULL };
}

void arena_cache_read_unlock(lazyfree_cache_t cache, lazyfree_rlock_t lock, bool drop) {
    return;
}

// == Write Lock API ==

// Allocates a new page in the cache.
void* arena_cache_alloc(lazyfree_cache_t cache, lazyfree_key_t key) {
    return NULL;
}

// Upgrade the read lock into write lock.
//   - Returns true if successfully upgraded.
//   - Returns false if the page was freshly allocated.
// After that, has to unlock with lazyfree_write_unlock.
void* arena_cache_upgrade_lock(lazyfree_cache_t cache, lazyfree_rlock_t lock, uint8_t **value) {
    return NULL;
}

// Unlocks the write lock.
// If drop is true, drops the page.
void arena_cache_write_unlock(lazyfree_cache_t cache, bool drop) {
    return;
}
