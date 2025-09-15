#ifndef LAZYFREE_CACHE_H
#define LAZYFREE_CACHE_H

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

#include "cache.h"


// Cache capacity is in bytes. 
// If the cache is full, it will start evicting random chunks.
lazyfree_cache_t lazyfree_cache_new(size_t cache_capacity, lazyfree_mmap_impl_t mmap_impl, lazyfree_madv_impl_t madv_impl);

// Call after done using the cache.
void lazyfree_cache_free(lazyfree_cache_t cache);


// == Low-level API ==
// These are zero-copy.

// Lock the cache to write to the key.
//  - Sets 'value' to point at the page inside the cache.
//  - Next call must be lazyfree_cache_unlock.
//    Pointers are valid until then.
//  - Returns true if the key has existing data.
//    Returns false if the key was just allocated and is empty.
bool lazyfree_write_lock(lazyfree_cache_t cache, 
                         lazyfree_key_t key,
                         uint8_t **value);
                           

//  Optimistically lock the cache to read read from the page.
//  - Sets 'head' to the first byte of the page.
//  - Sets 'tail' to point at the [1:PAGE_SIZE] of the page.
//    After reading from the tail, must call lazyfree_cache_read_lock_check 
//    to see if the lock is still valid.
//  - Next call must be lazyfree_cache_unlock.
//    Pointers are valid until then.
//  - Returns true if the key has existing data.
//    Returns false if the key is empty.
bool lazyfree_read_try_lock(lazyfree_cache_t cache, 
                            lazyfree_key_t key,
                            uint8_t *head,
                            uint8_t **tail);

// Check if the read lock is still valid.
bool lazyfree_read_lock_check(lazyfree_cache_t cache);

// Turn the read lock into write lock.
void lazyfree_upgrade_lock(lazyfree_cache_t cache);

// Unlock the cache.
//  - If 'drop' is true, the key is dropped from the cache.
void lazyfree_unlock(lazyfree_cache_t cache, bool drop);


// == Extra API ==

// Returns stats and sets verbose mode.
struct lazyfree_stats lazyfree_fetch_stats(lazyfree_cache_t cache, bool verbose);



// == Generic Implementation ==

inline struct lazyfree_impl lazyfree_impl() {
    struct lazyfree_impl impl = {
        .new = lazyfree_cache_new,
        .free = lazyfree_cache_free,
        .write_lock = lazyfree_write_lock,
        .read_try_lock = lazyfree_read_try_lock,
        .read_lock_check = lazyfree_read_lock_check,
        .unlock = lazyfree_unlock,
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
