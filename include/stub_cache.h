#ifndef ARENA_CACHE_H
#define ARENA_CACHE_H

#include <stdint.h>

#include "cache.h"


lazyfree_cache_t stub_cache_new(size_t /*cache_size*/, lazyfree_mmap_impl_t /*mmap_impl*/, lazyfree_madv_impl_t /*madv_impl*/);
void stub_cache_free(lazyfree_cache_t /*lfcache*/);

// == Read Lock API ==

void stub_cache_read_lock(lazyfree_cache_t /*cache*/, lazyfree_rlock_t* /*lock*/);
bool stub_cache_read_unlock(lazyfree_cache_t /*cache*/, lazyfree_rlock_t* /*lock*/, bool /*drop*/);

// == Write Lock API ==

void* stub_cache_write_lock(lazyfree_cache_t /*cache*/, lazyfree_rlock_t* /*lock*/);
void stub_cache_write_unlock(lazyfree_cache_t /*cache*/, bool /*drop*/);


#endif


