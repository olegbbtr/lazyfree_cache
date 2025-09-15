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
lazyfree_rlock_t lazyfree_read_lock(lazyfree_cache_t cache, lazyfree_key_t key);

// Check if the read lock is still valid.
bool lazyfree_read_lock_check(lazyfree_cache_t cache, lazyfree_rlock_t lock);

// Read unlock
void lazyfree_read_unlock(lazyfree_cache_t cache, lazyfree_rlock_t lock, bool drop);

// == Write Lock API ==

// Allocates a new page in the cache.
// Must not be called for existing entries, instead use lazyfree_upgrade_lock.
void* lazyfree_write_alloc(lazyfree_cache_t cache, lazyfree_key_t key);

// Upgrade the read lock into write lock.
// Returns ptr to the page.
// Updates the present flag in the lock.
void* lazyfree_write_upgrade(lazyfree_cache_t cache, lazyfree_rlock_t* lock);

// Unlocks the write lock.
// If drop is true, drops the page.
void lazyfree_write_unlock(lazyfree_cache_t cache, bool drop);


// == Extra API ==

// Returns stats and sets verbose mode.
struct lazyfree_stats lazyfree_fetch_stats(lazyfree_cache_t cache, bool verbose);

void lazyfree_tests();

#endif
