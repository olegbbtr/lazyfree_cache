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
#include "testlib.h"


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


size_t get_cnt(size_t size) {
    return size/PAGE_SIZE - 1024; // Don't want to overflow
}

float check_twice(size_t size, float expected_hitrate) {
    printf("=== Check twice test, size %zu, expected hitrate %.2f%% ===\n", size, expected_hitrate * 100);
    struct testlib_keyset keyset = testlib_init(get_cnt(size));
    
    
    printf("1. First pass");
    float hitrate1 = testlib_check_all(cache, keyset, testlib_order_affine);
    if (hitrate1 > 0) {
        printf("Hitrate1 must be 0\n");
        exit(1);
    }
    
    printf("2. Second pass");
    float hitrate2 = testlib_check_all(cache, keyset, testlib_order_affine);

    if (hitrate2 < hitrate1) {
        printf("Hitrate2 must be greater than hitrate1\n");
        exit(1);
    }
    if (hitrate2 < expected_hitrate) {
        printf("Hitrate2 must be greater than expected hitrate\n");
        exit(1);
    }
    
    testlib_drop_all(cache, keyset);
    return hitrate2;
}


void mem_pressure_test(size_t size) {
    struct testlib_keyset keyset = testlib_init(get_cnt(size));
    
    uint64_t attempts = 5;
    for (uint64_t i = 0; i < attempts; ++i) {
        float hitrate = testlib_check_all(cache, keyset, testlib_order_affine_chunk);
        printf("Hitrate before reclaim: %.2f%%\n", hitrate * 100);
        sleep(1);
        // fallthrough_cache_debug(cache, false);
    }
    // fallthrough_cache_debug(cache, false);
    
    
    testlib_reclaim_many(3, size/4);
    sleep(1);
    
    for (uint64_t i = 0; i < attempts; ++i) {
        float hitrate = testlib_check_all(cache, keyset, testlib_order_affine_chunk);
        printf("Hitrate after reclaim: %.2f%%\n", hitrate * 100);
        // fallthrough_cache_debug(cache, false);
    }

    testlib_drop_all(cache, keyset);
    // fallthrough_cache_debug(cache, false);
    sleep(1);

    for (uint64_t i = 0; i < attempts; ++i) {
        float hitrate = testlib_check_all(cache, keyset, testlib_order_affine_chunk);
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


    run_smoke_test();
    // fallthrough_cache_debug(cache, false);

    // sleep(100);

    // mem_pressure_test(cache_size, true);
    if (!full) {
        return;
    }
    
    // fallthrough_cache_debug(cache, true);
    // Second hitrate is almost 100%
    check_twice(cache_size, 0.9);

    // Hitrate should be >15%
    check_twice(2*cache_size, 0.15);
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

    check_twice(cache_size, 1);
    // check_twice(2*cache_size, 0.25);
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

    check_twice(cache_size, 1);
    // check_twice(2*cache_size, 0.25);
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
