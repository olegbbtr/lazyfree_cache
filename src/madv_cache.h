
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

#define K 1024ul
#define M (K*K)
#define G (K*M)


// PAGE_SIZE should be equal to kernel page size.
#define PAGE_SIZE 4096
// Should be significantly higher than actual available memory.
// Beyond that, the cache will start evicting random consecutive entries.
#define CACHE_CAPACITY (8 * G)


typedef uint64_t madv_key_t;

struct madv_cache;

void madv_cache_init(struct madv_cache* cache);

void madv_cache_free(struct madv_cache* cache);

// Prints some  stats
void madv_cache_print_stats(struct madv_cache* cache);


// == High level API ==
// These copy to/from value.

// Copy data from 'value' to the cache.
void madv_cache_put(struct madv_cache* cache, madv_key_t key, const uint8_t value[PAGE_SIZE]);

// Copy data from the cache to 'value'.
bool madv_cache_get(struct madv_cache* cache, madv_key_t key, uint8_t value[PAGE_SIZE]);


// == Low level API ==
// These are zero-copy.

// Lock the cache to write to the key.
//  - Sets 'value' to point at the page inside the cache.
//  - Next call must be madv_cache_unlock.
//    Pointers are valid until then.
//  - Returns true if the key has existing data.
//    Returns false if the key was just allocated and is empty.
bool madv_cache_write_lock(struct madv_cache* cache, 
                           madv_key_t key,
                           uint8_t (*value)[PAGE_SIZE]);

//  Optimistically lock the cache to read read from the page.
//  - Sets 'head' to the first byte of the page.
//  - Sets 'tail' to point at the [1:PAGE_SIZE] of the page.
//    After reading from the tail, must call madv_cache_read_lock_check 
//    to see if the lock is still valid.
//  - Next call must be madv_cache_unlock.
//    Pointers are valid until then.
//  - Returns true if the key has existing data.
//    Returns false if the key is empty.
bool madv_cache_read_lock(struct madv_cache* cache, 
                          madv_key_t key,
                          uint8_t* head,
                          const uint8_t (*tail)[PAGE_SIZE-1]);

// Check if the read lock is still valid.
bool madv_cache_read_lock_check(struct madv_cache* cache);

// Unlock the cache.
//  - If 'drop' is true, the key is dropped from the cache.
void madv_cache_unlock(struct madv_cache* cache, bool drop);


// Implementation constants
#define NUMBER_OF_CHUNKS 32 
#define CHUNK_SIZE (CACHE_CAPACITY / NUMBER_OF_CHUNKS) // E.g. 256mb
static_assert(CHUNK_SIZE==256 * M, "Chunk size is not 256mb");

#define PAGES_PER_CHUNK (CHUNK_SIZE / PAGE_SIZE) // E.g. 64K
static_assert(PAGES_PER_CHUNK==64 * K, "Pages per chunk is not 64K");

struct discardable_entry {
    uint8_t head;
    uint8_t tail[PAGE_SIZE-1];
};
static_assert(sizeof(struct discardable_entry) == PAGE_SIZE, "Discardable entry size is not equal to page size");

struct entry_descriptor {
    uint32_t index;
    int8_t chunk;
};

struct chunk {
    struct discardable_entry* entries; // anonymous mmap size=CHUNK_SIZE
    uint8_t* first_byte0; // malloc size=PAGES_PER_CHUNK/8 

    uint32_t* free_pages; // malloc size=PAGES_PER_CHUNK
    uint32_t free_pages_count;
    uint32_t len;
    madv_key_t* keys; // malloc size=PAGES_PER_CHUNK
};

struct madv_cache {
    struct chunk chunks[NUMBER_OF_CHUNKS];
    
    size_t current_chunk_idx;
    
    struct { uint64_t key; struct entry_descriptor value; } *map; 

    bool verbose;
    size_t total_free_pages;
};
