#include <assert.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hashmap.h"

#include "lazyfree_cache.h"

#include "bitset.h"
#include "random.h"


#define DEBUG_KEY 1609906849761488ul
// #define DEBUG_KEY -1ul

#define NUMBER_OF_CHUNKS 16
static_assert(NUMBER_OF_CHUNKS < (1 << 7), "Too many chunks");

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
static_assert(sizeof(struct entry_descriptor) == 8, "entry_descriptor size is not 8 bytes");

static struct entry_descriptor EMPTY_DESC = { .chunk = -1, .index = -1 };

struct chunk {
    struct discardable_entry* entries; // anonymous mmap size=CHUNK_SIZE
    bitset_t bit0;                     // malloc size=PAGES_PER_CHUNK/8 
    uint32_t* free_pages;              // malloc size=PAGES_PER_CHUNK
    lazyfree_key_t* keys;              // malloc size=PAGES_PER_CHUNK
    uint32_t free_pages_count;
    uint32_t len;
};

struct lazyfree_cache {
    lazyfree_madv_impl_t madv_impl;
    size_t cache_capacity;

    struct chunk chunks[NUMBER_OF_CHUNKS];
    size_t pages_per_chunk;
    size_t chunk_size;
    size_t current_chunk_idx;

    struct hashmap_s map; 

    size_t total_free_pages;
    lazyfree_key_t last_key;

    // Lock state
    struct discardable_entry* locked_entry;
    struct entry_descriptor locked_desc;
    bool locked_read;
    bool verbose;
};

static struct lazyfree_cache* cache_new(size_t cache_capacity, lazyfree_mmap_impl_t mmap_impl, lazyfree_madv_impl_t madv_impl) {
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

static union {
    struct entry_descriptor desc;
    void* ptr;
} u;

static struct entry_descriptor hmap_get(struct lazyfree_cache* cache, lazyfree_key_t key) {
    u.ptr = hashmap_get(&cache->map, &key, sizeof(key));
    if (key == DEBUG_KEY) {
        printf("DEBUG: HASHMAP GET %lu -> %d %d %d\n", key, u.desc.chunk, u.desc.index, u.desc.set);
    }
    if (u.desc.set) {
        return u.desc;
    }
    return EMPTY_DESC;
}

static void hmap_put(struct lazyfree_cache* cache, lazyfree_key_t* key, struct entry_descriptor desc) {
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

static void hmap_remove(struct lazyfree_cache* cache, lazyfree_key_t key) {
    if (key == DEBUG_KEY) {
        printf("DEBUG: HASHMAP REMOVE %lu, new size %u\n", key, hashmap_num_entries(&cache->map));
    }
    hashmap_remove(&cache->map, &key, sizeof(key));
}

// == Bitset helpers

static void bit_to_head(struct chunk* chunk, struct entry_descriptor desc, uint8_t* head) {
    if (!bitset_get(chunk->bit0, desc.index)) {
        *head &= ~1ul;
    }
}

static void bit_from_head(struct chunk* chunk, struct entry_descriptor desc, uint8_t* head) {
    bool bit0 = (*head & 1) != 0;
    bitset_put(chunk->bit0, desc.index, bit0);
    *head |= 1;
}

// == Read lock implementation ==

static void cache_drop(struct lazyfree_cache* cache, struct entry_descriptor desc) {
    if (cache->verbose) {
        printf("DEBUG: Dropping chunk %d, index %d\n", desc.chunk, desc.index);
    }
    
    struct chunk* chunk = &cache->chunks[desc.chunk];
    lazyfree_key_t key = chunk->keys[desc.index];
    chunk->free_pages[chunk->free_pages_count++] = desc.index;
    if (chunk->free_pages_count > chunk->len) {

        print_stats(cache);
        printf("DEBUG: Free pages count %u is greater than chunk len %u\n", chunk->free_pages_count, chunk->len);
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

bool lazyfree_read_try_lock(lazyfree_cache_t cache, 
                               lazyfree_key_t key,
                               uint8_t* head,
                               uint8_t** tail) {
    assert(cache->locked_entry == NULL);
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

    bit_to_head(chunk, desc, head);

    *tail = entry->tail;
    cache->locked_entry = entry;
    cache->locked_desc = desc;
    cache->last_key = key;
    cache->locked_read = true;
    return true;
}

bool lazyfree_read_lock_check(struct lazyfree_cache* cache) {
    assert(cache->locked_entry != NULL);
    assert(cache->locked_desc.chunk != EMPTY_DESC.chunk);
    assert(cache->locked_read);
    if (cache->verbose && !cache->locked_entry->head) {
        printf("Key was evicted during read\n");
        return false;
    }
    return true;
}

static void read_cache_unlock(struct lazyfree_cache* cache, bool drop) {
    assert(cache->locked_entry != NULL);
    assert(cache->locked_desc.chunk != EMPTY_DESC.chunk);
    assert(cache->locked_read);
    if (cache->verbose) {
        printf("DEBUG: Unlocking key read %lu, drop %d\n", cache->last_key, drop);
    }
    if (drop) {
        cache_drop(cache, cache->locked_desc);
    }
    cache->locked_entry = NULL;
    cache->locked_desc = EMPTY_DESC;
    cache->locked_read = false;
}


// == Write lock implementation ==

static void drop_next_chunk(struct lazyfree_cache* cache) {
    // Next chunk:
    // cache->current_chunk_idx = (cache->current_chunk_idx + 1) % NUMBER_OF_CHUNKS;

    // Random chunk:
    cache->current_chunk_idx = random_next() % NUMBER_OF_CHUNKS;

    struct chunk* chunk = &cache->chunks[cache->current_chunk_idx];
        
    if (cache->verbose) {
        printf("DEBUG: Dropping chunk %zu\n", cache->current_chunk_idx);
        print_stats(cache);
    }

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
    if (cache->madv_impl != NULL) {
        cache->madv_impl(cache->chunks[cache->current_chunk_idx].entries, cache->chunk_size);
    }
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
    cache->locked_entry = entry;
    *value = (uint8_t*) entry;
}

bool lazyfree_write_lock(lazyfree_cache_t cache, 
                            lazyfree_key_t key,
                            uint8_t **value) {
    struct lazyfree_cache* lazyfree_cache = (struct lazyfree_cache*) cache;
    assert(cache->locked_entry == NULL);
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

        bit_to_head(chunk, desc, &cache->locked_entry->head);
        take_write_lock(cache, desc, value);
        return true;
    }

    // Looking for a free page

    if (cache->total_free_pages == 0) {
        if (cache->verbose || key == DEBUG_KEY) {
            printf("No free pages, freeing up next chunk\n");
        }
        drop_next_chunk(cache);
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
            print_stats(cache);
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
    assert(cache->locked_entry != NULL);
    assert(!cache->locked_read);

    if (cache->verbose || cache->last_key == DEBUG_KEY) {
        printf("DEBUG: Unlocking key write %lu, drop %d\n", cache->last_key, drop);
    }

    if (drop) {
        cache_drop(cache, cache->locked_desc);
    } else {
        // Move bit0 from head to bit0
        struct chunk* chunk = &cache->chunks[cache->locked_desc.chunk];      
        bit_from_head(chunk, cache->locked_desc, &cache->locked_entry->head);  

        if (cache->verbose || cache->last_key == DEBUG_KEY) {
            printf("DEBUG: Putting key %lu, chunk %d, index %d\n", cache->last_key, cache->locked_desc.chunk, cache->locked_desc.index);
        }

        chunk->keys[cache->locked_desc.index] = cache->last_key;
        hmap_put(cache, &chunk->keys[cache->locked_desc.index], cache->locked_desc);
    }

    cache->locked_desc = EMPTY_DESC;
    cache->locked_entry = NULL;
    cache->locked_read = false;
}

// == General lock functions ==

void lazyfree_unlock(lazyfree_cache_t cache, bool drop) {
    if (cache->verbose) {
        printf("DEBUG: Unlocking key %lu, drop %d, locked_read %d\n", 
            cache->last_key, drop, cache->locked_read);
    }
    if (cache->locked_read) {
        read_cache_unlock(cache, drop);
    } else {
        cache_write_unlock(cache, drop);
    }
}


bool lazyfree_upgrade_lock(lazyfree_cache_t cache) {
    assert(cache->locked_read);
    assert(cache->locked_desc.chunk != EMPTY_DESC.chunk);
    assert(cache->locked_entry != NULL);

    struct chunk* chunk = &cache->chunks[cache->locked_desc.chunk];


    cache->locked_read = false;

    // Use second byte to lock the page
    uint8_t byte1 = cache->locked_entry->tail[0];
    cache->locked_entry->tail[0] = 1;

    if (!lazyfree_read_lock_check(cache)) {
        cache->locked_entry->tail[0] = 0;
        return false;
    }

    cache->locked_entry->tail[0] = byte1;
    bit_from_head(chunk, cache->locked_desc, &cache->locked_entry->head);  
    return true;
}


// == Public functions ==

lazyfree_cache_t lazyfree_cache_new(size_t cache_capacity, lazyfree_mmap_impl_t mmap_impl, lazyfree_madv_impl_t madv_impl) {
    return cache_new(cache_capacity, mmap_impl, madv_impl);
}

struct lazyfree_stats lazyfree_fetch_stats(lazyfree_cache_t cache, bool verbose) {
    struct lazyfree_cache* lazyfree_cache = (struct lazyfree_cache*) cache;
    struct lazyfree_stats stats;
    stats.total_pages = lazyfree_cache->cache_capacity / PAGE_SIZE;
    stats.free_pages = lazyfree_cache->total_free_pages;
    print_stats(cache);
    return stats;
}


// == Memory functions  ==

void lazyfree_madv_free(void *memory, size_t size) {
    int ret;

    ret = madvise(memory, size, MADV_FREE);
    assert(ret == 0);
}

void lazyfree_madv_cold(void *memory, size_t size) {
    int ret = madvise(memory, size,  MADV_COLD);
    assert(ret == 0);
}

void *lazyfree_mmap_anon(size_t size) {
    void *addr = mmap(NULL, size, 
                PROT_READ | PROT_WRITE, 
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                -1, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    // int ret = madvise(addr, size, MADV_NOHUGEPAGE | MADV_WILLNEED);
    // assert(ret == 0);
    // printf("Anon mmap %zu Mb at %p\n", size / M, addr);
    return addr;
}

void *lazyfree_mmap_file(size_t size) {
    char filename[PATH_MAX];
    mkdir("./tmp", 0755);
    snprintf(filename, PATH_MAX, "./tmp/cache-%ld", random_next());


    int fd = open(filename, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        perror("open");
        exit(1);
    }

    if (ftruncate(fd, size) == -1) {
        perror("ftruncate");
        exit(1);
    }

    void *addr = mmap(NULL, size, 
        PROT_READ | PROT_WRITE, 
        MAP_SHARED | MAP_NORESERVE, 
        fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    close(fd);
    lazyfree_madv_cold(addr, size);
    return addr;
}
