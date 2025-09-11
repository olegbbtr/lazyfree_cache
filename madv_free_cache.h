
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

#define K 1024ul
#define M (K*K)
#define G (K*M)


// PAGE_SIZE should be equal to kernel page size, and must be a multiple of kernel page size.
#define PAGE_SIZE 4096
// ENTRY_SIZE must be >= (4KB - 8). Extra space will be stored in normal memory.
#define ENTRY_SIZE 4096 
// Should be significantly higher than actual memory usage to not cause the cache to overflow.
#define MEMORY_LIMIT (64 * G)

struct madv_free_cache;

void madv_cache_init(struct madv_free_cache* cache);

void madv_cache_free(struct madv_free_cache* cache);

// value must point to the ENTRY_SIZE bytes of memory
// madv_cache_put returns false if the cache is full, only possible when memory limit is reached.
bool madv_cache_put(struct madv_free_cache* cache, uint64_t key, const uint8_t* value);


// madv_cache_get returns -1 if the entry was never put into the cache.
// madv_cache_get returns 0 if the entry was found. It set *value to the entry content.
// madv_cache_get returns 1 if the entry was evicted by kernel. It set *value to 0.
int madv_cache_get(struct madv_free_cache* cache, uint64_t key, uint8_t* value);


// madv_cache_print_stats prints some cache stats
void madv_cache_print_stats(struct madv_free_cache* cache);


// Implementation constants
#define NUMBER_OF_CHUNKS 32

#define CHUNK_SIZE (MEMORY_LIMIT / NUMBER_OF_CHUNKS)
#define PAGES_PER_CHUNK (CHUNK_SIZE / PAGE_SIZE)

typedef uint64_t madv_cache_key_t; // Stores key
#define STORED_DISCARDABLE (PAGE_SIZE - sizeof(madv_cache_key_t))

#define STORED_EXTRA  (ENTRY_SIZE - STORED_DISCARDABLE)
typedef struct {
    madv_cache_key_t key;
    uint8_t value[STORED_DISCARDABLE];
} discardable_entry;

typedef struct {
    uint8_t cnt_put;
    uint8_t cnt_get;
    uint8_t cnt_compacted;

    uint8_t chunk;
    uint32_t index;

    uint8_t extra_value[STORED_EXTRA];
} entry_descriptor;

struct madv_free_cache {
    discardable_entry* chunks[NUMBER_OF_CHUNKS];
    
    size_t current_chunk_idx;
    size_t current_entry_idx;

    size_t next_chunk_idx;
    ssize_t next_chunk_first_slot_idx;
    ssize_t next_chunk_last_entry_idx;

    bool key_zero_set; // key 0 is the sentinel value, so we have to store the value here
    uint8_t key_zero[ENTRY_SIZE]; 
    struct { uint64_t key; entry_descriptor value; } *map; 
};
