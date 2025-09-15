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

#include "cache.h"
#include "hashmap.h"

#include "lazyfree_cache.h"

#include "bitset.h"
#include "random.h"


#define DEBUG_KEY -1ul

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
};
static_assert(sizeof(struct entry_descriptor) == 8, "entry_descriptor size is not 8 bytes");

static struct entry_descriptor EMPTY_DESC = { .chunk = -1 };
static lazyfree_rlock_t EMPTY_LOCK = { .tail = NULL, ._chunk = -1};

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
    struct entry_descriptor key0;

    size_t total_free_pages;

    // Write lock state
    uint32_t wlock_index;
    int8_t wlock_chunk;
    lazyfree_key_t wlock_key;

    bool verbose;
};

static int exact_key_comparer(const void *a, hashmap_uint32_t /*a_len*/,
                              const void *b, hashmap_uint32_t /*b_len*/) {
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
                                         hashmap_uint32_t /*key_len*/) {
    return seed + hash_u64_to_u32((uint64_t) key) + 1;
}

lazyfree_cache_t lazyfree_cache_new_ex(size_t cache_capacity, 
                                       lazyfree_mmap_impl_t mmap_impl, 
                                       lazyfree_madv_impl_t madv_impl) {
    struct lazyfree_cache* cache = malloc(sizeof(struct lazyfree_cache));
    memset(cache, 0, sizeof(struct lazyfree_cache));
    cache->madv_impl = madv_impl;
    cache->cache_capacity = cache_capacity;
    cache->chunk_size = cache_capacity / NUMBER_OF_CHUNKS;
    cache->pages_per_chunk = cache->chunk_size / PAGE_SIZE;
    

    // Allocate all chunks on start
    for (size_t i = 0; i < NUMBER_OF_CHUNKS; ++i) {
        void *entries = mmap_impl(cache->chunk_size);
        assert(entries != MAP_FAILED);

        cache->chunks[i].entries = entries;

        cache->chunks[i].bit0 = bitset_new(cache->pages_per_chunk);
        assert(cache->chunks[i].bit0 != NULL);

        cache->chunks[i].free_pages = malloc(cache->pages_per_chunk * sizeof(uint32_t));
        assert(cache->chunks[i].free_pages != NULL);

        cache->chunks[i].keys = malloc(cache->pages_per_chunk * sizeof(uint64_t));
        assert(cache->chunks[i].keys != NULL);
    }
    cache->total_free_pages = NUMBER_OF_CHUNKS * cache->pages_per_chunk;

    cache->wlock_chunk = EMPTY_DESC.chunk;
    
    hashmap_create_ex( (struct hashmap_create_options_s){
        .initial_capacity = NUMBER_OF_CHUNKS*cache->pages_per_chunk,
        .comparer = &exact_key_comparer,
        .hasher = &exact_key_hasher,
    }, &cache->map);
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

static void bit_to_head(struct chunk* chunk, uint32_t index, uint8_t* head) {
    if (!bitset_get(chunk->bit0, index)) {
        *head &= ~1ul;
    }
}

static void bit_from_head(struct chunk* chunk, uint32_t index, uint8_t* head) {
    bool bit0 = (*head & 1) != 0;
    bitset_put(chunk->bit0, index, bit0);
    *head |= 1;
}

// == rlock helpers ==

uint32_t rlock_to_index(struct chunk* chunk, lazyfree_rlock_t lock) {
    struct discardable_entry* entry = (struct discardable_entry*) (lock.tail - 1);
    return entry - chunk->entries;
}

bool rlock_check_head(struct lazyfree_cache* cache, lazyfree_rlock_t lock) {
    struct chunk* chunk = &cache->chunks[lock._chunk];
    uint32_t index = rlock_to_index(chunk, lock);
    if (!chunk->entries[index].head) {
        if (cache->verbose) {
            printf("Key was evicted during read\n");
        }
        return false;
    }
    return true;
}

bool rlock_check_key(struct lazyfree_cache* cache, lazyfree_rlock_t lock) {
    struct chunk* chunk = &cache->chunks[lock._chunk];
    uint32_t index = rlock_to_index(chunk, lock);
    if (chunk->keys[index] != lock._key) {
        if (cache->verbose) {
            printf("Key %lu was evicted by dropping the chunk\n", lock._key);
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

lazyfree_rlock_t lazyfree_read_lock(lazyfree_cache_t cache, 
                                    lazyfree_key_t key) {
    struct entry_descriptor desc = hmap_get(cache, key);

    assert(cache->wlock_chunk == EMPTY_DESC.chunk);
  
    lazyfree_rlock_t rlock = EMPTY_LOCK;
    rlock._key = key;
    if (desc.chunk == EMPTY_DESC.chunk) {
        if (cache->verbose) {
            printf("Key %lu not found\n", key);
        }
        return rlock;
    }
    struct chunk* chunk = &cache->chunks[desc.chunk];
    struct discardable_entry* entry = &chunk->entries[desc.index];

    if (cache->verbose || key == DEBUG_KEY) {
        printf("DEBUG: rlock key=%lu chunk=%d index=%d\n", key, desc.chunk, desc.index);
    }

    if (chunk->keys[desc.index] != key) {
        if (cache->verbose || key == DEBUG_KEY) {
            printf("Key %lu was evicted by dropping the chunk\n", key);
        }
        return rlock;
    }   

    rlock._index = desc.index;
    rlock._chunk = desc.chunk;
    rlock.head = entry->head;
    
    if (entry->head == 0) {
        if (cache->verbose || key == DEBUG_KEY) {
            printf("Key %lu was evicted by kernel\n", key);
        }

        rlock.tail = NULL;
        return rlock;
    }

    rlock.tail = entry->tail;
    bit_to_head(chunk, desc.index, &rlock.head);
    return rlock;
}

bool lazyfree_read_lock_check(struct lazyfree_cache* cache, lazyfree_rlock_t lock) {
    assert(cache->wlock_chunk == EMPTY_DESC.chunk);
    if (lock._chunk == EMPTY_DESC.chunk) {
        return false;
    }
    return rlock_check_head(cache, lock) && rlock_check_key(cache, lock);
}

void lazyfree_read_unlock(struct lazyfree_cache* cache, lazyfree_rlock_t lock, bool drop) {
    assert(cache->wlock_chunk == EMPTY_DESC.chunk);

    // Do we need to drop?
    if (!drop) {
        return;
    }
    if (lock._chunk == EMPTY_DESC.chunk) {
        return;
    }
    if (!rlock_check_key(cache, lock)) {
        return;
    }
    
    struct chunk* chunk = &cache->chunks[lock._chunk];
    struct entry_descriptor desc = {
        .chunk = lock._chunk,
        .index = rlock_to_index(chunk, lock),
    };
    cache_drop(cache, desc);
}


// == Write Lock Implementation ==

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
    if (cache->verbose) {
        printf("DEBUG: Switched to chunk %zu\n", cache->current_chunk_idx);
    }
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

static struct entry_descriptor alloc_new_page(struct lazyfree_cache* cache, uint64_t key) {
    struct entry_descriptor desc = EMPTY_DESC;
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

    return desc;
}

void* lazyfree_write_alloc(lazyfree_cache_t cache, lazyfree_key_t key) {
    assert(cache->wlock_chunk == EMPTY_DESC.chunk);

    // Looking for a free page
    struct entry_descriptor desc = alloc_new_page(cache, key);
    struct chunk* chunk = &cache->chunks[desc.chunk];
    struct discardable_entry* entry = &chunk->entries[desc.index];

    cache->wlock_chunk = desc.chunk;
    cache->wlock_index = desc.index;
    cache->wlock_key = key;
    hmap_put(cache, key, desc);
    chunk->keys[desc.index] = cache->wlock_key;

    if (cache->verbose || key == DEBUG_KEY) {
        printf("Allocated key=%lu chunk %d, index %d\n", key, desc.chunk, desc.index);
    }
    
    return entry;
}


void* lazyfree_write_upgrade(lazyfree_cache_t cache, lazyfree_rlock_t* lock) {
    if (cache->verbose || lock->_key == DEBUG_KEY) {
        printf("DEBUG: Upgrading lock for key %lu\n", lock->_key);
    }
    assert(cache->wlock_chunk == EMPTY_DESC.chunk);

    if (lock->tail == NULL) {
        // This is an empty entry
        return lazyfree_write_alloc(cache, lock->_key);
    }

    if (!rlock_check_key(cache, *lock)) {
        // This is now some other key
        return lazyfree_write_alloc(cache, lock->_key);
    }

    // Use second byte to lock the page
    uint8_t byte1 = lock->tail[0];
    lock->tail[0] = 1;
    if (!rlock_check_head(cache, *lock)) {
        lock->tail[0] = 0;
        printf("DEBUG: Page updated during upgrade\n");
        return lazyfree_write_alloc(cache, lock->_key);
    }
    lock->tail[0] = byte1;
    
    // Set wlock
    cache->wlock_chunk = lock->_chunk;
    cache->wlock_index = lock->_index;
    cache->wlock_key = lock->_key;
    // Already in hashmap and keys
    
    struct chunk* chunk = &cache->chunks[lock->_chunk];
    struct discardable_entry* entry = &chunk->entries[lock->_index];

    bit_to_head(chunk, lock->_index, &entry->head);
    return (uint8_t*) entry;
}

void lazyfree_write_unlock(lazyfree_cache_t cache, bool drop) {
    assert(cache->wlock_chunk != EMPTY_DESC.chunk);
    
    if (cache->verbose || cache->wlock_key == DEBUG_KEY) {
        printf("DEBUG: Unlocking key write %lu, drop %d\n", cache->wlock_key, drop);
    }

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
    bit_from_head(chunk, desc.index, &entry->head);  

    // if (cache->verbose || cache->wlock_key == DEBUG_KEY) {
    //     printf("DEBUG: Write unlock key %lu, chunk %d, index %d\n", cache->wlock_key, desc.chunk, desc.index);
    // }
}


// == Public functions ==

lazyfree_cache_t lazyfree_cache_new(size_t cache_capacity) {
    return lazyfree_cache_new_ex(cache_capacity, lazyfree_mmap_anon, lazyfree_madv_free);
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


// == Tests

void lazyfree_cache_tests() {
    uint64_t value = random_next();
    
    lazyfree_cache_t cache = lazyfree_cache_new(32*NUMBER_OF_CHUNKS*PAGE_SIZE);
    
    
    uint64_t* ptr = lazyfree_write_alloc(cache, 1);
    *ptr = value;
    
    lazyfree_write_unlock(cache, false);

    lazyfree_rlock_t lock = lazyfree_read_lock(cache, 1);
    if (!lazyfree_read_lock_check(cache, lock)) {
        printf("Lock is not valid\n");
        exit(1);
    }
    uint64_t* ptr2 = lazyfree_write_upgrade(cache, &lock);
    if (*ptr2 != value) {
        printf("Value %lu != expected %lu\n", *ptr2, value);
        exit(1);
    }
    lazyfree_write_unlock(cache, false);


    lazyfree_cache_free(cache);
}
