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
float check_all(uint64_t key_seed, size_t cnt) {
    refill_ctx.count = 0;
    printf("Checking %zu pages\n", cnt);
    for (size_t i = 0; i < cnt; ++i) {
        uint64_t key = key_seed + i;
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
    printf("Dropping %zu pages\n", cnt);
    size_t hits = 0;
    for (size_t i = 0; i < cnt; ++i) {
        uint64_t key = key_seed + i;
        hits += fallthrough_cache_drop(cache, key);
    }
    return (float) hits / (float) cnt;
}

void check_twice_test(size_t size, float expected_hitrate) {
    size *= 0.9;

    printf("=== Check twice test, size %zu, expected hitrate %.2f%% ===\n", size, expected_hitrate * 100);
    uint64_t key_seed = random_next();
    uint64_t cnt = size/PAGE_SIZE;
    printf("1. Start first pass\n");
    float hitrate1 = check_all(key_seed, cnt);

    printf("Hitrate 1: %.2f%%\n", hitrate1 * 100);
    printf("2. Start second pass\n");
    
    // fallthrough_cache_debug(cache, true);
    float hitrate2 = check_all(key_seed, cnt);
    printf("Hitrate 2: %.2f%%\n", hitrate2 * 100);

    assert(hitrate2 >= hitrate1);
    assert(hitrate2 >= expected_hitrate);

    drop_all(cache, key_seed, cnt);
}

void mem_pressure_test(size_t size) {
    check_twice_test(size, 0.9);

    
}


void suite_lazyfree(bool full) {
    struct cache_impl impl = lazyfree_cache_impl;
    impl.mmap_impl = mmap_normal;
    impl.madv_impl = madv_lazyfree;
    
    cache = fallthrough_cache_new(impl, 
        cache_size, 
        sizeof(uint64_t), 
        1,
        refill_cb);

    run_smoke_test();
    if (!full) {
        return;
    }

    // fallthrough_cache_debug(cache, true);
    // Second hitrate is almost 100%
    check_twice_test(cache_size, 0.9);

    // Hitrate should be around 25%
    check_twice_test(2*cache_size, 0.2);
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
    check_twice_test(2*cache_size, 0.3);
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
