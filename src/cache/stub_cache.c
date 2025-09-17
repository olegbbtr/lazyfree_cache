#include <string.h>
#include <stdlib.h>

#include "cache.h"
#include "stub_cache.h"

#include "util.h"



lazyfree_cache_t stub_cache_new(size_t cache_size, lazyfree_mmap_impl_t mmap_impl, lazyfree_madv_impl_t madv_impl) {
    UNUSED(cache_size);
    UNUSED(mmap_impl);
    UNUSED(madv_impl);
    return (lazyfree_cache_t)(&EMPTY_PAGE);
}

void stub_cache_free(lazyfree_cache_t lfcache) { 
    assert(lfcache == (lazyfree_cache_t)(&EMPTY_PAGE));
}

// == Read Lock API ==

lazyfree_rlock_t stub_cache_read_lock(lazyfree_cache_t cache, lazyfree_key_t key) {
    UNUSED(cache);
    UNUSED(key);
    return EMPTY_LOCK;
}


void stub_cache_read_unlock(lazyfree_cache_t cache, lazyfree_rlock_t lock, bool drop) {
    UNUSED(cache);
    UNUSED(lock);
    UNUSED(drop);
    return;
}

// == Write Lock API ==
// Always uses the same page

void* stub_cache_write_alloc(lazyfree_cache_t cache, lazyfree_key_t key) {
    UNUSED(cache);
    UNUSED(key);
    return EMPTY_PAGE;
}   

void* stub_cache_write_upgrade(lazyfree_cache_t cache, lazyfree_rlock_t* lock) {
    UNUSED(cache);
    UNUSED(lock);
    return EMPTY_PAGE;
}

void stub_cache_write_unlock(lazyfree_cache_t cache, bool drop) {
    UNUSED(cache);
    UNUSED(drop);
    EMPTY_PAGE[PAGE_SIZE-1] = 0; // Evict the page
    return;
}

struct lazyfree_impl lazyfree_stub_impl() {
    return (struct lazyfree_impl){
    .new = stub_cache_new,
    .free = stub_cache_free,

    .read_lock = stub_cache_read_lock,
    .read_unlock = stub_cache_read_unlock,

    .write_upgrade = stub_cache_write_upgrade,
    .write_alloc = stub_cache_write_alloc,
    .write_unlock = stub_cache_write_unlock,

    .mmap_impl = lazyfree_mmap_anon,
    .madv_impl = NULL,
    };
}
