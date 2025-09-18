#include <assert.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cache.h"
#include "lazyfree_cache.h"

#include "util.h"
#include "hashmap.h"
#include "bitset.h"
#include "random.h"


#define DEBUG_KEY -1ul


struct discardable_entry {
    volatile uint8_t head[PAGE_SIZE-1];
    uint8_t tail;
};
static_assert(sizeof(struct discardable_entry) == PAGE_SIZE, "Discardable entry size is not equal to page size");


struct entry_descriptor {
    uint32_t index;
    int8_t chunk;
};
static_assert(sizeof(struct entry_descriptor) == 8, "entry_descriptor size is not 8 bytes");

typedef struct {
    lazyfree_key_t key;
    volatile uint8_t *head;     // [0:PAGE_SIZE-1]
    uint8_t tail;      // last byte of the page

    int8_t _chunk;
    uint16_t _index;
} rlock_impl_t;
static_assert(sizeof(rlock_impl_t) == 24, "rlock_impl_t size is not 24 bytes");
static_assert(sizeof(lazyfree_rlock_t) == 24, "lazyfree_rlock_t size is not 24 bytes");
static_assert(offsetof(lazyfree_rlock_t, head) == offsetof(rlock_impl_t, head), "lazyfree_rlock_t and rlock_impl_t have different head offsets");
static_assert(offsetof(lazyfree_rlock_t, tail) == offsetof(rlock_impl_t, tail), "lazyfree_rlock_t and rlock_impl_t have different tail offsets");

static struct entry_descriptor  EMPTY_DESC = { .chunk = -1 };


struct chunk {
    madv_impl_t madv_impl;
    mmap_impl_t mmap_impl;
    struct discardable_entry* entries; // anonymous mmap size=CHUNK_SIZE
    bitset_t bit0;                     // malloc size=PAGES_PER_CHUNK/8 
    uint32_t* free_pages;              // malloc size=PAGES_PER_CHUNK
    lazyfree_key_t* keys;              // malloc size=PAGES_PER_CHUNK
    uint32_t free_pages_count;
    uint32_t len;
};

struct lazyfree_cache {
    size_t cache_capacity;

    struct chunk chunks[NUMBER_OF_CHUNKS];
    size_t pages_per_chunk;
    size_t chunk_size;
    size_t current_chunk_idx;

    struct hashmap_s map;
    struct entry_descriptor key0;

    size_t total_free_pages;

    // Write lock state
    uint32_t wlock_index;
    int8_t wlock_chunk;
    lazyfree_key_t wlock_key;

    bool verbose;
};

static int exact_key_comparer(const void *a, hashmap_uint32_t a_len,
                              const void *b, hashmap_uint32_t b_len) {
    UNUSED(a_len);
    UNUSED(b_len);
    return a == b;
}

static uint32_t hash_u64_to_u32(uint64_t key) {
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdULL;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53ULL;
    key ^= key >> 33;
    return (uint32_t)key; // take the low 32 bits
}

static hashmap_uint32_t exact_key_hasher(hashmap_uint32_t seed,
                                         const void *key,
                                         hashmap_uint32_t key_len) {
    UNUSED(key_len);
    return seed + hash_u64_to_u32((uint64_t) key) + 1;
}

lazyfree_cache_t lazyfree_cache_new_ex(size_t cache_capacity, size_t lazyfree_chunks, size_t anon_chunks, size_t disk_chunks) {
    if (lazyfree_chunks + anon_chunks + disk_chunks != NUMBER_OF_CHUNKS) {
        printf("Lazyfree chunks + anon chunks + disk chunks must equal %d\n", NUMBER_OF_CHUNKS);
        exit(1);
    }
    struct lazyfree_cache* cache = malloc(sizeof(struct lazyfree_cache));
    memset(cache, 0, sizeof(struct lazyfree_cache));
    cache->cache_capacity = cache_capacity;
    cache->chunk_size = cache_capacity / NUMBER_OF_CHUNKS;
    cache->pages_per_chunk = cache->chunk_size / PAGE_SIZE;

    size_t idx = 0;
    while (idx < lazyfree_chunks) {
        cache->chunks[idx].mmap_impl = lazyfree_mmap_anon;
        cache->chunks[idx].madv_impl = lazyfree_madv_free;
        idx++;
    }
    while (idx < lazyfree_chunks + anon_chunks) {
        cache->chunks[idx].mmap_impl = lazyfree_mmap_anon;
        cache->chunks[idx].madv_impl = lazyfree_madv_nop;
        idx++;
    }
    while (idx < lazyfree_chunks + anon_chunks + disk_chunks) {
        cache->chunks[idx].mmap_impl = lazyfree_mmap_file;
        cache->chunks[idx].madv_impl = lazyfree_madv_nop;
        idx++;
    }
    

    // Allocate all chunks on start
    for (size_t i = 0; i < NUMBER_OF_CHUNKS; i++) {
        void *entries = cache->chunks[i].mmap_impl(cache->chunk_size);
        assert(entries != MAP_FAILED);

        cache->chunks[i].entries = entries;

        cache->chunks[i].bit0 = bitset_new(cache->pages_per_chunk);
        assert(cache->chunks[i].bit0 != NULL);

        cache->chunks[i].free_pages = malloc(cache->pages_per_chunk * sizeof(uint32_t));
        assert(cache->chunks[i].free_pages != NULL);

        cache->chunks[i].keys = malloc(cache->pages_per_chunk * sizeof(uint64_t));
        assert(cache->chunks[i].keys != NULL);
    }
    hashmap_create_ex( (struct hashmap_create_options_s){
        .initial_capacity = NUMBER_OF_CHUNKS*cache->pages_per_chunk,
        .comparer = &exact_key_comparer,
        .hasher = &exact_key_hasher,
    }, &cache->map);
    cache->key0.chunk = -1;

    cache->total_free_pages = NUMBER_OF_CHUNKS * cache->pages_per_chunk;

    cache->wlock_chunk = EMPTY_DESC.chunk;
    
    return cache;
}

lazyfree_cache_t lazyfree_cache_new(size_t cache_capacity) {
    return lazyfree_cache_new_ex(cache_capacity, NUMBER_OF_CHUNKS, 0, 0);
}

void lazyfree_cache_free(struct lazyfree_cache* cache) {
    for (size_t i = 0; i < NUMBER_OF_CHUNKS; ++i) {
        munmap(cache->chunks[i].entries, cache->chunk_size);
        bitset_free(cache->chunks[i].bit0);
        free(cache->chunks[i].free_pages);
        free(cache->chunks[i].keys);
    }
    hashmap_destroy(&cache->map);
    free(cache);
}

static void print_stats(lazyfree_cache_t cache) {
    struct lazyfree_cache* lazyfree_cache = (struct lazyfree_cache*) cache;
    printf("Htable size: %u\n", hashmap_num_entries(&lazyfree_cache->map));
    printf("Total free pages: %zu\n", lazyfree_cache->total_free_pages);
    for (size_t i = 0; i < NUMBER_OF_CHUNKS; ++i) {
        struct chunk* chunk = &lazyfree_cache->chunks[i];
        float ratio = (float) chunk->free_pages_count / (float) chunk->len;
        printf("Chunk %zu: %u/%u (%.2f%%)\n", i, chunk->free_pages_count, chunk->len, ratio * 100);
    }
}

// == Hashmap helpers ==

union {
    struct entry_descriptor desc;
    void *value;
    struct {
        uint32_t index;
        int8_t chunk;
        bool set;
    } entry;
} hmap_access;
static_assert(sizeof(hmap_access) == sizeof(void*), "");
static_assert(sizeof(hmap_access.entry) == sizeof(void*), "");

static struct entry_descriptor hmap_get(struct lazyfree_cache* cache, lazyfree_key_t key) {
    if (key == 0) {
        return cache->key0;
    }
    hmap_access.value = hashmap_get(&cache->map, (void*) (key), sizeof(key));
    // printf("hmap_get %lu: %p\n", key, hmap_access.value);
    if (hmap_access.entry.set) {
        return hmap_access.desc;
    }
    return EMPTY_DESC;
}


static void hmap_put(struct lazyfree_cache* cache, lazyfree_key_t key, struct entry_descriptor desc) {
    if (key == 0) {
        cache->key0 = desc;
        return;
    }
    hmap_access.desc = desc;
    hmap_access.entry.set = true;
    hashmap_put(&cache->map, (void*) key, sizeof(key), hmap_access.value);
}

static void hmap_remove(struct lazyfree_cache* cache, lazyfree_key_t key) {
    hashmap_remove(&cache->map,     (void*) (key), sizeof(key));
}

// == Bitset helpers

static void bit_to_tail(struct chunk* chunk, uint32_t index, uint8_t* tail) {
    if (!bitset_get(chunk->bit0, index)) {
        *tail &= ~1; // set the last bit to 0
    }
}

static void bit_from_tail(struct chunk* chunk, uint32_t index, uint8_t* tail) {
    bool bit0 = (*tail & 1) != 0;
    bitset_put(chunk->bit0, index, bit0);
    *tail |= 1;
}

// == rlock helpers ==

static uint32_t rlock_to_index(struct chunk* chunk, rlock_impl_t* lock) {
    struct discardable_entry* entry = (struct discardable_entry*) (lock->head);
    return entry - chunk->entries;
}

static bool rlock_check_key(struct lazyfree_cache* cache, rlock_impl_t* lock) {
    struct chunk* chunk = &cache->chunks[lock->_chunk];
    uint32_t index = rlock_to_index(chunk, lock);

    if (chunk->keys[index] != lock->key) {
        if (cache->verbose) {
            printf("Key %lu was evicted by dropping the chunk\n", lock->key);
        }
        return false;
    }
    return true;
}

// == Read lock implementation ==

static void cache_drop(struct lazyfree_cache* cache, struct entry_descriptor desc) {
    if (cache->verbose) {
        printf("DEBUG: Dropping chunk %d, index %d\n", desc.chunk, desc.index);
    }
    
    struct chunk* chunk = &cache->chunks[desc.chunk];
    chunk->free_pages[chunk->free_pages_count++] = desc.index;
    if (chunk->free_pages_count > chunk->len) {

        print_stats(cache);
        exit(1);
    }
    
    cache->total_free_pages++;

    hmap_remove(cache, chunk->keys[desc.index]);
    chunk->keys[desc.index] = 0;
}

void lazyfree_read_lock(lazyfree_cache_t cache, lazyfree_rlock_t* lock) {
    assert(cache->wlock_chunk == EMPTY_DESC.chunk);
    rlock_impl_t *lock_impl = (rlock_impl_t*) lock;
    lock_impl->head = EMPTY_PAGE;
    lock_impl->tail = 0;
    lock_impl->_chunk = EMPTY_DESC.chunk;

  
    struct entry_descriptor desc = hmap_get(cache, lock->key);

    if (desc.chunk == EMPTY_DESC.chunk) {
        if (cache->verbose) {
            printf("Key %lu not found\n", lock->key);
        }
        return;
    }
    struct chunk* chunk = &cache->chunks[desc.chunk];
    struct discardable_entry* entry = &chunk->entries[desc.index];

    if (cache->verbose || lock->key == DEBUG_KEY) {
        printf("DEBUG: rlock key=%lu chunk=%d index=%d\n", lock->key, desc.chunk, desc.index);
    }

    if (chunk->keys[desc.index] != lock->key) {
        if (cache->verbose || lock->key == DEBUG_KEY) {
            printf("Key %lu was evicted by dropping the chunk\n", lock->key);
        }
        return;
    }   

    lock_impl->_index = desc.index;
    lock_impl->_chunk = desc.chunk;
    
    if (entry->tail == 0) {
        if (cache->verbose || lock->key == DEBUG_KEY) {
            printf("Key %lu was evicted by kernel\n", lock->key);
        }

        return;
    }

    lock_impl->head = entry->head;
    lock_impl->tail = entry->tail;
    bit_to_tail(chunk, desc.index, &lock_impl->tail);
}


bool lazyfree_read_unlock(struct lazyfree_cache* cache, lazyfree_rlock_t* lock, bool drop) {
    assert(lock->head != NULL);
    assert(cache->wlock_chunk == EMPTY_DESC.chunk);



    if(lock->head == EMPTY_PAGE) {
        // No real page
        return false;
    }

    
    rlock_impl_t *lock_impl = (rlock_impl_t*) lock;
    assert(lock_impl->_chunk != EMPTY_DESC.chunk);

    if (!LAZYFREE_LOCK_CHECK(*lock)) {
        printf("LOCK CHECK FAILED\n");
        exit(1);
    }
   
    // Check if was dropped already
    if (!rlock_check_key(cache, lock_impl)) {
        return false;
    }

    // Do we need to drop?
    if (!drop) {
        return true;
    }
    
    struct chunk* chunk = &cache->chunks[lock_impl->_chunk];
    struct entry_descriptor desc = {
        .chunk = lock_impl->_chunk,
        .index = rlock_to_index(chunk, lock_impl),
    };
    cache_drop(cache, desc);
    return true;
}

// == Write Lock Implementation ==

static void drop_next_chunk(struct lazyfree_cache* cache) {
    // Next chunk:
    // cache->current_chunk_idx = (cache->current_chunk_idx + 1) % NUMBER_OF_CHUNKS;

    // Random chunk:
    cache->current_chunk_idx = random_next() % NUMBER_OF_CHUNKS;

    struct chunk* chunk = &cache->chunks[cache->current_chunk_idx];

    for (size_t i = 0; i < chunk->len; ++i) {
        hmap_remove(cache, chunk->keys[i]);
    }
    memset(chunk->keys, 0, chunk->len * sizeof(lazyfree_key_t));
    int ret = madvise(chunk->entries, cache->chunk_size, MADV_DONTNEED);
    if (ret != 0) {
        printf("MADV_DONTNEED failed: %d\n", ret);
        exit(1);
    }
    
    cache->total_free_pages += (chunk->len - chunk->free_pages_count);
    
    chunk->len = 0;
    chunk->free_pages_count = 0;
}

static void advance_chunk(struct lazyfree_cache* cache) {
    struct chunk* chunk = &cache->chunks[cache->current_chunk_idx];
    chunk->madv_impl(chunk->entries, cache->chunk_size);
    
    cache->current_chunk_idx = (cache->current_chunk_idx + 1) % NUMBER_OF_CHUNKS;
} 

static struct entry_descriptor alloc_current_chunk(struct lazyfree_cache* cache) {
    struct chunk* chunk = &cache->chunks[cache->current_chunk_idx];
    struct entry_descriptor desc = { .chunk = cache->current_chunk_idx };

    if (chunk->free_pages_count > 0 ) {
        // We have free pages
        uint32_t idx = chunk->free_pages[--chunk->free_pages_count];
        desc.index = idx;

        cache->total_free_pages--;
        return desc;
    }

    if (chunk->len < cache->pages_per_chunk) {
        // We have blank pages
        desc.index = chunk->len++;

        cache->total_free_pages--;
        return desc;
    }
    
    // Nothing left in this chunk
    return EMPTY_DESC;
}

static struct entry_descriptor alloc_new_page(struct lazyfree_cache* cache) {
    struct entry_descriptor desc = EMPTY_DESC;
    if (cache->total_free_pages == 0) {
        drop_next_chunk(cache);
    }

    size_t chunks_visited = 0;
    while(desc.chunk == EMPTY_DESC.chunk) {
        desc = alloc_current_chunk(cache);
        if (desc.chunk != EMPTY_DESC.chunk) {
            // Found a free page
            break;
        }

        advance_chunk(cache);

        chunks_visited++;
        if (chunks_visited >= NUMBER_OF_CHUNKS) {
            // This means total_free_pages is not updated correctly
            printf("Failed to find free page\n");
            print_stats(cache);
            exit(1);
        }
    }   

    return desc;
}

void* lazyfree_write_alloc(lazyfree_cache_t cache, lazyfree_key_t key) {
    assert(cache->wlock_chunk == EMPTY_DESC.chunk);

    // Looking for a free page
    struct entry_descriptor desc = alloc_new_page(cache);
    struct chunk* chunk = &cache->chunks[desc.chunk];
    struct discardable_entry* entry = &chunk->entries[desc.index];

    cache->wlock_chunk = desc.chunk;
    cache->wlock_index = desc.index;
    cache->wlock_key = key;
    hmap_put(cache, key, desc);
    chunk->keys[desc.index] = cache->wlock_key;
    
    return entry;
}


void* lazyfree_write_lock(lazyfree_cache_t cache, lazyfree_rlock_t* lock) {
    rlock_impl_t *lock_impl = (rlock_impl_t*) lock;
    assert(cache->wlock_chunk == EMPTY_DESC.chunk);
    
    if (lock_impl->head == NULL) {
        // This is an empty lock
        return lazyfree_write_alloc(cache, lock_impl->key);
    }

    if (lock_impl->head == EMPTY_PAGE) {
        // This is an empty entry
        return lazyfree_write_alloc(cache, lock_impl->key);
    }
    assert(lock_impl->_chunk != EMPTY_DESC.chunk);


    if (!rlock_check_key(cache, lock_impl)) {
        // This is now some other key
        lock_impl->head = NULL;
        return lazyfree_write_alloc(cache, lock_impl->key);
    }

    // Use first byte to lock the page
    uint8_t byte0 = lock_impl->head[0];
    lock_impl->head[0] = 1;
    if (!LAZYFREE_LOCK_CHECK(*lock)) {
        lock_impl->head[0] = 0;
        lock_impl->head = NULL;
        printf("DEBUG: Page updated during lock upgrade\n");
        return lazyfree_write_alloc(cache, lock_impl->key);
    }
    lock_impl->head[0] = byte0;
    
    // Set wlock
    cache->wlock_chunk = lock_impl->_chunk;
    cache->wlock_index = lock_impl->_index;
    cache->wlock_key   = lock_impl->key;
    // Already in hashmap and keys
    
    struct chunk* chunk = &cache->chunks[lock_impl->_chunk];
    struct discardable_entry* entry = &chunk->entries[lock_impl->_index];

    bit_to_tail(chunk, lock_impl->_index, &entry->tail);
    return (uint8_t*) entry;
}

void lazyfree_write_unlock(lazyfree_cache_t cache, bool drop) {
    assert(cache->wlock_chunk != EMPTY_DESC.chunk);


    struct entry_descriptor desc = {
        .chunk = cache->wlock_chunk,
        .index = cache->wlock_index,
    };

    cache->wlock_chunk = EMPTY_DESC.chunk;

    if (drop) {
        cache_drop(cache, desc);
        return;
    } 
    // Move bit0 from head to bit0
    struct chunk* chunk = &cache->chunks[desc.chunk];
    struct discardable_entry* entry = &chunk->entries[desc.index];
    bit_from_tail(chunk, desc.index, &entry->tail);  
}



struct lazyfree_stats lazyfree_fetch_stats(lazyfree_cache_t cache, bool verbose) {
    struct lazyfree_cache* lazyfree_cache = (struct lazyfree_cache*) cache;
    struct lazyfree_stats stats;
    stats.total_pages = lazyfree_cache->cache_capacity / PAGE_SIZE;
    stats.free_pages = lazyfree_cache->total_free_pages;
    print_stats(cache);
    lazyfree_cache->verbose = verbose;
    return stats;
}


// == Tests

void lazyfree_cache_tests() {
    volatile uint64_t value = random_next();    
    lazyfree_cache_t cache = lazyfree_cache_new(32*NUMBER_OF_CHUNKS*PAGE_SIZE);
    uint64_t* ptr = NULL;
    lazyfree_rlock_t lock;
    uint64_t result;
    
    
    // // HEAD WRITE
    // ptr = lazyfree_write_alloc(cache, 1);
    // *ptr = value;
    // lazyfree_write_unlock(cache, false);

    // lock.key = 1;
    // lazyfree_read_lock(cache, &lock);
    // lazyfree_read(&lock, &result, 0, sizeof(uint64_t));
    // assert(LAZYFREE_LOCK_CHECK(lock));
    // assert(result == value);
    // lazyfree_read_unlock(cache, &lock, false);
    // // END HEAD WRITE


    // TAIL WRITE
    lock.key = 2;
    ptr = lazyfree_write_lock(cache, &lock);
    ptr[PAGE_SIZE/sizeof(uint64_t) - 1] = value + 1;
    lazyfree_write_unlock(cache, false);


    lazyfree_read_lock(cache, &lock);
    lazyfree_read(&lock, &result, PAGE_SIZE-sizeof(uint64_t), sizeof(uint64_t));
    assert(LAZYFREE_LOCK_CHECK(lock));
    if (result != value + 1) {
        printf("result: %lu, expected: %lu\n", result, value + 1);
        exit(1);
    }
    lazyfree_read_unlock(cache, &lock, false);
    // END TAIL WRITE

    lazyfree_cache_free(cache);
}
