#ifndef LAZYFREE_CACHE_H
#define LAZYFREE_CACHE_H

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

#include "cache.h"

// === Lazyfree Cache API ===

// == Core API ==

// Create new cache.
// Cache capacity is in bytes. 
lazyfree_cache_t lazyfree_cache_new(size_t cache_capacity);

void lazyfree_cache_free(lazyfree_cache_t cache);

// == Optimistic Read Lock API ==
// This might look complicated, but it is necessary to support
// zero-copy reads.

// Take an optimistic read lock.
// Returns the handle with two fields:
//    head - page[0]
//    tail - ptr to page[1:PAGE_SIZE] if found, NULL otherwise
// The lock can be upgraded to a write lock.
lazyfree_rlock_t lazyfree_read_lock(lazyfree_cache_t cache, lazyfree_key_t key);

// Check the lock is still valid.
// Needs to be used after every read from tail, to verify the page has not been dropped.
bool lazyfree_read_lock_check(lazyfree_cache_t cache, lazyfree_rlock_t lock);

// Unlock the read lock.
// If drop is true, drops the page.
void lazyfree_read_unlock(lazyfree_cache_t cache, lazyfree_rlock_t lock, bool drop);

// == Write Lock API ==
// Only one page can be locked for write at the time.
// There are two ways to get a write lock:

// Allocates a new page in the cache.
// Must not be called for existing keys, instead use upgrade.
// Returns ptr to page[0:PAGE_SIZE]
void* lazyfree_write_alloc(lazyfree_cache_t cache, lazyfree_key_t key);

// Upgrade the read lock into write lock.
// Returns ptr to page[0:PAGE_SIZE]
void* lazyfree_write_upgrade(lazyfree_cache_t cache, lazyfree_rlock_t* lock);

// Unlocks the write lock.
// If drop is true, drops the page.
void lazyfree_write_unlock(lazyfree_cache_t cache, bool drop);

// == Behavior details ==
// If the cache is full, it will start evicting random chunks.
//
// Provides RWLock semantics:
//  Can lock any number of pages for read.
//  Can lock only one page for write.
//
// Must call all functions from the critical section.

// == Extra API ==

// Crete cache with custom memory implementation.
lazyfree_cache_t lazyfree_cache_new_ex(size_t cache_capacity, 
                                       lazyfree_mmap_impl_t mmap_impl, 
                                       lazyfree_madv_impl_t madv_impl);

// Returns stats and remembers verbosity.
struct lazyfree_stats lazyfree_fetch_stats(lazyfree_cache_t cache, bool verbose);

// Run tests.
void lazyfree_cache_tests();

#endif
