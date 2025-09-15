#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>

// PAGE_SIZE should be equal to kernel page size.
#define PAGE_SIZE 4096


// == Generic cache ==

typedef struct lazyfree_cache* lazyfree_cache_t;
typedef uint64_t lazyfree_key_t;

typedef void* (*lazyfree_mmap_impl_t)(size_t size);
typedef void (*lazyfree_madv_impl_t)(void *memory, size_t size);

struct lazyfree_stats {
    size_t total_pages;
    size_t free_pages;
};

typedef struct {
    lazyfree_key_t key;  
    bool active;
    // Internal use
    uint8_t _chunk;
    uint8_t _padding;
    // Data is two parts
    uint8_t head;
    uint8_t* tail;
} lazyfree_rlock_t;  
static_assert(sizeof(lazyfree_rlock_t) == 24, "");

struct lazyfree_impl {
    lazyfree_cache_t (*new)(size_t cache_size, lazyfree_mmap_impl_t mmap_impl, lazyfree_madv_impl_t madv_impl);
    void (*free)(lazyfree_cache_t cache);

    // == Optimistic Read Lock API ==
    lazyfree_rlock_t (*read_try_lock)(lazyfree_cache_t cache, lazyfree_key_t key);
    bool (*read_lock_check)(lazyfree_cache_t cache, lazyfree_rlock_t lock);
    void (*read_unlock)(lazyfree_cache_t cache, lazyfree_rlock_t lock, bool drop);

    // == Write Lock API ==
    bool (*upgrade_lock)(lazyfree_cache_t cache, lazyfree_rlock_t lock, uint8_t **value);
    uint8_t* (*alloc)(lazyfree_cache_t cache, lazyfree_key_t key);
    void (*write_unlock)(lazyfree_cache_t cache, bool drop);

    // == Memory implementation ==
    lazyfree_mmap_impl_t mmap_impl;
    lazyfree_madv_impl_t madv_impl;

    // == Extra API ==
    struct lazyfree_stats (*stats)(lazyfree_cache_t cache, bool verbose);
};


// == Memory allocation ==

// Allocate anonymous memory.
void *lazyfree_mmap_anon(size_t size);
// Allocate file memory.
void *lazyfree_mmap_file(size_t size);

// MADV_FREE
void lazyfree_madv_free(void *memory, size_t size);

// MADV_COLD
void lazyfree_madv_cold(void *memory, size_t size);

// == Implementation details ==

#define K 1024ul
#define M (K*K)
#define G (K*M)

#define UNUSED(x) (void)(x)

#endif
