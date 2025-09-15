#ifndef ARENA_CACHE_H
#define ARENA_CACHE_H

#include <stdint.h>

#include "cache.h"



lazyfree_cache_t arena_cache_new(size_t cache_size, lazyfree_mmap_impl_t mmap_impl, lazyfree_madv_impl_t madv_impl);

void arena_cache_free(lazyfree_cache_t cache);

// == Read Lock API ==

lazyfree_rlock_t arena_cache_read_lock(lazyfree_cache_t cache, lazyfree_key_t key);

void arena_cache_read_unlock(lazyfree_cache_t cache, lazyfree_rlock_t lock, bool drop);

// == Write Lock API ==

// Allocates a new page in the cache.
uint8_t *arena_cache_alloc(lazyfree_cache_t cache, lazyfree_key_t key);

// Upgrade the read lock into write lock.
//   - Returns true if successfully upgraded.
//   - Returns false if the page was freshly allocated.
// After that, has to unlock with lazyfree_write_unlock.
bool arena_cache_upgrade_lock(lazyfree_cache_t cache, lazyfree_rlock_t lock, uint8_t **value);

// Unlocks the write lock.
// If drop is true, drops the page.
void arena_cache_write_unlock(lazyfree_cache_t cache, bool drop);




static struct lazyfree_impl arena_cache_impl = {
    .new = arena_cache_new,
    .free = arena_cache_free,

    .read_try_lock = arena_cache_read_lock,
    .read_lock_check = NULL,
    .read_unlock = arena_cache_read_unlock,

    .upgrade_lock = arena_cache_upgrade_lock,
    .alloc = arena_cache_alloc,
    .write_unlock = arena_cache_write_unlock,

    .mmap_impl = lazyfree_mmap_anon,
    .madv_impl = NULL,
};

#endif


