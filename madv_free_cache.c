#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#include "madv_free_cache.h"


static void set_current_chunk(struct madv_free_cache* cache, size_t idx) {
    cache->current_chunk_idx = idx;
    cache->current_entry_idx = 0;
    cache->next_chunk_idx = (idx + 1) % NUMBER_OF_CHUNKS;
    cache->next_chunk_first_slot_idx = 0;
    cache->next_chunk_last_entry_idx = PAGES_PER_CHUNK-1;
}

void madv_cache_init(struct madv_free_cache* cache) {
    static_assert(CHUNK_SIZE % PAGE_SIZE == 0, "Chunk size must be a multiple of page size");
    printf("Memory limit: %zu Gb\n", MEMORY_LIMIT/G);
    printf("Total pages: %zu\n", NUMBER_OF_CHUNKS * PAGES_PER_CHUNK);
    printf("Chunk size: %zu Mb\n", CHUNK_SIZE/M);
    printf("Pages per chunk: %zu\n", PAGES_PER_CHUNK);
    printf("\n");

    memset(cache, 0, sizeof(struct madv_free_cache));
    for (int i = 0; i < NUMBER_OF_CHUNKS; ++i) {
        // Allocates all the chunks on the start
        int prot = PROT_READ|PROT_WRITE;
        int flags = MAP_PRIVATE|MAP_ANONYMOUS;
        char *chunk = mmap(0, CHUNK_SIZE, prot, flags, -1, 0);
        assert(chunk != MAP_FAILED);
        cache->chunks[i] = (discardable_entry*) chunk;
    }
    set_current_chunk(cache, 0);
}

void madv_cache_free(struct madv_free_cache* cache) {
    for (int i = 0; i < NUMBER_OF_CHUNKS; ++i) {
        munmap(cache->chunks[i], CHUNK_SIZE);
    }
    hmfree(cache->map);
}

static bool compact_next_chunk(struct madv_free_cache* cache) {
    if(cache->next_chunk_last_entry_idx <= cache->next_chunk_first_slot_idx) {
        // Already compacted
        return true;
    }

    discardable_entry* chunk = cache->chunks[cache->next_chunk_idx];

    bool next_time = false;
    if (chunk[cache->next_chunk_first_slot_idx].key != 0) {
        // Slot is still busy
        cache->next_chunk_first_slot_idx++;
        next_time = true;
    }

    if (chunk[cache->next_chunk_last_entry_idx].key == 0) {
        // Chunk is empty
        cache->next_chunk_last_entry_idx--;
        next_time = true;
    }
    
    if (next_time) {
        return false;
    }

    entry_descriptor desc = hmget(cache->map, chunk[cache->next_chunk_first_slot_idx].key);
    if (desc.cnt_compacted > 0 && desc.cnt_get == 0) {
        // This entry was compacted, but never got accessed
        // No need to compact it
        // chunk[cache->next_chunk_last_entry_idx].key = 0;
        // cache->next_chunk_last_entry_idx--;
        // printf("Skipping compacting entry %lu\n", chunk[cache->next_chunk_first_slot_idx].key);
        // return true;
    }


    // Move the last entry to the first slot
    memmove(&chunk[cache->next_chunk_first_slot_idx], &chunk[cache->next_chunk_last_entry_idx], PAGE_SIZE);
    
    desc.cnt_compacted = 1;
    desc.index = cache->next_chunk_first_slot_idx;
    hmput(cache->map, chunk[cache->next_chunk_first_slot_idx].key, desc);

    cache->next_chunk_first_slot_idx++;
    cache->next_chunk_last_entry_idx--;
    return true;
}

void madv_cache_print_stats(struct madv_free_cache* cache) {
    for (size_t i = 0; i < NUMBER_OF_CHUNKS; ++i) {
        uint64_t empty = 0;
        discardable_entry* chunk = cache->chunks[i];
        for (size_t j = 0; j < PAGES_PER_CHUNK; ++j) {
            if (chunk[j].key == 0) {
                empty++;
            } 
        }
        printf("Chunk %zu: %zu/%zu (%.2f%%) empty\n", i, empty, PAGES_PER_CHUNK, (float) empty / (float) PAGES_PER_CHUNK * 100);
    }
}


static void advance_chunk(struct madv_free_cache* cache) {
    // printf("DEBUG: Switching to chunk %zu\n", cache->next_chunk_idx);
    // print_stats(cache);
    while (!compact_next_chunk(cache)) {
        // Complete the compaction
    }

    int ret = madvise(cache->chunks[cache->current_chunk_idx], CHUNK_SIZE, MADV_FREE);
    // printf("madvise for chunk %d returned %d\n", cache->current_chunk, ret);
    assert(ret == 0);

    set_current_chunk(cache, cache->next_chunk_idx);
}


static discardable_entry* find_free_slot(struct madv_free_cache* cache) {
    while(true) {
        discardable_entry* slot = cache->chunks[cache->current_chunk_idx] + cache->current_entry_idx;
        if (slot->key == 0) {
            compact_next_chunk(cache);
            return slot;
        }

        cache->current_entry_idx++;
        // printf("Current entry idx: %zu\n", cache->current_entry_idx);
        if (cache->current_entry_idx == PAGES_PER_CHUNK) {
            // Can't find a free slot
            return NULL;
        }
    }
}

static discardable_entry* get_entry(struct madv_free_cache* cache, uint64_t key, entry_descriptor* desc_out) {
    entry_descriptor desc = hmget(cache->map, key);
    memcpy(desc_out, &desc, sizeof(entry_descriptor));
    if (desc.cnt_put == 0) {
        return NULL;
    }
    
    discardable_entry* entry = &cache->chunks[desc.chunk][desc.index];
    if (entry->key != key) {
        // This key is used for something else now
        return NULL;
    }
    return entry;
}
    

    

void do_put(entry_descriptor *desc, discardable_entry* slot, const uint8_t* value) {
    if ((void*) &desc->extra_value != (void*) desc->extra_value) {
        printf("DEBUG: p1: %p, p2: %p\n", &desc->extra_value, desc->extra_value);
        exit(1);
    }
    memcpy(&desc->extra_value, value, STORED_EXTRA);
    memcpy(slot->value, value+STORED_EXTRA, STORED_DISCARDABLE);
}

bool madv_cache_put(struct madv_free_cache* cache, uint64_t key, const uint8_t* value) {
    if (key == 0) {
        // key 0 is special case
        cache->key_zero_set = true;    
        memcpy(cache->key_zero, value, ENTRY_SIZE);
        return true;
    }
    entry_descriptor desc;
    // printf("DEBUG: sizeof entry_descriptor: %zu\n", sizeof(entry_descriptor));
    discardable_entry* slot = get_entry(cache, key, &desc);
    if (slot != NULL) {
        // Found existing slot
        // printf("Found existing slot for key %lu\n", key);
        do_put(&desc, slot, value);
        return true;
    }
    
    slot = find_free_slot(cache);
    if (slot == NULL) {
        advance_chunk(cache);
        // Try again on a full next chunk
        slot = find_free_slot(cache);
        if (slot == NULL) {
            advance_chunk(cache);
            printf("No more space in the cache. Increase memory pressure or memory limit!\n");
            return false;
        }
    }
    
    // Found new slot
    slot->key = key;

    memset(&desc, 0, sizeof(entry_descriptor));
    desc.cnt_put = 1;
    desc.chunk = cache->current_chunk_idx;
    desc.index = cache->current_entry_idx;

    do_put(&desc, slot, value);
    hmput(cache->map, key, desc);

    
    if (key==2015046830383809606ul) {
        printf("DEBUG: Put key %lu to chunk %d, index %d. Extra value: %s\n", key, desc.chunk, desc.index, desc.extra_value);
    }

    // Sanity-check the value we just inserted
    {
        // entry_descriptor chk = hmget(cache->map, key);
        // assert(chk.cnt_put == 1);
        // assert(chk.chunk == desc.chunk);
        // assert(chk.index == desc.index);

        // uint8_t value2[ENTRY_SIZE];
        // int res = madv_cache_get(cache, key, value2);
        // assert(res==0);
        // assert(memcmp(value, value2, ENTRY_SIZE) == 0);
    }
    return true;
}   


int madv_cache_get(struct madv_free_cache* cache, uint64_t key, uint8_t* value) {
    if (key == 0) {
        if (!cache->key_zero_set) {
            // Never seen a zero key
            return -1;
        }
        memmove(value, cache->key_zero, ENTRY_SIZE);
        return 0;
    }
    
    entry_descriptor desc;
    discardable_entry* entry = get_entry(cache, key, &desc);
    if (desc.cnt_put == 0) {
        printf("Key %lu is not in use!\n", key);
        return -1;
    }
    if (key==2015046830383809606ul) {
        printf("DEBUG: Get key %lu from chunk %d, index %d, entry: %p, Extra value: %s\n", key, desc.chunk, desc.index, entry, desc.extra_value);
    }
    if (entry == NULL) {
        // Was evicted
        // printf("Key %lu was evicted some time ago!\n", key);
        return 1;
    }
    // printf("Got key %lu from chunk %d, index %d, extra value: %s\n", key, desc.chunk, desc.index, desc.extra_value);
    
    memcpy(value, desc.extra_value, STORED_EXTRA);
    
    // Have to check again.
    if (entry->key != key) {
        printf("Key %lu was evicted just now!\n", key);
        return 1;
    }

    desc.cnt_get = 1;
    hmput(cache->map, key, desc);

    memcpy(value+STORED_EXTRA, entry->value, STORED_DISCARDABLE);
    
    return 0;
}