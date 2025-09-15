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

void ft_cache_init(ft_cache_t *cache, struct lazyfree_impl impl, 
                   size_t capacity, size_t entry_size,
                   ft_refill_t refill_cb, void *refill_opaque);

void ft_cache_destroy(ft_cache_t *cache);

void ft_cache_get(ft_cache_t *cache, 
                  lazyfree_key_t key, 
                  uint8_t *value);

// Returns true if found, false if not found.
bool ft_cache_drop(ft_cache_t *cache, lazyfree_key_t key);

void ft_cache_debug(ft_cache_t *cache, bool verbose);
                
#endif
