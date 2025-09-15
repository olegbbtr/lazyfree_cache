#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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

struct lazyfree_impl {
    lazyfree_cache_t (*new)(size_t cache_size, lazyfree_mmap_impl_t mmap_impl, lazyfree_madv_impl_t madv_impl);
    void (*free)(lazyfree_cache_t cache);

    // Locks the key for write.
    // Returns true if the key is found.
    bool (*write_lock)(lazyfree_cache_t cache, lazyfree_key_t key, uint8_t **value);

    // Locks the key optimistically for read.
    // Returns true if the key is found.
    bool (*read_try_lock)(lazyfree_cache_t cache, lazyfree_key_t key, uint8_t *head, uint8_t **tail);
    
    // Checks if the read lock is still valid.
    bool (*read_lock_check)(lazyfree_cache_t cache);
    
    // Unlocks the key.
    void (*unlock)(lazyfree_cache_t cache, bool drop);


    // Allocate memory for the cache.
    lazyfree_mmap_impl_t mmap_impl;

    // Advise the kernel about the memory when done writing to the chunk.
    lazyfree_madv_impl_t madv_impl;

    // Returns stats about the cache.
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
