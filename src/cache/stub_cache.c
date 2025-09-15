#include <string.h>
#include <stdlib.h>

#include "stub_cache.h"


uint8_t EMPTY_PAGE[PAGE_SIZE];

lazyfree_cache_t stub_cache_new(size_t /*cache_size*/, lazyfree_mmap_impl_t /*mmap_impl*/, lazyfree_madv_impl_t /*madv_impl*/) {
    return (lazyfree_cache_t)(&EMPTY_PAGE);
}

void stub_cache_free(lazyfree_cache_t lfcache) { 
    assert(lfcache == (lazyfree_cache_t)(&EMPTY_PAGE));
}

// == Read Lock API ==

lazyfree_rlock_t stub_cache_read_lock(lazyfree_cache_t /*cache*/, lazyfree_key_t /*key*/) {
    return (lazyfree_rlock_t){ .tail = NULL };
}

bool stub_cache_read_lock_check(lazyfree_cache_t /*cache*/, lazyfree_rlock_t /*lock*/) {
    // Read lock is never valid
    return false;
}

void stub_cache_read_unlock(lazyfree_cache_t /*cache*/, lazyfree_rlock_t /*lock*/, bool /*drop*/) {
    return;
}

// == Write Lock API ==
// Always uses the same page

void* stub_cache_write_alloc(lazyfree_cache_t /*cache*/, lazyfree_key_t /*key*/) {
    return EMPTY_PAGE;
}

void* stub_cache_write_upgrade(lazyfree_cache_t /*cache*/, lazyfree_rlock_t* /*lock*/) {
    return EMPTY_PAGE;
}

void stub_cache_write_unlock(lazyfree_cache_t /*cache*/, bool /*drop*/) {
    return;
}

struct lazyfree_impl lazyfree_stub_impl() {
    return (struct lazyfree_impl){
    .new = stub_cache_new,
    .free = stub_cache_free,

    .read_lock = stub_cache_read_lock,
    .read_lock_check = stub_cache_read_lock_check,
    .read_unlock = stub_cache_read_unlock,

    .write_upgrade = stub_cache_write_upgrade,
    .write_alloc = stub_cache_write_alloc,
    .write_unlock = stub_cache_write_unlock,

    .mmap_impl = lazyfree_mmap_anon,
    .madv_impl = NULL,
    };
}
