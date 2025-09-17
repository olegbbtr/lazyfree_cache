#ifndef FALLTHROUGH_CACHE_H
#define FALLTHROUGH_CACHE_H

#include <stdint.h>
#include <stdbool.h>

#include "cache.h"

typedef void (*ft_refill_t)(void *opaque, uint64_t key, uint8_t *value);

struct fallthrough_cache {
    struct lazyfree_impl impl;
    void *cache;

    ft_refill_t refill_cb;
    void *refill_opaque;
   
    uint64_t entry_size;
};

typedef struct fallthrough_cache ft_cache_t; 

// Takes a generic implementation and a refill callback.
void ft_cache_init(ft_cache_t *cache, struct lazyfree_impl impl, 
                   ft_refill_t refill_cb, void *refill_opaque,
                   size_t capacity, size_t entry_size);

void ft_cache_destroy(ft_cache_t *cache);

// Get value from the cache, or refill it.
void ft_cache_get(ft_cache_t *cache, 
                  lazyfree_key_t key, 
                  uint8_t *value);

// Drop the key from the cache. Returns true if existed.
bool ft_cache_drop(ft_cache_t *cache, lazyfree_key_t key);

// Print debug info and remember verbosity.
void ft_cache_debug(ft_cache_t *cache, bool verbose);
                
#endif
