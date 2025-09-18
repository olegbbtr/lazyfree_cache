#include <string.h>
#include <stdlib.h>

#include "cache.h"
#include "stub_cache.h"

#include "util.h"



lazyfree_cache_t stub_cache_new(size_t capacity_bytes, size_t lazyfree_chunks, size_t anon_chunks, size_t disk_chunks) {
    UNUSED(capacity_bytes);
    UNUSED(lazyfree_chunks);
    UNUSED(anon_chunks);
    UNUSED(disk_chunks);
    return (lazyfree_cache_t)(&EMPTY_PAGE);
}

void stub_cache_free(lazyfree_cache_t lfcache) { 
    assert(lfcache == (lazyfree_cache_t)(&EMPTY_PAGE));
}

// == Read Lock API ==

void stub_cache_read_lock(lazyfree_cache_t cache, lazyfree_rlock_t* lock) {
    UNUSED(cache);
    lock->head = EMPTY_PAGE;
    lock->tail = 0;
    EMPTY_PAGE[PAGE_SIZE-1] = 0; // Evict the page
    return;
}


bool stub_cache_read_unlock(lazyfree_cache_t cache, lazyfree_rlock_t* lock, bool drop) {
    UNUSED(cache);
    UNUSED(lock);
    UNUSED(drop);
    return true;
}

// == Write Lock API ==
// Always uses the same page

void* stub_cache_write_lock(lazyfree_cache_t cache, lazyfree_rlock_t* lock) {
    UNUSED(cache);
    UNUSED(lock);
    return EMPTY_PAGE;
}

void stub_cache_write_unlock(lazyfree_cache_t cache, bool drop) {
    UNUSED(cache);
    UNUSED(drop);
    return;
}

struct lazyfree_impl lazyfree_stub_impl() {
    return (struct lazyfree_impl){
        .new = stub_cache_new,
        .free = stub_cache_free,

        .read_lock = stub_cache_read_lock,
        .read_unlock = stub_cache_read_unlock,

        .write_lock = stub_cache_write_lock,
        .write_unlock = stub_cache_write_unlock,

        .lazyfree_chunks = NUMBER_OF_CHUNKS,
        .anon_chunks = 0,
        .disk_chunks = 0,
    };
}
