
#include <assert.h>
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

#define MEM_HIGH (10 * G)
#define MEM_TARGET (9 * G)


typedef uint64_t madv_cache_key_t;

struct madv_cache;

void madv_cache_init(struct madv_cache* cache);

void madv_cache_free(struct madv_cache* cache);

// value must point to the ENTRY_SIZE bytes of memory
// madv_cache_put returns false if the cache is full, only possible when memory limit is reached.
bool madv_cache_put(struct madv_cache* cache, madv_cache_key_t key, const uint8_t* value);

// If the entry was found, it sets *value to the entry content and returns true.
// Otherwise, it returns false.
bool madv_cache_get(struct madv_cache* cache, madv_cache_key_t key, uint8_t* value);

// If the entry is found, evicts it and returns true.
// Otherwise, it returns false.
bool madv_cache_evict(struct madv_cache* cache, madv_cache_key_t key);

// madv_cache_print_stats prints some cache stats
void madv_cache_print_stats(struct madv_cache* cache);


// Implementation constants
#define CHUNK_SIZE (1 * G) // 1 Gb chunk
#define NUMBER_OF_CHUNKS (MEMORY_LIMIT / CHUNK_SIZE)

#define PAGES_PER_CHUNK (CHUNK_SIZE / PAGE_SIZE)

#define STORED_DISCARDABLE (PAGE_SIZE - sizeof(madv_cache_key_t))
#define STORED_EXTRA  (ENTRY_SIZE - STORED_DISCARDABLE)

struct discardable_entry {
    madv_cache_key_t key;
    uint8_t value[STORED_DISCARDABLE];
};
static_assert(sizeof(struct discardable_entry) == PAGE_SIZE, "Discardable entry size is not equal to page size");

#define HAZARD_PAGE_PADDING (PAGE_SIZE - sizeof(struct hazard_page*))

struct hazard_page {
    struct hazard_page* next_page;
    uint8_t padding[HAZARD_PAGE_PADDING];
};
static_assert(sizeof(struct hazard_page) == PAGE_SIZE, "Hazard page size is not equal to page size");

struct entry_descriptor {
    uint8_t set;

    uint8_t padding[2];

    uint8_t chunk;
    uint32_t index;
};

struct extra_value {
    madv_cache_key_t key;
    uint8_t value[STORED_EXTRA];
};

struct chunk {
    uint32_t len;
    struct discardable_entry* entries;
    struct hazard_page* free_page_list;
    struct extra_value* extra_values;
};

struct madv_cache {
    struct chunk chunks[NUMBER_OF_CHUNKS];
    
    size_t current_chunk_idx;
    size_t next_chunk_idx;
    size_t compacted_suffix;
    
    bool key_zero_set; // key 0 is the sentinel value, so we have to store the value here
    uint8_t key_zero[ENTRY_SIZE]; 
    struct { uint64_t key; struct entry_descriptor value; } *map; 

    bool verbose;
    size_t mem_upper_bound;
};
