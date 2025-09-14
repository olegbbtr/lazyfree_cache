#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define K 1024ul
#define M (K*K)
#define G (K*M)

#ifndef CACHE_H
#define CACHE_H

// PAGE_SIZE should be equal to kernel page size.
#define PAGE_SIZE 4096

typedef void* cache_t;
typedef uint64_t cache_key_t;

typedef void* (*mmap_impl_t)(size_t size);
typedef void (*madv_impl_t)(void *memory, size_t size);

struct cache_impl {
    cache_t (*new)(size_t cache_size, mmap_impl_t mmap_impl, madv_impl_t madv_impl);
    void (*free)(cache_t cache);

    // Locks the key for write.
    // Returns true if the key is found.
    bool (*write_lock)(cache_t cache, cache_key_t key, uint8_t **value);

    // Locks the key optimistically for read.
    // Returns true if the key is found.
    bool (*read_try_lock)(cache_t cache, cache_key_t key, uint8_t *head, uint8_t **tail);
    
    // Checks if the read lock is still valid.
    bool (*read_lock_check)(cache_t cache);
    
    // Unlocks the key.
    void (*unlock)(cache_t cache, bool drop);

    // Debug
    void (*debug)(cache_t cache, bool verbose);

    // Allocate memory for the cache.
    mmap_impl_t mmap_impl;

    // Advise the kernel about the memory when done writing to the chunk.
    madv_impl_t madv_impl;
};



// == Memory allocation ==

void *mmap_normal(size_t size);
void *mmap_file(size_t size);

void madv_lazyfree(void *memory, size_t size);
void madv_noop(void *memory, size_t size);


#endif
