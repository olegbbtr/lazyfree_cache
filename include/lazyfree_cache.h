#ifndef LAZYFREE_CACHE_H
#define LAZYFREE_CACHE_H

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

#include "cache.h"

// == Core API ==

// Cache capacity is in bytes. 
// If the cache is full, it will start evicting random chunks.
lazyfree_cache_t lazyfree_cache_new(size_t cache_capacity, 
                                    lazyfree_mmap_impl_t mmap_impl, 
                                    lazyfree_madv_impl_t madv_impl);

// Call after done using the cache.
void lazyfree_cache_free(lazyfree_cache_t cache);

// == Optimistic Read Lock API ==
// This might look complicated, but it is necessary to support
// zero-copy reads.

// Try to take an optimistic read lock.
// Returns the handle with two fields:
//    head - page[0]
//    tail - ptr to page[1:PAGE_SIZE]
// If the page is not present in the cache, tail is NULL. 
lazyfree_rlock_t lazyfree_read_try_lock(lazyfree_cache_t cache, lazyfree_key_t key);

// Check if the read lock is still valid.
bool lazyfree_read_lock_check(lazyfree_cache_t cache, lazyfree_rlock_t lock);

// Read unlock
void lazyfree_read_unlock(lazyfree_cache_t cache, lazyfree_rlock_t lock, bool drop);

// == Write Lock API ==

// Allocates a new page in the cache.
// Must not be called for existing entries, instead use lazyfree_upgrade_lock.
uint8_t *lazyfree_write_alloc(lazyfree_cache_t cache, lazyfree_key_t key);

// Upgrade the read lock into write lock.
//   - Returns true if successfully upgraded.
//   - Returns false if the page was freshly allocated.
// After that, has to unlock with lazyfree_write_unlock.
bool lazyfree_upgrade_lock(lazyfree_cache_t cache, lazyfree_rlock_t lock, uint8_t **value);

// Unlocks the write lock.
// If drop is true, drops the page.
void lazyfree_write_unlock(lazyfree_cache_t cache, bool drop);


// == Extra API ==

// Returns stats and sets verbose mode.
struct lazyfree_stats lazyfree_fetch_stats(lazyfree_cache_t cache, bool verbose);


// == Generic Implementation ==

inline struct lazyfree_impl lazyfree_impl() {
    struct lazyfree_impl impl = {
        .new = lazyfree_cache_new,
        .free = lazyfree_cache_free,

        .read_try_lock = lazyfree_read_try_lock,
        .read_lock_check = lazyfree_read_lock_check,
        .read_unlock = lazyfree_read_unlock,

        .upgrade_lock = lazyfree_upgrade_lock,
        .alloc = lazyfree_write_alloc,
        .write_unlock = lazyfree_write_unlock,

        .stats = lazyfree_fetch_stats,
    
        .mmap_impl = lazyfree_mmap_anon,
        .madv_impl = lazyfree_madv_free,
    };    
    return impl;
}

// Anonymous storage is the same: no MADV_FREE, no lock failures.
inline struct lazyfree_impl lazyfree_anon_impl() {
    struct lazyfree_impl impl = lazyfree_impl();
    impl.madv_impl = NULL;
    impl.read_lock_check = NULL;
    return impl;
}

// Store pages in files.
inline struct lazyfree_impl lazyfree_disk_impl() {
    struct lazyfree_impl impl = lazyfree_anon_impl();
    impl.mmap_impl = lazyfree_mmap_file;
    return impl;
}

#endif
