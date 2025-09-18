#include <stdbool.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stddef.h>

#include "cache.h"
#include "lazyfree_cache.h"

#include "util.h"


// == Generic Implementation ==

inline struct lazyfree_impl lazyfree_impl() {
    struct lazyfree_impl impl = {
        .new = lazyfree_cache_new_ex,
        .free = lazyfree_cache_free,

        .read_lock = lazyfree_read_lock,
        .read_unlock = lazyfree_read_unlock,

        .write_lock = lazyfree_write_lock,
        .write_unlock = lazyfree_write_unlock,

        .stats = lazyfree_fetch_stats,
    
        .lazyfree_chunks = NUMBER_OF_CHUNKS,
        .anon_chunks = 0,
        .disk_chunks = 0,
    };    
    return impl;
}

// Anonymous storage is the same: no MADV_FREE, no lock failures.
inline struct lazyfree_impl lazyfree_anon_impl() {
    struct lazyfree_impl impl = lazyfree_impl();
    impl.lazyfree_chunks = 0;
    impl.anon_chunks = NUMBER_OF_CHUNKS;
    return impl;
}

// Store pages in files.
inline struct lazyfree_impl lazyfree_disk_impl() {
    struct lazyfree_impl impl = lazyfree_impl();
    impl.lazyfree_chunks = 0;
    impl.disk_chunks = NUMBER_OF_CHUNKS;
    return impl;
}
