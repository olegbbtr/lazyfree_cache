#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>



#include "fallthrough_cache.h"
#include "lazyfree_cache.h"
#include "random_cache.h"
// #include "disk_cache.h"


#include "random.h"
#include "refill.h"


struct fallthrough_cache *cache;
static size_t cache_size;

#define SMOKE_TEST_CNT 10
void run_smoke_test() {
    refill_ctx.count = 0;


    uint64_t keys[SMOKE_TEST_CNT];
    for (size_t i = 0; i < SMOKE_TEST_CNT; ++i) {
        keys[i] = random_next() + i;
        uint64_t value;
        fallthrough_cache_get(cache, keys[i], (uint8_t*) &value);

        // Refills one more time
        assert(refill_ctx.count == i + 1);
    }

    uint64_t values[SMOKE_TEST_CNT];
    for (size_t i = 0; i < SMOKE_TEST_CNT; ++i) {
        fallthrough_cache_get(cache, keys[i], (uint8_t*) &values[i]);
        if (values[i] != refill_expected(keys[i])) {
            printf("Value %zu: %lu != expected %lu\n", i, values[i], refill_expected(keys[i]));
            exit(1);
        }
        // Refill should not be called
        assert(refill_ctx.count == 10);
    }

    for (size_t i = 0; i < SMOKE_TEST_CNT; ++i) {
        bool ok = fallthrough_cache_drop(cache, keys[i]);
        assert(ok);
    }

    assert(refill_ctx.count == 10);
}

// Returns hitrate
float check_all(uint64_t key_seed, size_t cnt, bool randomize) {
    refill_ctx.count = 0;
    printf("Checking %zu pages\n", cnt);
    for (size_t i = 0; i < cnt; ++i) {
        uint64_t key = key_seed;
        if (randomize) {
            key += random_next() % cnt;
        } else {
            key += i;
        }
        uint64_t value;
        fallthrough_cache_get(cache, key, (uint8_t*) &value);

        if (value != refill_expected(key)) {
            printf("Key %lu: Value %lu != expected %lu\n", key, value, refill_expected(key));
            exit(1);
        }
    }
    return ((float) cnt - (float) refill_ctx.count) / (float) cnt;
}

// Returns hitrate
float drop_all(struct fallthrough_cache *cache, uint64_t key_seed, size_t cnt) {
    
    fallthrough_cache_debug(cache, false);

    printf("\nDropping %zu pages\n", cnt);
    size_t hits = 0;
    for (size_t i = 0; i < cnt; ++i) {
        uint64_t key = key_seed + i;
        hits += fallthrough_cache_drop(cache, key);
    }

    fallthrough_cache_debug(cache, false);
    return (float) hits / (float) cnt;
}

size_t get_cnt(size_t size) {
    return size/PAGE_SIZE - 1024; // Don't want to overflow
}

void check_twice_test(size_t size, float expected_hitrate) {
    printf("=== Check twice test, size %zu, expected hitrate %.2f%% ===\n", size, expected_hitrate * 100);
    uint64_t key_seed = random_next();
    uint64_t cnt = get_cnt(size);
    printf("1. Start first pass\n");
    float hitrate1 = check_all(key_seed, cnt, false);

    printf("Hitrate 1: %.2f%%\n", hitrate1 * 100);
    printf("2. Start second pass\n");
    
    // fallthrough_cache_debug(cache, true);
    float hitrate2 = check_all(key_seed, cnt, false);
    printf("Hitrate 2: %.2f%%\n", hitrate2 * 100);

    assert(hitrate2 >= hitrate1);
    assert(hitrate2 >= expected_hitrate);

    drop_all(cache, key_seed, cnt);
}

void reclaim_memory(size_t size) {
    printf("Reclaiming %zu Mb\n", size/M);
    uint8_t *mem = mmap_normal(size);
    for (size_t i = 0; i < size/PAGE_SIZE; ++i) {
        mem[i*PAGE_SIZE] = random_next();
    }
    munmap(mem, size);
}

void reclaim_many(size_t chunks, size_t chunk_size) {
    printf("Reclaiming %zu chunks of %zu Mb\n", chunks, chunk_size/M);
    uint8_t **mem = malloc(chunks * sizeof(uint8_t*));
    for (size_t i = 0; i < chunks; ++i) {
        mem[i] = mmap_normal(chunk_size);
        for (size_t j = 0; j < chunk_size/PAGE_SIZE; ++j) {
            mem[i][j*PAGE_SIZE] = random_next();
        }
    }
    for (size_t i = 0; i < chunks; ++i) {
        munmap(mem[i], chunk_size);
    }
    free(mem);
}

void mem_pressure_test(size_t size, bool randomize) {
    uint64_t key_seed = random_next();
    uint64_t cnt = get_cnt(size);
    uint64_t attempts = 5;
    for (uint64_t i = 0; i < attempts; ++i) {
        float hitrate = check_all(key_seed, cnt, randomize);
        printf("Hitrate before reclaim: %.2f%%\n", hitrate * 100);
        sleep(1);
    // fallthrough_cache_debug(cache, false);
    }

    fallthrough_cache_debug(cache, false);
    
    // drop_all(cache, key_seed, cnt);
    
    reclaim_many(7, size/8);
    
    sleep(10);

    
    for (uint64_t i = 0; i < attempts; ++i) {
        float hitrate = check_all(key_seed, cnt, randomize);
        printf("Hitrate after reclaim: %.2f%%\n", hitrate * 100);
        // fallthrough_cache_debug(cache, false);
    }

    drop_all(cache, key_seed, cnt);
    fallthrough_cache_debug(cache, false);



    sleep(10);

    for (uint64_t i = 0; i < attempts; ++i) {
        float hitrate = check_all(key_seed, cnt, randomize);
        printf("Hitrate after reclaim and drop: %.2f%%\n", hitrate * 100);
        // fallthrough_cache_debug(cache, false);
    }
}


void suite_lazyfree(bool full) {
    struct cache_impl impl = lazyfree_cache_impl;
    impl.mmap_impl = mmap_normal;
    impl.madv_impl = madv_lazyfree;

    size_t overcommited_size = cache_size;
    cache = fallthrough_cache_new(impl, 
        overcommited_size, 
        sizeof(uint64_t), 
        1,
        refill_cb);


    // run_smoke_test();

    check_twice_test(cache_size, 0.8);
    fallthrough_cache_debug(cache, false);

    // sleep(100);

    // mem_pressure_test(cache_size, true);
    if (!full) {
        return;
    }

    // fallthrough_cache_debug(cache, true);
    // Second hitrate is almost 100%
    check_twice_test(cache_size, 0.9);

    // Hitrate should be around 25%
    check_twice_test(2*cache_size, 0.15);
}

void suite_normal() {
    struct cache_impl impl = lazyfree_cache_impl;
    impl.mmap_impl = mmap_normal;
    impl.madv_impl = madv_noop;
    
    cache = fallthrough_cache_new(impl, 
        cache_size, 
        sizeof(uint64_t), 
        1,
        refill_cb);

    run_smoke_test();    

    check_twice_test(cache_size, 1);
    check_twice_test(2*cache_size, 0.15);
}

void suite_disk() {
    struct cache_impl impl = lazyfree_cache_impl;
    impl.mmap_impl = mmap_file;
    impl.madv_impl = madv_noop;

    cache = fallthrough_cache_new(impl, 
        cache_size, 
        sizeof(uint64_t), 
        1,
        refill_cb);

    run_smoke_test();

    check_twice_test(cache_size, 1);
    check_twice_test(2*cache_size, 0.3);
}


int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s <suite> <memory_size_gb>\n", argv[0]);       
        printf("Suites: lazyfree, lazyfree_full, normal, disk\n");
        return 1;
    }
    size_t memory_size = atoll(argv[2]);
    if (memory_size < 1) {
        printf("Memory size must be at least 1Gb\n");
        return 1;
    }
    cache_size = memory_size * G;


    random_rotate();

    refill_ctx.seed = random_next();
    refill_ctx.count = 0;



    printf("== Starting suite %s ==\n", argv[1]);
    if (strcmp(argv[1], "lazyfree") == 0) {
        suite_lazyfree(false);
    } else if (strcmp(argv[1], "lazyfree_full") == 0) {
        suite_lazyfree(true);
    } else if (strcmp(argv[1], "normal") == 0) {
        suite_normal();
    } else if (strcmp(argv[1], "disk") == 0) {
        suite_disk();
    } else {
        printf("Unknown suite: %s\n", argv[1]);
        return 1;
    }

    fallthrough_cache_free(cache);


    return 0;
}
