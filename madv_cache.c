#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#include "madv_cache.h"


#define DEBUG_KEY -1ul

static_assert(sizeof(struct entry_descriptor) == 8, "entry_descriptor size is not 8 bytes");
static_assert(CHUNK_SIZE % PAGE_SIZE == 0, "Chunk size must be a multiple of page size");
static_assert(STORED_EXTRA==8, "STORED_EXTRA is not 8 bytes");
static_assert(PAGES_PER_CHUNK == 256 * K, "Pages per chunk is not 256K");
    
void madv_cache_init(struct madv_cache* cache) {
    printf("\nInitiating cache\n");
    printf("Memory limit: %zu Gb\n", MEMORY_LIMIT/G);
    printf("Total pages: %zu\n", NUMBER_OF_CHUNKS * PAGES_PER_CHUNK);
    printf("Chunk size: %zu Mb\n", CHUNK_SIZE/M);
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
        cache->chunks[i].extra_values = malloc(PAGES_PER_CHUNK * sizeof(struct extra_value));
        assert(cache->chunks[i].extra_values != NULL);
    }
    cache->next_chunk_idx = 0;
    cache->compacted_suffix = PAGES_PER_CHUNK;
}

void madv_cache_free(struct madv_cache* cache) {
    for (size_t i = 0; i < NUMBER_OF_CHUNKS; ++i) {
        munmap(cache->chunks[i].entries, CHUNK_SIZE);
        free(cache->chunks[i].extra_values);
    }
    hmfree(cache->map);
}

void madv_cache_print_stats(struct madv_cache* cache) {
    for (size_t i = 0; i < NUMBER_OF_CHUNKS; ++i) {
        uint64_t empty = 0;
        struct discardable_entry* chunk = cache->chunks[i].entries;
        for (size_t j = 0; j < PAGES_PER_CHUNK; ++j) {
            if (chunk[j].key == 0) {
                empty++;
            } 
        }
        printf("Chunk %zu: %zu/%zu (%.2f%%) empty\n", i, empty, PAGES_PER_CHUNK, (float) empty / (float) PAGES_PER_CHUNK * 100);
    }
}


static struct discardable_entry* alloc_page(struct madv_cache* cache) {
    struct chunk* chunk = &cache->chunks[cache->current_chunk_idx];
    
    
    if (chunk->len < PAGES_PER_CHUNK) {
        // We have blank pages
        return &chunk->entries[chunk->len++];
    }

    struct hazard_page* hazard_page = chunk->free_page_list;
    if (hazard_page != NULL) {
        chunk->free_page_list = hazard_page->next_page;
        
    }
    return (struct discardable_entry*) hazard_page;
}

static void free_page(struct chunk* chunk, struct discardable_entry* page) {
    struct hazard_page* hazard_page = (struct hazard_page*) page;
    
    hazard_page->next_page = chunk->free_page_list;
    chunk->free_page_list = hazard_page;
}


// struct context {
//     struct madv_cache* cache;
//     uint64_t key;
//     struct entry_descriptor desc;
//     struct discardable_entry* entry;
// };

// static void clear_entry(struct context* ctx) {
//     free_page(&ctx->cache->chunks[ctx->desc.chunk], ctx->entry);
//     // hmdel(ctx->cache->map, ctx->key);
//     ctx->entry = NULL;
// }

// static void find_entry(struct context* ctx) {
//     ctx->desc = 
//     if (ctx->desc.set == 0) {
//         return;
//     }
//     ctx->entry = &ctx->cache->chunks[ctx->desc.chunk].entries[ctx->desc.index];
//     if (ctx->entry->key != ctx->key) {
//         clear_entry(ctx);
//         return;
//     }
// }

void print_desc(struct entry_descriptor desc) {
    printf("set %u, chunk %u, index %u\n", desc.set, desc.chunk, desc.index);
}

bool madv_cache_get(struct madv_cache* cache, uint64_t key, uint8_t* value) {
    if (key == 0) {
        // Special case
        if (!cache->key_zero_set) {
            // Never seen a zero key
            return false;
        }
        memmove(value, cache->key_zero, ENTRY_SIZE);
        return true;
    }

     
    struct entry_descriptor desc = hmget(cache->map, key);

    if (key == DEBUG_KEY) {
        printf("DEBUG: Getting key %lu: ", key);
        print_desc(desc);
    }
    
    if (desc.set == 0) {
        if (cache->verbose) {
            printf("Descriptor for key %lu not found\n", key);
        }
        return false;
    }
    struct chunk* chunk = &cache->chunks[desc.chunk];
    
    struct extra_value* extra_value = &chunk->extra_values[desc.index];
    if (extra_value->key != key) {
        if (cache->verbose) {
            printf("Key %lu was reused\n", key);
        }
        return false;
    }
  
    struct discardable_entry* entry = &chunk->entries[desc.index];
    if (entry->key != key) {
        if (cache->verbose) {
            printf("Key %lu was evicted\n", key);
        }
        return false;
    } 
  

    // Copy discardable data
    memcpy(value+STORED_EXTRA, entry->value, STORED_DISCARDABLE);
    
    // Have to check again.
    if (entry->key != key) {
        printf("Key %lu was evicted during memcpy!\n", key);
        return false;
    }
    
    // Copy extra data
    memcpy(value, extra_value->value, STORED_EXTRA);
    
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

static void compact(struct madv_cache* cache) {
    struct chunk* current_chunk = &cache->chunks[cache->current_chunk_idx];
    struct chunk* next_chunk = &cache->chunks[cache->next_chunk_idx];
    
    // We want to have as much entries compacted as we have busy pages in the current chunk
    // That means when we switch the chunk, next one will be fully compacted
    size_t target_suffix = PAGES_PER_CHUNK - current_chunk->len;
    while (cache->compacted_suffix > target_suffix) {
        cache->compacted_suffix--;
        struct extra_value* extra_value = &next_chunk->extra_values[cache->compacted_suffix];
        if (extra_value->key == 0) {
            continue;
        }

        struct entry_descriptor desc = hmget(cache->map, extra_value->key);
        if (extra_value->key == DEBUG_KEY) {
            printf("DEBUG: Compacting key %lu: ", extra_value->key);
            print_desc(desc);
        }
        assert(desc.set == 1); // If we have extra data, it must be in the map

        struct discardable_entry* entry = &current_chunk->entries[desc.index];
        if (entry->key == extra_value->key) {
            // This one is not evcited, no need to compact
            continue;
        }

        free_page(current_chunk, entry);
        extra_value->key = 0;
        hmdel(cache->map, extra_value->key);
    }
}
 

void cache_put(struct madv_cache* cache, uint64_t key, struct entry_descriptor desc, const uint8_t* value) {
    struct chunk* chunk = &cache->chunks[desc.chunk];

    // Write data
    memcpy(chunk->extra_values[desc.index].value, value, STORED_EXTRA);
    memcpy(chunk->entries[desc.index].value, value+STORED_EXTRA, STORED_DISCARDABLE);

    // Set keys
    chunk->entries[desc.index].key = key;
    chunk->extra_values[desc.index].key = key;

    compact(cache);

    cache->mem_upper_bound += ENTRY_SIZE;
    if (cache->mem_upper_bound > MEM_HIGH) {
        // Do a mmap allocation to free up some memory
        // int prot = PROT_READ|PROT_WRITE;
        // int flags = MAP_PRIVATE|MAP_ANONYMOUS;
        // int len = MEM_HIGH - MEM_TARGET;
        // void * ret = mmap(0, 1, prot, flags, -1, 0);
        // assert(ret != MAP_FAILED);
        // munmap(ret, 1);
        // cache->mem_upper_bound -= len;
    }

    // Sanity-check the value we just inserted
    {
        // struct entry_descriptor chk = hmget(cache->map, key);
        // assert(chk.set == 1);
        // assert(chk.chunk == ctx.desc.chunk);
        // assert(chk.index == ctx.desc.index);

        // uint8_t value2[ENTRY_SIZE];
        // int res = madv_cache_get(cache, key, value2);
        // assert(res==0);
        // assert(memcmp(value, value2, ENTRY_SIZE) == 0);
    }
}

bool madv_cache_put(struct madv_cache* cache, uint64_t key, const uint8_t* value) {
    if (key == 0) {
        // key 0 is special case
        cache->key_zero_set = true;    
        memcpy(cache->key_zero, value, ENTRY_SIZE);
        return true;
    }
    
    struct entry_descriptor desc = hmget(cache->map, key);
    if (key == DEBUG_KEY) {
        printf("DEBUG: Putting key %lu: ", key);
        print_desc(desc);
    }
    if (desc.set == 1) {
        cache_put(cache, key, desc, value);
        if (cache->verbose) {
            printf("Found existing slot for key %lu\n", key);
            uint8_t value2[ENTRY_SIZE];
            assert(madv_cache_get(cache, key, value2));
            assert(memcmp(value, value2, ENTRY_SIZE) == 0);
        }
        return true;
    }

    size_t chunks_to_look = 2; // Current + full next

    while(chunks_to_look > 0) {
        struct discardable_entry* entry = alloc_page(cache);
        if (entry == NULL) {
            advance_chunk(cache);
            chunks_to_look--;
            continue;
        }

        desc.set = 1;
        desc.chunk = cache->current_chunk_idx;
        desc.index = entry - cache->chunks[desc.chunk].entries;
        hmput(cache->map, key, desc);

        cache_put(cache, key, desc, value);
        return true;
    }

    return false;
}
