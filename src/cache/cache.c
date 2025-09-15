#include "cache.h"
#include "lazyfree_cache.h"

// == Generic Implementation ==

inline struct lazyfree_impl lazyfree_impl() {
    struct lazyfree_impl impl = {
        .new = lazyfree_cache_new_ex,
        .free = lazyfree_cache_free,

        .read_lock = lazyfree_read_lock,
        .read_lock_check = lazyfree_read_lock_check,
        .read_unlock = lazyfree_read_unlock,

        .write_upgrade = lazyfree_write_upgrade,
        .write_alloc = lazyfree_write_alloc,
        .write_unlock = lazyfree_write_unlock,

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
    impl.madv_impl = lazyfree_madv_cold;
    return impl;
}
