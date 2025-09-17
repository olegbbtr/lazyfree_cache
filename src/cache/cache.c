#include <stdbool.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stddef.h>

#include "cache.h"
#include "lazyfree_cache.h"

#include "util.h"
#include "random.h"

// == Memory functions  ==

void lazyfree_madv_free(void *memory, size_t size) {
    int ret;

    ret = madvise(memory, size, MADV_FREE);
    assert(ret == 0);
}

void lazyfree_madv_cold(void *memory, size_t size) {
    int ret = madvise(memory, size,  MADV_COLD);
    assert(ret == 0);
}

void *lazyfree_mmap_anon(size_t size) {
    void *addr = mmap(NULL, size, 
                PROT_READ | PROT_WRITE, 
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                -1, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    // int ret = madvise(addr, size, MADV_NOHUGEPAGE | MADV_WILLNEED);
    // assert(ret == 0);
    // printf("Anon mmap %zu Mb at %p\n", size / M, addr);
    return addr;
}

void *lazyfree_mmap_file(size_t size) {
    char filename[PATH_MAX];
    mkdir("./tmp", 0755);
    snprintf(filename, PATH_MAX, "./tmp/cache-%ld", random_next());


    int fd = open(filename, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        perror("open");
        exit(1);
    }

    if (ftruncate(fd, size) == -1) {
        perror("ftruncate");
        exit(1);
    }

    void *addr = mmap(NULL, size, 
        PROT_READ | PROT_WRITE, 
        MAP_SHARED | MAP_NORESERVE, 
        fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    close(fd);
    lazyfree_madv_cold(addr, size);
    return addr;
}

bool lock_check_nop(struct lazyfree_cache* cache, lazyfree_rlock_t lock) {
    UNUSED(cache);
    UNUSED(lock);
    return true;
}


// == Generic Implementation ==

inline struct lazyfree_impl lazyfree_impl() {
    struct lazyfree_impl impl = {
        .new = lazyfree_cache_new_ex,
        .free = lazyfree_cache_free,

        .read_lock = lazyfree_read_lock,
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
    return impl;
}

// Store pages in files.
inline struct lazyfree_impl lazyfree_disk_impl() {
    struct lazyfree_impl impl = lazyfree_impl();
    impl.mmap_impl = lazyfree_mmap_file;
    impl.madv_impl = lazyfree_madv_cold;
    return impl;
}
