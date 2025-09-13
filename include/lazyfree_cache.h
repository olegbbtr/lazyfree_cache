
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

#include "cache.h"


// Cache capacity is in bytes. 
// It should be significantly higher than actual available memory.
// If the cache is full, it will start evicting random entries.
cache_t lazyfree_cache_new(size_t cache_capacity);

// Call after done using the cache.
void lazyfree_cache_free(cache_t cache);


// == Low-level API ==
// These are zero-copy.

// Lock the cache to write to the key.
//  - Sets 'value' to point at the page inside the cache.
//  - Next call must be lazyfree_cache_unlock.
//    Pointers are valid until then.
//  - Returns true if the key has existing data.
//    Returns false if the key was just allocated and is empty.
bool lazyfree_cache_write_lock(cache_t cache, 
                               cache_key_t key,
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
bool lazyfree_cache_read_try_lock(cache_t cache, 
                                  cache_key_t key,
                                  uint8_t *head,
                                  uint8_t **tail);

// Check if the read lock is still valid.
bool lazyfree_cache_read_lock_check(cache_t cache);

// Unlock the cache.
//  - If 'drop' is true, the key is dropped from the cache.
void lazyfree_cache_unlock(cache_t cache, bool drop);



// == Extra API ==

// Prints some stats and set verbose logging
void lazyfree_cache_debug(cache_t cache, bool verbose);


static struct cache_impl lazyfree_cache_impl = {
    .new = lazyfree_cache_new,
    .free = lazyfree_cache_free,
    .write_lock = lazyfree_cache_write_lock,
    .read_try_lock = lazyfree_cache_read_try_lock,
    .read_lock_check = lazyfree_cache_read_lock_check,
    .unlock = lazyfree_cache_unlock,
    .debug = lazyfree_cache_debug,
};
