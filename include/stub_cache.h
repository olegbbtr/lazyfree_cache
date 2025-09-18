#ifndef ARENA_CACHE_H
#define ARENA_CACHE_H

#include <stdint.h>

#include "cache.h"


lazyfree_cache_t stub_cache_new(size_t /*cache_size*/, size_t /*lazyfree_chunks*/, size_t /*anon_chunks*/, size_t /*disk_chunks*/);
void stub_cache_free(lazyfree_cache_t /*lfcache*/);

// == Read Lock API ==

void stub_cache_read_lock(lazyfree_cache_t /*cache*/, lazyfree_rlock_t* /*lock*/);
bool stub_cache_read_unlock(lazyfree_cache_t /*cache*/, lazyfree_rlock_t* /*lock*/, bool /*drop*/);

// == Write Lock API ==

void* stub_cache_write_lock(lazyfree_cache_t /*cache*/, lazyfree_rlock_t* /*lock*/);
void stub_cache_write_unlock(lazyfree_cache_t /*cache*/, bool /*drop*/);


#endif


