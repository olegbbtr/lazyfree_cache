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

bool arena_cache_write_lock(lazyfree_cache_t lfcache, 
                             lazyfree_key_t key,
                             uint8_t **value) {
   return false;
}

bool arena_cache_read_lock(lazyfree_cache_t lfcache, 
                            lazyfree_key_t key,
                            uint8_t* head,
                            uint8_t **tail) {
    return false;
}

void arena_cache_unlock(lazyfree_cache_t lfcache, bool drop) {
    return;
}
