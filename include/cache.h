#ifndef CACHE_H
#define CACHE_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>


typedef struct lazyfree_cache* lazyfree_cache_t;
typedef uint64_t lazyfree_key_t;

typedef void* (*lazyfree_mmap_impl_t)(size_t size);
typedef void (*lazyfree_madv_impl_t)(void *memory, size_t size);

struct lazyfree_stats {
    size_t total_pages;
    size_t free_pages;
};


// ========= Generic cache ============

// PAGE_SIZE must be equal to kernel page size.
#define PAGE_SIZE 4096

// Use this to read from the page.
typedef struct {
    const uint8_t *head;     // [0:PAGE_SIZE-1]
    uint8_t tail;            // last byte of the page

    uint8_t __padding[15];
} lazyfree_rlock_t;  

// Use this to check if the entry is present in the cache.
#define LAZYFREE_LOCK_IS_BLANK(lock) ((lock).head == NULL)

// Use this to check if the lock is still valid.
#define LAZYFREE_LOCK_IS_VALID(lock) ((lock).head[PAGE_SIZE-1] > 0)

// Helper to memcpy with offset
// Returns true if the lock is still valid.
static inline bool lazyfree_read(void *dest, lazyfree_rlock_t lock, size_t offset, size_t size) {
    if (LAZYFREE_LOCK_IS_BLANK(lock)) {
        return false;
    }
    memcpy(dest, lock.head + offset, size);
    if (offset + size == PAGE_SIZE) {
        ((uint8_t*)dest)[size-1] = lock.tail;
    }
    return LAZYFREE_LOCK_IS_VALID(lock);
}


struct lazyfree_impl {
    lazyfree_cache_t (*new)(size_t cache_size, lazyfree_mmap_impl_t mmap_impl, lazyfree_madv_impl_t madv_impl);
    void (*free)(lazyfree_cache_t cache);

    // == Optimistic Read Lock API ==
    lazyfree_rlock_t (*read_lock)(lazyfree_cache_t cache, lazyfree_key_t key);
    void             (*read_unlock)(lazyfree_cache_t cache, lazyfree_rlock_t lock, bool drop);

    // == Write Lock API ==
    void* (*write_upgrade)(lazyfree_cache_t cache, lazyfree_rlock_t* lock);
    void* (*write_alloc)(lazyfree_cache_t cache, lazyfree_key_t key);
    void  (*write_unlock)(lazyfree_cache_t cache, bool drop);

    // == Memory implementation ==
    lazyfree_mmap_impl_t mmap_impl;
    lazyfree_madv_impl_t madv_impl;

    // == Extra API ==
    struct lazyfree_stats (*stats)(lazyfree_cache_t cache, bool verbose);
};

struct lazyfree_impl lazyfree_impl();
struct lazyfree_impl lazyfree_anon_impl();
struct lazyfree_impl lazyfree_disk_impl();
struct lazyfree_impl lazyfree_stub_impl();

#endif
