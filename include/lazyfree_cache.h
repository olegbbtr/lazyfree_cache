#ifndef LAZYFREE_CACHE_H
#define LAZYFREE_CACHE_H

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

#include "cache.h"

// ================================ Core API =====================================

lazyfree_cache_t lazyfree_cache_new(size_t cache_capacity);

void lazyfree_cache_free(lazyfree_cache_t cache);

// ================================ Read Lock API ================================
// This might look complex, but it is necessary to support zero-copy reads.

// Take an optimistic read lock.
lazyfree_rlock_t lazyfree_read_lock(lazyfree_cache_t cache, lazyfree_key_t key);

// If drop is true, drops the page.
void lazyfree_read_unlock(lazyfree_cache_t cache, lazyfree_rlock_t lock, bool drop);

// ================================ Write Lock API ===============================
// Only one page can be locked for write at the time.
// There are two ways to get a write lock:

// Can be called only for new keys.
void* lazyfree_write_alloc(lazyfree_cache_t cache, lazyfree_key_t key);

// Upgrade the read lock into write lock.
// LAZYFREE_LOCK_CHECK(lock) can be used to verify if the page still has the same data.
void* lazyfree_write_upgrade(lazyfree_cache_t cache, lazyfree_rlock_t* lock);

// If drop is true, drops the page.
void lazyfree_write_unlock(lazyfree_cache_t cache, bool drop);

// ================================ Behavior details =============================
// If the cache is full, it will start evicting random chunks.
//
// Provides RWLock semantics:
//  Can lock any number of pages for read.
//  Can lock only one page for write.
//
// Must call all functions from the critical section.

// ================================ Extra API ===================================

// Crete cache with custom memory implementation.
lazyfree_cache_t lazyfree_cache_new_ex(size_t cache_capacity, 
                                       lazyfree_mmap_impl_t mmap_impl, 
                                       lazyfree_madv_impl_t madv_impl);

// Returns stats and remembers verbosity.
struct lazyfree_stats lazyfree_fetch_stats(lazyfree_cache_t cache, bool verbose);

// Run tests.
void lazyfree_cache_tests();

#endif
