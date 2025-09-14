#include "random_cache.h"
#include <stdlib.h>

typedef struct random_cache {
    void *arena;
    cache_key_t last_key;
    size_t key_offset;
} random_cache;



cache_t random_cache_new(size_t cache_size, mmap_impl_t mmap_impl, madv_impl_t _) {
    random_cache *cache = malloc(sizeof(random_cache));
    cache->arena = mmap_impl(cache_size);
    return cache;
}

void random_cache_free(cache_t cache) {
    random_cache *rcache = (random_cache *)cache;
    free(rcache->arena);
    free(rcache);
}

bool random_cache_write_lock(cache_t cache, 
                             cache_key_t key,
                             uint8_t **value) {
   return false;
}

bool random_cache_read_lock(cache_t cache, 
                            cache_key_t key,
                            uint8_t* head,
                            uint8_t **tail) {
    return false;
}

void random_cache_unlock(cache_t cache, bool drop) {
    return;
}
