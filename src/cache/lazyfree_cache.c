#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>

// #include "stb_ds.h"
#include "hashmap.h"

#include "bitset.h"
#include "random.h"

#include "lazyfree_cache.h"

#define DEBUG_KEY 1609906849761488ul
// #define DEBUG_KEY -1ul


#define NUMBER_OF_CHUNKS 8
static_assert(NUMBER_OF_CHUNKS < (1 << 8), "Too many chunks");

// This guarantees we switch chunks < 50% of the writes
#define MIN_FREE_TOTAL_PAGES (2 * NUMBER_OF_CHUNKS)


struct discardable_entry {
    uint8_t head;
    uint8_t tail[PAGE_SIZE-1];
};
static_assert(sizeof(struct discardable_entry) == PAGE_SIZE, "Discardable entry size is not equal to page size");

struct entry_descriptor {
    uint32_t index;
    int8_t chunk;
    bool set;
    uint16_t padding;
};

struct chunk {
    struct discardable_entry* entries; // anonymous mmap size=CHUNK_SIZE
    bitset_t bit0;                     // malloc size=PAGES_PER_CHUNK/8 
    uint32_t* free_pages;              // malloc size=PAGES_PER_CHUNK
    cache_key_t* keys;                 // malloc size=PAGES_PER_CHUNK
    uint32_t free_pages_count;
    uint32_t len;
};

struct lazyfree_cache {
    madv_impl_t madv_impl;
    size_t cache_capacity;
    size_t pages_per_chunk;
    size_t chunk_size;

    size_t current_chunk_idx;
    struct chunk chunks[NUMBER_OF_CHUNKS];

    struct hashmap_s map; 

    size_t total_free_pages;

    cache_key_t last_key;
    uint8_t *locked_head;
    struct entry_descriptor locked_desc;
    bool locked_read;
    bool verbose;
};



static_assert(sizeof(struct entry_descriptor) == 8, "entry_descriptor size is not 8 bytes");
static struct entry_descriptor EMPTY_DESC = { .chunk = -1, .index = -1 };

static struct lazyfree_cache* cache_new(size_t cache_capacity, mmap_impl_t mmap_impl, madv_impl_t madv_impl) {
    struct lazyfree_cache* cache = malloc(sizeof(struct lazyfree_cache));
    memset(cache, 0, sizeof(struct lazyfree_cache));

    cache->madv_impl = madv_impl;
    cache->cache_capacity = cache_capacity;
    cache->chunk_size = cache_capacity / NUMBER_OF_CHUNKS;
    cache->pages_per_chunk = cache->chunk_size / PAGE_SIZE;
    
    printf("Initiating cache\n");
    printf("Cache capacity: %zuMb\n", cache_capacity/M);
    // printf("Total pages: %zuK\n", NUMBER_OF_CHUNKS * cache->pages_per_chunk/K);
    // printf("Chunk size: %zuMb\n", cache->chunk_size/M);
    // printf("Pages per chunk: %zu\n", cache->pages_per_chunk);
    printf("\n");
    for (size_t i = 0; i < NUMBER_OF_CHUNKS; ++i) {
        // Allocate all the chunks on the start
        void *entries = mmap_impl(cache->chunk_size);

        cache->chunks[i].entries = entries;

        cache->chunks[i].bit0 = bitset_new(cache->pages_per_chunk);
        assert(cache->chunks[i].bit0 != NULL);

        cache->chunks[i].free_pages = malloc(cache->pages_per_chunk * sizeof(uint32_t));
        assert(cache->chunks[i].free_pages != NULL);

        cache->chunks[i].keys = malloc(cache->pages_per_chunk * sizeof(uint64_t));
        assert(cache->chunks[i].keys != NULL);
    }
    cache->total_free_pages = NUMBER_OF_CHUNKS * cache->pages_per_chunk;
    cache->locked_desc = EMPTY_DESC;
    hashmap_create( NUMBER_OF_CHUNKS*cache->pages_per_chunk, &cache->map);
    return cache;
}

void cache_free(struct lazyfree_cache* cache) {
    for (size_t i = 0; i < NUMBER_OF_CHUNKS; ++i) {
        munmap(cache->chunks[i].entries, cache->chunk_size);
        bitset_free(cache->chunks[i].bit0);
        free(cache->chunks[i].free_pages);
        free(cache->chunks[i].keys);
    }
    hashmap_destroy(&cache->map);
    free(cache);
}

// == Hashmap helpers ==
union {
    struct entry_descriptor desc;
    void* ptr;
} u;

static struct entry_descriptor hmap_get(struct lazyfree_cache* cache, cache_key_t key) {
    u.ptr = hashmap_get(&cache->map, &key, sizeof(key));
    if (key == DEBUG_KEY) {
        printf("DEBUG: HASHMAP GET %lu -> %d %d %d\n", key, u.desc.chunk, u.desc.index, u.desc.set);
    }
    if (u.desc.set) {
        return u.desc;
    }
    return EMPTY_DESC;
}

static void hmap_put(struct lazyfree_cache* cache, cache_key_t* key, struct entry_descriptor desc) {
    u.ptr = 0;
    u.desc.chunk = desc.chunk;
    u.desc.index = desc.index;
    u.desc.set = 1;
    if (*key == DEBUG_KEY) {
        printf("DEBUG: HASHMAP PUT %lu -> %d %d %d\n", *key, desc.chunk, desc.index, desc.set);
    }
    hashmap_put(&cache->map, key, sizeof(*key), u.ptr);

    struct entry_descriptor desc2 = hmap_get(cache, *key);
    assert(desc2.set);
}

static void hmap_remove(struct lazyfree_cache* cache, cache_key_t key) {
    if (key == DEBUG_KEY) {
        printf("DEBUG: HASHMAP REMOVE %lu, new size %u\n", key, hashmap_num_entries(&cache->map));
    }
    hashmap_remove(&cache->map, &key, sizeof(key));
}

// == Read lock implementation ==

static void cache_drop(struct lazyfree_cache* cache, struct entry_descriptor desc) {
    // printf("DEBUG: Dropping chunk %d, index %d\n", desc.chunk, desc.index);
    struct chunk* chunk = &cache->chunks[desc.chunk];
    cache_key_t key = chunk->keys[desc.index];
    chunk->free_pages[chunk->free_pages_count++] = desc.index;
    if (chunk->free_pages_count > chunk->len) {
        printf("DEBUG: Free pages count %u is greater than chunk len %u\n", chunk->free_pages_count, chunk->len);
        lazyfree_cache_debug(cache, true);
        exit(1);
    }
    chunk->keys[desc.index] = 0;
    
    cache->total_free_pages++;
    // printf("DEBUG DROP key %lu\n", key);

    if (key == DEBUG_KEY) {
        struct entry_descriptor desc2 = hmap_get(cache, key);
        printf("DEBUG: Dropping key %lu, chunk %d, index %d. Current %d, %d\n", key, desc.chunk, desc.index, desc2.chunk, desc2.index);
    }
    hmap_remove(cache, key);

    // printf("Hmap size: %zu\n", hmlen(cache->map));
}

static bool cache_read_try_lock(struct lazyfree_cache* cache, 
                               cache_key_t key,
                               uint8_t* head,
                               uint8_t** tail) {
    assert(cache->locked_head == NULL);
    assert(cache->locked_desc.chunk == EMPTY_DESC.chunk);
    if (cache->verbose) {
        printf("DEBUG: Locking key %lu for read\n", key);
    }

    struct entry_descriptor desc = hmap_get(cache, key);
    if (key == DEBUG_KEY) {
        printf("DEBUG: Getting key %lu, chunk %d, index %d\n", key, desc.chunk, desc.index);
    }

    if (desc.chunk == -1) {
        if (cache->verbose) {
            printf("Key %lu not found\n", key);
        }
        return false;
    }
    struct chunk* chunk = &cache->chunks[desc.chunk];
    struct discardable_entry* entry = &chunk->entries[desc.index];
    
    
    *head = entry->head;
    if (*head == 0) {
        if (cache->verbose) {
            printf("Key %lu was evicted by kernel\n", key);
        }
        cache_drop(cache, desc);
        return false;
    }

    if (chunk->keys[desc.index] != key) {
        if (cache->verbose) {
            printf("Key %lu was evicted by dropping the chunk\n", key);
        }
        return false;
    }

    if (!bitset_get(chunk->bit0, desc.index)) {
        *head &= ~1ul;
    }

    *tail = entry->tail;
    cache->locked_head = &entry->head;
    cache->locked_desc = desc;
    cache->last_key = key;
    cache->locked_read = true;
    return true;
}

static bool cache_read_lock_check(struct lazyfree_cache* cache) {
    assert(cache->locked_head != NULL);
    assert(cache->locked_desc.chunk != EMPTY_DESC.chunk);
    assert(cache->locked_read);
    if (cache->verbose && !*cache->locked_head) {
        printf("Key was evicted during read\n");
        return false;
    }
    return true;
}

static void cache_read_unlock(struct lazyfree_cache* cache, bool drop) {
    assert(cache->locked_head != NULL);
    assert(cache->locked_desc.chunk != EMPTY_DESC.chunk);
    assert(cache->locked_read);
    if (cache->verbose) {
        printf("DEBUG: Unlocking key read %lu, drop %d\n", cache->last_key, drop);
    }
    if (drop) {
        cache_drop(cache, cache->locked_desc);
    }
    cache->locked_head = NULL;
    cache->locked_desc = EMPTY_DESC;
    cache->locked_read = false;
}


// == Write lock implementation ==

static void drop_random_chunk(struct lazyfree_cache* cache) {
    cache->current_chunk_idx = random_next() % NUMBER_OF_CHUNKS;
    struct chunk* chunk = &cache->chunks[cache->current_chunk_idx];
    printf("DEBUG: Dropping chunk %zu\n", cache->current_chunk_idx);
        
    // if (cache->verbose) {
        lazyfree_cache_debug(cache, false);
    // }
    for (size_t i = 0; i < chunk->len; ++i) {
        hmap_remove(cache, chunk->keys[i]);
    }
    memset(chunk->keys, 0, chunk->len * sizeof(cache_key_t));
    int ret = madvise(chunk->entries, chunk->len * sizeof(struct discardable_entry), MADV_DONTNEED);
    assert(ret == 0);
    
    cache->total_free_pages += (chunk->len - chunk->free_pages_count);
    
    chunk->len = 0;
    chunk->free_pages_count = 0;
}

static void advance_chunk(struct lazyfree_cache* cache) {
    cache->madv_impl(cache->chunks[cache->current_chunk_idx].entries, cache->chunk_size);
    cache->current_chunk_idx = (cache->current_chunk_idx + 1) % NUMBER_OF_CHUNKS;

    // printf("DEBUG: Switched to chunk %zu\n", cache->current_chunk_idx);
} 

static struct entry_descriptor alloc_current_chunk(struct lazyfree_cache* cache) {
    struct chunk* chunk = &cache->chunks[cache->current_chunk_idx];
    struct entry_descriptor desc;
    desc.chunk = cache->current_chunk_idx;

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


static void take_write_lock(struct lazyfree_cache* cache, 
                      struct entry_descriptor desc, 
                      uint8_t** value) {
    struct chunk* chunk = &cache->chunks[desc.chunk];
    struct discardable_entry* entry = &chunk->entries[desc.index];
    cache->locked_desc = desc;
    cache->locked_head = &entry->head;
    *value = (uint8_t*) entry;
}

static bool cache_write_lock(struct lazyfree_cache* cache, 
                            cache_key_t key,
                            uint8_t **value) {
    assert(cache->locked_head == NULL);
    assert(cache->locked_desc.chunk == EMPTY_DESC.chunk);
    assert(!cache->locked_read);
    if (cache->verbose) {
        printf("DEBUG: Locking key %lu for write\n", key);
    }
 
    struct entry_descriptor desc = hmap_get(cache, key);
    if (key == DEBUG_KEY) {
        printf("DEBUG: Putting key %lu, chunk %d, index %d\n", key, desc.chunk, desc.index);
        // cache->verbose = true;
    }
    cache->last_key = key;

    if (desc.chunk != EMPTY_DESC.chunk) {
        if (cache->verbose || key == DEBUG_KEY) {
            printf("Found existing slot for key %lu\n", key);
        }
        struct chunk* chunk = &cache->chunks[desc.chunk];

        if (!bitset_get(chunk->bit0, desc.index)) {
            *cache->locked_head &= ~1ul;
        }

        take_write_lock(cache, desc, value);
        return true;
    }

    
    if (cache->total_free_pages < MIN_FREE_TOTAL_PAGES) {
        if (cache->verbose || key == DEBUG_KEY) {
            printf("No free pages, freeing up random chunk\n");
        }
        drop_random_chunk(cache);
    }

    if (cache->verbose || key == DEBUG_KEY) {
        printf("Allocating from the current chunk\n");
    }

    size_t chunks_visited = 0;
    while(desc.chunk == EMPTY_DESC.chunk) {
        desc = alloc_current_chunk(cache);
        if (desc.chunk != EMPTY_DESC.chunk) {
            // Found a free page
            break;
        }

        advance_chunk(cache);

        if (cache->verbose || key == DEBUG_KEY) {
            printf("Advanced to chunk %zu\n", cache->current_chunk_idx);
        }
        chunks_visited++;
        if (chunks_visited >= NUMBER_OF_CHUNKS) {
            // This means total_free_pages is not updated correctly
            printf("Failed to find free page\n");
            lazyfree_cache_debug(cache, true);
            exit(1);
        }
    }

    if (cache->verbose || key == DEBUG_KEY) {
        printf("Allocated from chunk %d, index %d\n", desc.chunk, desc.index);
    }

    take_write_lock(cache, desc, value);
    return true;
}

static void cache_write_unlock(struct lazyfree_cache* cache, bool drop) {
    assert(cache->locked_desc.chunk != EMPTY_DESC.chunk);
    assert(cache->locked_head != NULL);
    assert(!cache->locked_read);

    if (cache->verbose || cache->last_key == DEBUG_KEY) {
        printf("DEBUG: Unlocking key write %lu, drop %d\n", cache->last_key, drop);
    }

    if (drop) {
        cache_drop(cache, cache->locked_desc);
    } else {
        // Move bit0 from head to bit0
        struct chunk* chunk = &cache->chunks[cache->locked_desc.chunk];        
        bool bit0 = (*cache->locked_head & 1) != 0;
        bitset_put(chunk->bit0, cache->locked_desc.index, bit0);
        if (!bit0) {
            if (bitset_get(chunk->bit0, cache->locked_desc.index)) {
                // Move bit0 from bitset to head
                printf("DEBUG: WTF\n");
            } 
        }
        *cache->locked_head |= 1;

        if (cache->verbose || cache->last_key == DEBUG_KEY) {
            printf("DEBUG: Putting key %lu, chunk %d, index %d\n", cache->last_key, cache->locked_desc.chunk, cache->locked_desc.index);
        }

        chunk->keys[cache->locked_desc.index] = cache->last_key;
        hmap_put(cache, &chunk->keys[cache->locked_desc.index], cache->locked_desc);
    }

    cache->locked_desc = EMPTY_DESC;
    cache->locked_head = NULL;
    cache->locked_read = false;
}

// == Public functions ==
cache_t lazyfree_cache_new(size_t cache_capacity, void* (*mmap_impl)(size_t size), void (*madv_impl)(void *memory, size_t size)) {
    return cache_new(cache_capacity, mmap_impl, madv_impl);
}

void lazyfree_cache_free(cache_t cache) {
    cache_free(cache);
}

bool lazyfree_cache_write_lock(cache_t cache, 
                               cache_key_t key,
                               uint8_t **value) {
    return cache_write_lock(cache, key, value);
}

bool lazyfree_cache_read_try_lock(cache_t cache, 
                                  cache_key_t key,
                                  uint8_t* head,
                                  uint8_t **tail) {
    return cache_read_try_lock(cache, key, head, tail);
}

bool lazyfree_cache_read_lock_check(cache_t cache) {
    return cache_read_lock_check(cache);
}

void lazyfree_cache_unlock(cache_t cache, bool drop) {
    struct lazyfree_cache* lazyfree_cache = (struct lazyfree_cache*) cache;
    if (lazyfree_cache->verbose) {
        printf("DEBUG: Unlocking key %lu, drop %d, locked_read %d\n", 
            lazyfree_cache->last_key, drop, lazyfree_cache->locked_read);
    }
    if (lazyfree_cache->locked_read) {
        cache_read_unlock(lazyfree_cache, drop);
    } else {
        cache_write_unlock(lazyfree_cache, drop);
    }
}

// == Extra API ==
static void print_stats(cache_t cache) {
    struct lazyfree_cache* lazyfree_cache = (struct lazyfree_cache*) cache;
    printf("Htable size: %u\n", hashmap_num_entries(&lazyfree_cache->map));
    printf("Total free pages: %zu\n", lazyfree_cache->total_free_pages);
    for (size_t i = 0; i < NUMBER_OF_CHUNKS; ++i) {
        struct chunk* chunk = &lazyfree_cache->chunks[i];
        float ratio = (float) chunk->free_pages_count / (float) chunk->len;
        printf("Chunk %zu: %u/%u (%.2f%%)\n", i, chunk->free_pages_count, chunk->len, ratio * 100);
       
    }
}

void lazyfree_cache_debug(cache_t cache, bool verbose) {
    struct lazyfree_cache* lazyfree_cache = (struct lazyfree_cache*) cache;
    lazyfree_cache->verbose = verbose;
    print_stats(cache);
}
