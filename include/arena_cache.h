

#include <stdint.h>

#include "cache.h"



lazyfree_cache_t arena_cache_new(size_t cache_size, lazyfree_mmap_impl_t mmap_impl, lazyfree_madv_impl_t madv_impl);

void arena_cache_free(lazyfree_cache_t cache);

bool arena_cache_write_lock(lazyfree_cache_t cache, 
                             lazyfree_key_t key,
                             uint8_t **value);

bool arena_cache_read_lock(lazyfree_cache_t cache, 
                            lazyfree_key_t key,
                            uint8_t* head,
                            uint8_t **tail);

void arena_cache_unlock(lazyfree_cache_t cache, bool drop);




static struct lazyfree_impl arena_cache_impl = {
    .new = arena_cache_new,
    .free = arena_cache_free,

    .write_lock = arena_cache_write_lock,
    .read_try_lock = arena_cache_read_lock,
    .read_lock_check = NULL,
    .unlock = arena_cache_unlock,


};

