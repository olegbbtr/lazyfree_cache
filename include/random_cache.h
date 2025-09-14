

#include <stdint.h>
#include "cache.h"


cache_t random_cache_new(size_t cache_size, mmap_impl_t mmap_impl, madv_impl_t madv_impl);

void random_cache_free(cache_t cache);

bool random_cache_write_lock(cache_t cache, 
                             cache_key_t key,
                             uint8_t **value);

bool random_cache_read_lock(cache_t cache, 
                            cache_key_t key,
                            uint8_t* head,
                            uint8_t **tail);

void random_cache_unlock(cache_t cache, bool drop);


static bool noop_lock_check(cache_t _) { return true; }


static struct cache_impl random_cache_impl = {
    .new = random_cache_new,
    .free = random_cache_free,
    .write_lock = random_cache_write_lock,
    .read_try_lock = random_cache_read_lock,
    .read_lock_check = noop_lock_check,
    .unlock = random_cache_unlock,
};

