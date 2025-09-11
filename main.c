#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#define PAGE_SIZE 4096
#define ENTRY_SIZE 4096

typedef struct pcache pcache;

// memory_limit must be divisible by 512kb and be less than 512gb
// it can be more than actual memory
void pcache_init(pcache* cache);

// value must point to the ENTRY_SIZE bytes of memory
// they will be stored in purgable memory
void pcache_put(pcache* cache, uint64_t key, uint8_t* value, size_t max_search);

// if the content wasn't evicted get will write entry content to *value if it wasn't evicted 
// and return true
// Otherwise, it will set *value to zero and return false
bool pcache_get(pcache* cache, uint64_t key, uint8_t* value);



#define K 1024ul
#define M (K*K)
#define G (K*M)

#define MEMORY_LIMIT (10*G)
#define NUMBER_OF_CHUNKS 16


#define CHUNK_SIZE (MEMORY_LIMIT / NUMBER_OF_CHUNKS)
#define PAGES_PER_CHUNK (CHUNK_SIZE / PAGE_SIZE)

#define STORED_PURGABLE (PAGE_SIZE - sizeof(uint64_t))
#define STORED_EXTRA  (ENTRY_SIZE - STORED_PURGABLE)

typedef struct {
    uint64_t key;
    uint8_t value[STORED_PURGABLE];
} purgable_entry;

typedef struct {
    bool in_use;

    uint8_t chunk;
    uint32_t index;

    uint8_t extra_value[STORED_EXTRA];
} descriptor;

struct pcache {
    purgable_entry* chunks[NUMBER_OF_CHUNKS];
    purgable_entry* next_slot;
    size_t current_chunk;
    size_t current_idx;

    uint8_t zero_key[ENTRY_SIZE];
    struct { uint64_t key; descriptor value; } *map; 
};


void pcache_init(pcache* cache) {
    static_assert(CHUNK_SIZE % PAGE_SIZE == 0);
    printf("Pages per chunk: %zu\n", PAGES_PER_CHUNK);
    printf("Total capacity: %zu\n", NUMBER_OF_CHUNKS * PAGES_PER_CHUNK);

    memset(cache, 0, sizeof(pcache));
    for (int i = 0; i < NUMBER_OF_CHUNKS; ++i) {
        int prot = PROT_READ|PROT_WRITE;
        int flags = MAP_PRIVATE|MAP_ANONYMOUS;
        char *chunk = mmap(0, CHUNK_SIZE, prot, flags, -1, 0);
        assert(chunk != MAP_FAILED);
        cache->chunks[i] = (purgable_entry*) chunk;
    }
    cache->next_slot = cache->chunks[0];
}

void pache_free(pcache* cache) {
    for (int i = 0; i < NUMBER_OF_CHUNKS; ++i) {
        munmap(cache->chunks[i], CHUNK_SIZE);
    }
    hmfree(cache->map);
}


void advance_slot(pcache* cache) {
    cache->current_idx++;
    cache->next_slot++;
    if (cache->current_idx == PAGES_PER_CHUNK) {
        // int ret = madvise(cache->chunks[cache->current_chunk], CHUNK_SIZE, MADV_DONTNEED);
        // printf("madvise for chunk %d returned %d\n", cache->current_chunk, ret);
        // assert(ret == 0);

        cache->current_chunk = (cache->current_chunk + 1) % NUMBER_OF_CHUNKS;
        cache->current_idx = 0;
        cache->next_slot = cache->chunks[cache->current_chunk];
        // printf("Switched to chunk %zu\n", cache->current_chunk);
    }
}

void pcache_put(pcache* cache, uint64_t key, uint8_t* value, size_t max_search) {
    if (key == 0) {
        memmove(cache->zero_key, value, ENTRY_SIZE);
        return;
    }
    for (size_t i = 0; i < max_search; ++i) {
        if (cache->next_slot->key == 0) {
            break;
        }
        advance_slot(cache);
    }
    // uint64_t old = cache->next_slot->key;
    // if (old != 0) hmdel(cache->map, old);

    cache->next_slot->key = key;
    memmove(cache->next_slot->value, value, STORED_PURGABLE);

    descriptor desc = {
        .in_use = true,
        .chunk = cache->current_chunk,
        .index = cache->current_idx,
        .extra_value = {0},
    };
    memmove(desc.extra_value, value + STORED_PURGABLE, STORED_EXTRA);

    

    hmput(cache->map, key, desc);
    // Sanity-check the value we just inserted
    {
        descriptor chk = hmget(cache->map, key);
        assert(chk.in_use);
        assert(chk.chunk == desc.chunk);
        assert(chk.index == desc.index);
    }
    

    advance_slot(cache);
}   


bool pcache_get(pcache* cache, uint64_t key, uint8_t* value) {
    if (key == 0) {
        memmove(value, cache->zero_key, ENTRY_SIZE);
        return true;
    }
    descriptor desc = hmget(cache->map, key);
    // if (key == 2) {
    //     printf("Get %lu %lu/%lu\n", key, desc.chunk, desc.index);
    // }

    if (!desc.in_use) {
        return false;
    }
    
    purgable_entry* entry = &cache->chunks[desc.chunk][desc.index];
    if (entry->key != key) {
        // This entry was evicted
        return false;
    }
    memmove(value, entry->value, STORED_PURGABLE);
    if (entry->key != key) {
        printf("Evicted just now!\n");
        // This entry was evicted just now!
        return false;
    }

    memmove(value + STORED_PURGABLE, desc.extra_value, STORED_EXTRA);
    return true;
}

void pcache_put_str(pcache* cache, uint64_t key, const char* value) {
    assert(strlen(value) < STORED_PURGABLE);
    pcache_put(cache, key, (uint8_t*) value, 0);
}

void smoke_test(void) {
    pcache cache;
    pcache_init(&cache);



    pcache_put_str(&cache, 1, "Hello");
    pcache_put_str(&cache, 2, "World");
    char value[ENTRY_SIZE];
    bool res = pcache_get(&cache, 1, (uint8_t*) value);
    assert(res);
    assert(strcmp(value, "Hello") == 0);
    printf("value: %s\n", value);


    res = pcache_get(&cache, 2, (uint8_t*) value);
    assert(res);
    assert(strcmp(value, "World") == 0);
    printf("value: %s\n", value);

    pache_free(&cache);
}

int put_idx = 0;

void put_test(pcache* cache, size_t num) {
    put_idx++;
    for (size_t i = 0; i < num; ++i) {
        char value[ENTRY_SIZE];
        sprintf(value, "Value %d:%zu", put_idx, i);
        pcache_put_str(cache, i, value);
        // if (i % 10000 == 0) {
            // printf("ASDSDD %d, %p %zu values\n", put_idx, cache->map, i);
        // }
    }
    printf("Put %zu values\n", num);
    printf("map pointer: %p\n", (void*) cache->map);
}

void get_test(pcache* cache, size_t num, bool fix) {
    size_t found = 0;
    for (size_t i = 0; i < num; ++i) {
        char expected[ENTRY_SIZE];
        sprintf(expected, "Value %d:%zu", put_idx, i);

        char value[ENTRY_SIZE];
        bool res = pcache_get(cache, i, (uint8_t*) value);
        found += res;
        // printf("value: %s\n", value);
        if(res && strcmp(value, expected) != 0) {
            printf("Expected %s, got %s\n", expected, value);
            exit(1);
        }
        if (!res && fix) {
            pcache_put_str(cache, i, expected);
        }
    }
    printf("Got %zu of %zu values\n", found, num);
}

void put_get_test(pcache *cache, size_t iterations, size_t num) {
    for (int i = 0; i < iterations; ++i) {
        put_test(cache, num);
        get_test(cache, num, false);
        printf("Iteration %d done\n", i);
        sleep(1);
    }
}

void run_put_get_tests(size_t iterations, size_t num){
    pcache cache;
    pcache_init(&cache);
    put_get_test(&cache, iterations, num);
    pache_free(&cache);
}

void flush_then_small_test(void) {
    printf("\nStarting flush then small test\n\n");
    pcache cache;
    pcache_init(&cache);
    put_get_test(&cache, 5, 2000*1000);
    printf("Now small\n\n");  

    pcache_put_str(&cache, 1, "Hello");
    char value[ENTRY_SIZE];
    bool res = pcache_get(&cache, 1, (uint8_t*) value);
    assert(res);
    assert(strcmp(value, "Hello") == 0);
    

    sleep(1);
    put_test(&cache, 10*1000);
    for (int i = 0; i < 10; ++i) {
        get_test(&cache, 10*1000, true);
        sleep(1);
    }
    pache_free(&cache);
}

int main() {
    smoke_test();
    run_put_get_tests(1, 1000);
    run_put_get_tests(1, 100*1000);

    // run_put_get_tests(10, 1000*1000);
    flush_then_small_test();
    return 0;
}