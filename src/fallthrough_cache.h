#include "madv_cache.h"

struct fallthrough_cache_opts {
    uint64_t entry_size;
    void *repopulate_data;
    bool (*repopulate)(void *data, uint64_t key, uint8_t *value);

    uint64_t cache_sizeof;
    void (*cache_init)(void *cache);
    void (*cache_destroy)(void *cache);

    // Locks the key for write.
    bool (*cache_write_lock)(void *cache, uint64_t key, uint8_t (*value)[PAGE_SIZE]);
    
    // Locks the key optimistically for read.
    bool (*cache_read_lock_opt)(void *cache, uint64_t key, uint8_t *head, uint8_t (*tail)[PAGE_SIZE-1]);
    
    // Checks if the read lock is still valid.
    bool (*cache_read_lock_check)(void *cache);

    // Unlocks the key.
    void (*cache_unlock)(void *cache, bool drop);
};

struct fallthrough_cache {
    struct fallthrough_cache_opts opts;
    void *cache;
};

struct fallthrough_cache* fallthrough_cache_new(struct fallthrough_cache_opts opts);

void fallthrough_cache_free(struct fallthrough_cache* cache);

bool fallthrough_cache_get(struct fallthrough_cache* cache, 
                           uint64_t key, 
                           uint8_t *value);

void fallthrough_cache_drop(struct fallthrough_cache* cache, 
                            uint64_t key);
                             


                

