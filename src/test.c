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


#define SMOKE_TEST_CNT 10
void run_smoke_test(struct fallthrough_cache *cache) {
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
float check_all(struct fallthrough_cache *cache, uint64_t key_seed, size_t cnt) {
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

void run_check_twice_test(struct fallthrough_cache *cache, size_t size, float expected_hitrate) {
    uint64_t key_seed = random_next();
    uint64_t cnt = size/PAGE_SIZE;
    float hitrate1 = check_all(cache, key_seed, cnt);

    printf("Hitrate 1: %.2f%%\n", hitrate1 * 100);
    printf("\n== Start second pass ==\n");
    
    // fallthrough_cache_debug(cache, true);
    float hitrate2 = check_all(cache, key_seed, cnt);
    printf("Hitrate 2: %.2f%%\n", hitrate2 * 100);

    assert(hitrate2 > hitrate1);
    assert(hitrate2 > expected_hitrate);

    drop_all(cache, key_seed, cnt);
}


int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: %s <impl> <memory_size_gb> <suite>\n", argv[0]);
        printf("Impls: \n");
        printf("  lazyfree\n");
        printf("  random\n");
        printf("  disk\n");        
        return 1;
    }
    size_t memory_size = atoll(argv[2]);
    if (memory_size < 1) {
        printf("Memory size must be at least 1Gb\n");
        return 1;
    }

    struct cache_impl impl;
    if (strcmp(argv[1], "lazyfree") == 0) {
        impl = lazyfree_cache_impl;
    } else if (strcmp(argv[1], "random") == 0) {
        impl = random_cache_impl;
    } else {
        printf("Unknown impl: %s\n", argv[1]);
        return 1;
    }

    refill_ctx.seed = random_next();
    refill_ctx.count = 0;
    
    struct fallthrough_cache *cache = fallthrough_cache_new(impl, 
        memory_size * G, 
        sizeof(uint64_t), 
        1,
        refill_cb);

    // fallthrough_cache_debug(cache, true);

    if (strcmp(argv[3], "smoke") == 0) {
        run_smoke_test(cache);
    } else if (strcmp(argv[3], "check_twice") == 0) {
        run_check_twice_test(cache, memory_size * G, 0.9);
        fallthrough_cache_debug(cache, false);
        run_check_twice_test(cache, memory_size * G, 0.9);
    } else {
        printf("Unknown suite: %s\n", argv[3]);
        return 1;
    }
    fallthrough_cache_free(cache);


    return 0;


    // test_check_twice(cache, random_next(), 8 * M);
    // fallthrough_cache_free(cache);

    // run_put_get_tests(1, 100 * M);
    // run_put_get_tests(5, 4 * G);
    // run_ladder_test(true, false);
    // run_fix_test();
    // run_large_then_small_test();

    // run_put_get_tests(1, 10 * G);
    // run_put_get_tests(1, 100*K);
    // run_large_then_small_test();
    // run_ladder_test(true, true);

    // run_linear_subsets_test();
    // run_special_subsets_test();

    // run_put_get_tests(1, 100*1000);
    // run_put_get_tests(10, 1000*1000);
    // run_flush_then_small_test();
}
