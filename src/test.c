#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"


#include "fallthrough_cache.h"
#include "lazyfree_cache.h"
#include "random_cache.h"
// #include "disk_cache.h"


#include "random.h"
#include "repopulate.h"
#include "test.h"

void test_smoke(struct fallthrough_cache *cache) {
    struct repopulate_context ctx;
    repopulate_init(&ctx, random_next());
    fallthrough_cache_set_opaque(cache, &ctx);
    

    uint64_t seed_key = random_next();

    #define SMOKE_TEST_CNT 10
    uint64_t canonical_values[SMOKE_TEST_CNT];
    for (size_t i = 0; i < SMOKE_TEST_CNT; ++i) {
        uint64_t key = seed_key + i;

        printf("Getting key %lu\n", seed_key + i);
        fallthrough_cache_get(cache, key, (uint8_t*) &canonical_values[i]);
        assert(ctx.hits == i + 1);
    }

    printf("\n finished pass 1\n");

    uint64_t values[SMOKE_TEST_CNT];
    for (size_t i = 0; i < SMOKE_TEST_CNT; ++i) {
        printf("Getting key %lu\n", seed_key + i);
        uint64_t key = seed_key + i;
        fallthrough_cache_get(cache, key, (uint8_t*) &values[i]);
        assert(values[i] == canonical_values[i]);
        printf("Hits: %lu\n", ctx.hits);
        assert(ctx.hits == 10);
    }

    for (size_t i = 0; i < SMOKE_TEST_CNT; ++i) {
        uint64_t key = seed_key + i;
        bool ok = fallthrough_cache_drop(cache, key);
        assert(ok);
    }
}

// Returns hitrate
float check_all(struct fallthrough_cache *cache, uint64_t key_seed, size_t size) {
    struct repopulate_context ctx;
    repopulate_init(&ctx, key_seed);
    fallthrough_cache_set_opaque(cache, &ctx);

    struct repopulate_context ctx_expected;
    repopulate_init(&ctx_expected, key_seed);
    

    size_t cnt = size/ sizeof(uint64_t);
    for (size_t i = 0; i < cnt; ++i) {
        uint64_t key = key_seed + i;
        uint64_t value;
        fallthrough_cache_get(cache, key, (uint8_t*) &value);

        uint64_t expected;
        repopulate_job(&ctx_expected, key, (uint8_t*) &expected);
        if (value != expected) {
            printf("Value %lu != expected %lu\n", value, expected);
            exit(1);
        }
        if (i % 100000 == 0) {
            // printf("Checked %zu/%zu keys\n", i, cnt);
        }
    }
    return ((float) cnt - (float) ctx.hits) / (float) cnt;
}

// Returns hitrate
float drop_all(struct fallthrough_cache *cache, uint64_t key_seed, size_t size) {
    size_t cnt = size/ sizeof(uint64_t);
    size_t hits = 0;
    for (size_t i = 0; i < cnt; ++i) {
        uint64_t key = key_seed + i;
        hits += fallthrough_cache_drop(cache, key);
    }
    return (float) hits / (float) cnt;
}

void test_check_twice(struct fallthrough_cache *cache, uint64_t key_seed, size_t size) {
    struct repopulate_context ctx;
    repopulate_init(&ctx, key_seed);
    fallthrough_cache_set_opaque(cache, &ctx);

    float hitrate1 = check_all(cache, key_seed, size);

    printf("Hitrate 1: %.2f%%\n", hitrate1 * 100);
    printf("\n== Start second pass ==\n");
    fallthrough_cache_debug(cache, true);
    repopulate_init(&ctx, key_seed);
    float hitrate2 = check_all(cache, key_seed, size);
    printf("Hitrate 2: %.2f%%\n", hitrate2 * 100);

    assert(hitrate2 > hitrate1);
}


int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s <impl> <memory_size_gb>\n", argv[0]);
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

    struct fallthrough_cache *cache = fallthrough_cache_new(impl, 
        memory_size * G, 
        sizeof(uint64_t), 
        repopulate_job);
    // fallthrough_cache_debug(cache, true);
    // test_smoke(cache);
    // test_check_twice(cache, random_next(), 512 * K);

    test_check_twice(cache, random_next(), 8 * M);
    fallthrough_cache_free(cache);
    // run_smoke_test();
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
    return 0;
}
