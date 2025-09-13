#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#include "random.h"
#include "madv_cache.h"


#define DEBUG_KEY -1ul

static_assert(sizeof(struct entry_descriptor) == 8, "entry_descriptor size is not 8 bytes");
static_assert(CHUNK_SIZE % PAGE_SIZE == 0, "Chunk size must be a multiple of page size");
static_assert(PAGES_PER_CHUNK == 64 * K, "Pages per chunk is not 64K");


const static struct entry_descriptor EMPTY_DESC = { .chunk = -1, .index = -1 };

void madv_cache_init(struct madv_cache* cache) {
    printf("\nInitiating cache\n");
    printf("Cache capacity: %zuGb\n", CACHE_CAPACITY/G);
    printf("Total pages: %zuK\n", NUMBER_OF_CHUNKS * PAGES_PER_CHUNK/K);
    printf("Chunk size: %zuMb\n", CHUNK_SIZE/M);
    printf("Pages per chunk: %zu\n", PAGES_PER_CHUNK);
    printf("\n");

    memset(cache, 0, sizeof(struct madv_cache));
    for (size_t i = 0; i < NUMBER_OF_CHUNKS; ++i) {
        // Allocate all the chunks on the start
        int prot = PROT_READ|PROT_WRITE;
        int flags = MAP_PRIVATE|MAP_ANONYMOUS;
        void *entries = mmap(0, CHUNK_SIZE, prot, flags, -1, 0);
        assert(entries != MAP_FAILED);
        cache->chunks[i].entries = entries;

        cache->chunks[i].first_byte0 = malloc(PAGES_PER_CHUNK/8);
        assert(cache->chunks[i].first_byte0 != NULL);

        cache->chunks[i].free_pages = malloc(PAGES_PER_CHUNK * sizeof(uint32_t));
        assert(cache->chunks[i].free_pages != NULL);

        cache->chunks[i].keys = malloc(PAGES_PER_CHUNK * sizeof(uint64_t));
        assert(cache->chunks[i].keys != NULL);
    }
    cache->total_free_pages = NUMBER_OF_CHUNKS * PAGES_PER_CHUNK;
    hmdefault(cache->map, EMPTY_DESC);
}

void madv_cache_free(struct madv_cache* cache) {
    for (size_t i = 0; i < NUMBER_OF_CHUNKS; ++i) {
        munmap(cache->chunks[i].entries, CHUNK_SIZE);
    }
    hmfree(cache->map);
}

void madv_cache_print_stats(struct madv_cache* cache) {
    for (size_t i = 0; i < NUMBER_OF_CHUNKS; ++i) {
        struct chunk* chunk = &cache->chunks[i];
        float ratio = (float) chunk->free_pages_count / (float) chunk->len;
        printf("Chunk %zu: %u/%u (%.2f%%)\n", i, chunk->free_pages_count, chunk->len, ratio * 100);
       
    }
}


static struct discardable_entry* alloc_page(struct madv_cache* cache) {
    struct chunk* chunk = &cache->chunks[cache->current_chunk_idx];
    
    if (chunk->len < PAGES_PER_CHUNK) {
        // We have blank pages
        return &chunk->entries[chunk->len++];
    }
    
    if (chunk->free_pages_count == 0) {
        return NULL;
    }

    uint32_t idx = chunk->free_pages[chunk->free_pages_count--];
    return &chunk->entries[idx];
}

static void free_page(struct chunk* chunk, struct discardable_entry* page) {
    uint32_t idx = page - chunk->entries;
    
    chunk->free_pages[chunk->free_pages_count++] = idx;
}

static bool get_bit(struct chunk* chunk, uint32_t idx) {
    return (chunk->first_byte0[idx/8] & (1 << (idx % 8))) != 0;
}

static void set_bit(struct chunk* chunk, uint32_t idx) {
    uint8_t mask = 1 << (idx % 8);
    chunk->first_byte0[idx/8] |= mask;
}


static void reset_bit(struct chunk* chunk, uint32_t idx) {
    uint8_t mask = 1 << (idx % 8);
    chunk->first_byte0[idx/8] &= ~mask;
}

bool madv_cache_get(struct madv_cache* cache, uint64_t key, uint8_t* value) {     
    struct entry_descriptor desc = hmget(cache->map, key);

    if (key == DEBUG_KEY) {
        printf("DEBUG: Getting key %lu, chunk %d, index %d\n", key, desc.chunk, desc.index);
    }
    
    if (desc.chunk == -1) {
        errno = EINVAL;
        return false;
    }
    struct chunk* chunk = &cache->chunks[desc.chunk];
    struct discardable_entry* entry = &chunk->entries[desc.index];
    if (entry->head == 0) {
        if (cache->verbose) {
            printf("Key %lu was reused\n", key);
        }
        errno = EKEYEXPIRED;
        // Evict
        free_page(chunk, entry);
        hmdel(cache->map, key);
        return false;
    }
    
    // Copy discardable data
    memcpy(value, entry, PAGE_SIZE);

    
    // Have to check again.
    if (!entry->head) {
        printf("Key %lu was evicted during memcpy!\n", key);
        errno = EBUSY;
        return false;
    }


    // if (get_bit(chunk, desc.index)) {
    //     value[0] = 0;
    // }

    return true;
}


static void advance_chunk(struct madv_cache* cache) {
    int ret = madvise(cache->chunks[cache->current_chunk_idx].entries, CHUNK_SIZE, MADV_FREE);
    // printf("madvise for chunk %d returned %d\n", cache->current_chunk, ret);
    assert(ret == 0);

    cache->current_chunk_idx = (cache->current_chunk_idx + 1) % NUMBER_OF_CHUNKS;

    // print_stats(cache);
    // printf("DEBUG: Switched to chunk %zu\n", cache->current_chunk_idx);
} 

void cache_put(struct madv_cache* cache, struct entry_descriptor desc, const uint8_t* value) {
    struct chunk* chunk = &cache->chunks[desc.chunk];
    struct discardable_entry* entry = &chunk->entries[desc.index];
    // Write data
    memcpy(entry, value, PAGE_SIZE);

    if (entry->head == 0) {
        set_bit(chunk, desc.index);
        entry->head = 1;
    } else {
        reset_bit(chunk, desc.index);
    }
}

static struct discardable_entry* alloc_any(struct madv_cache* cache) {
    cache->current_chunk_idx = random_next() % NUMBER_OF_CHUNKS;
    uint32_t idx = random_next() % PAGES_PER_CHUNK;
    struct chunk* chunk = &cache->chunks[cache->current_chunk_idx];
    struct discardable_entry* page = &chunk->entries[idx];
    uint64_t key =  chunk->keys[idx];

    // Evict
    free_page(chunk, page);
    hmdel(cache->map, key);
    return page;
}
    


void madv_cache_put(struct madv_cache* cache, uint64_t key, const uint8_t* value) { 
    struct entry_descriptor desc = hmget(cache->map, key);
    if (key == DEBUG_KEY) {
        printf("DEBUG: Putting key %lu, chunk %d, index %d\n", key, desc.chunk, desc.index);
    }
    if (desc.chunk != -1) {
        cache_put(cache, desc, value);
        if (cache->verbose) {
            printf("Found existing slot for key %lu\n", key);
        }
    }

    struct discardable_entry* entry = NULL;
    if (cache->total_free_pages == 0) {
        entry = alloc_any(cache);
        assert(entry != NULL);
    }

    size_t chunks_visited = 0;
    while(entry == NULL) {
        entry = alloc_page(cache);
        if (entry != NULL) {
            break;
        }
        
        advance_chunk(cache);

        if (cache->verbose) {
            printf("Advanced to chunk %zu\n", cache->current_chunk_idx);
        }
        chunks_visited++;
        if (chunks_visited >= NUMBER_OF_CHUNKS) {
            printf("Failed to find free page\n");
            exit(1);
        }
    }

    desc.chunk = cache->current_chunk_idx;
    desc.index = entry - cache->chunks[desc.chunk].entries;
    hmput(cache->map, key, desc);

    cache_put(cache, desc, value);
    // Sanity-check the value we just inserted
    {
        uint8_t value2[PAGE_SIZE];
        bool res = madv_cache_get(cache, key, value2);
        assert(res);
        assert(memcmp(value, value2, PAGE_SIZE) == 0);
    }
}

void madv_cache_evict(struct madv_cache* cache, uint64_t key) {
    struct entry_descriptor desc = hmget(cache->map, key);
    if (desc.chunk == -1) {
        return;
    }
    struct chunk* chunk = &cache->chunks[desc.chunk];
    struct discardable_entry* entry = &chunk->entries[desc.index];
    free_page(chunk, entry);
    hmdel(cache->map, key);
}
