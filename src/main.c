#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <math.h>

#include "madv_cache.h"
#include "random.h"





void put_str(struct madv_cache *cache, uint64_t key, const char* value) {
    char buf[PAGE_SIZE];
    memset(buf, 0, PAGE_SIZE);
    strcpy(buf, value);
    madv_cache_put(cache, key, (uint8_t*) buf);
    printf("Put %s\n", value);
}

void check_str(struct madv_cache *cache, uint64_t key, const char* value) {
    char buf[PAGE_SIZE];
    bool ok = madv_cache_get(cache, key, (uint8_t*) buf);
    printf("Got %s\n", buf);
    assert(ok);
    assert(strcmp(buf, value) == 0);
}

void run_smoke_test() {
    struct madv_cache cache;
    madv_cache_init(&cache);
    put_str(&cache, 0, "Hello, World!");
    put_str(&cache, 1, "More text");
    put_str(&cache, 2, "Even more text");

    check_str(&cache, 0, "Hello, World!");
    check_str(&cache, 1, "More text");
    check_str(&cache, 2, "Even more text");
    
    madv_cache_free(&cache);
}


void next_value(char value[PAGE_SIZE], uint64_t* rng_key, uint64_t* rng_value) {
    *rng_key = random_mix(rng_key);
    *rng_value = random_mix(rng_value);
    sprintf(value, "Value %lu:%lu", *rng_key, *rng_value);
}

void test_put(struct madv_cache* cache, uint64_t rng_key, uint64_t rng_value, size_t size) {
    size_t num = size / PAGE_SIZE;
    // printf("Starting put %.2fGb, %zu K entries\n", (float) size / G, num/K);
    for (size_t i = 0; i < num; ++i) {
        char value[PAGE_SIZE];
        next_value(value, &rng_key, &rng_value);

        madv_cache_put(cache, rng_key, (uint8_t*) value);
    }
    printf("Finished put %.2fGb, %zu K entries\n", (float) size / G, num/K);
}

float test_get(struct madv_cache* cache, uint64_t rng_key, uint64_t rng_value, size_t size, bool fix) {
    size_t num = size / (size_t) PAGE_SIZE;
    // printf("Starting get %.2fGb, %zu K entries\n", (float) size / G, num/K);
    size_t misses = 0;
    for (size_t i = 0; i < num; ++i) {
        char expected[PAGE_SIZE];
        next_value(expected, &rng_key, &rng_value);

        char value[PAGE_SIZE];
        bool ok = madv_cache_get(cache, rng_key, (uint8_t*) value);
        if (!ok) {
            misses++;
            if (fix) {
                madv_cache_put(cache, rng_key, (uint8_t*) expected);
            }
            continue;
        }
        // printf("value: %s\n", value);
        if(strcmp(value, expected) != 0) {
            printf("Expected %s, got %s\n", expected, value);
            exit(1);
        }
    }
    float hitrate = (float) (num - misses) / (float) num;   
    printf("Finished get %fGb: hit %zuK of %zuK (%.2f%%)\n", (float) size / G, (num - misses)/K, num/K, hitrate * 100);
    return hitrate;
}

void put_get_test(struct madv_cache *cache, size_t size) {
    uint64_t rng_key = random_next();
    uint64_t rng_value = random_next();
    
    test_put(cache, rng_key, rng_value, size);
    float hitrate = test_get(cache, rng_key, rng_value, size, false);
    assert(hitrate > 0.001);
}

void run_put_get_tests(size_t iterations, size_t size){
    printf("\n==Put-get test: %zu iterations, %.2fGb per iteration\n", iterations, (float) size / (float) G);
    struct madv_cache cache;
    madv_cache_init(&cache);
    

    for (size_t i = 0; i < iterations; ++i) {
        put_get_test(&cache, size);
        void *data = malloc(1*G);
        assert(data);
        free(data);
        printf("Iteration %zu done\n", i + 1);
        sleep(1);
    }
    madv_cache_free(&cache);
}

#define SUBSET_ITERATIONS 10
#define SUBSET_CNT 16
#define TOTAL_SIZE (20 * G)
#define ENTRIES_PER_SUBSET (TOTAL_SIZE / SUBSET_CNT / PAGE_SIZE)

#define SUBSET_SPECIAL_CNT 5

void run_special_subsets_test() {
    printf("\n==Starting special subsets test\n");
    float special_ratio = (float) SUBSET_SPECIAL_CNT / (float) SUBSET_CNT;
    float special_size = special_ratio * TOTAL_SIZE;
    printf("Total size: %zuGb. Special size: %.2fGb\n", TOTAL_SIZE/G, special_size/G);
    system("free -h");
    uint64_t rng_keys[SUBSET_CNT];
    uint64_t rng_values[SUBSET_CNT];
    size_t special_idxs[SUBSET_SPECIAL_CNT];

    for (size_t i = 0; i < SUBSET_SPECIAL_CNT; ++i) {
        special_idxs[i] = random_next() % SUBSET_CNT;
    }
    
    for (size_t i = 0; i < SUBSET_CNT; ++i) {
        rng_keys[i] = random_next();
        rng_values[i] = random_next();
    }

    struct madv_cache cache;
    madv_cache_init(&cache);
    
    // Put everything
    for (int j = 0; j < SUBSET_CNT; ++j) {
        printf("Initializing subset %d/%d: ", j, SUBSET_CNT);
        test_put(&cache, rng_keys[j], rng_values[j], ENTRIES_PER_SUBSET);
    }
    for (int i = 0; i < SUBSET_ITERATIONS; ++i) {
        // Special
        float special_hitrate_sum = 0;
        for (size_t j = 0; j < SUBSET_CNT; ++j) {
            size_t special_idx = special_idxs[j % SUBSET_SPECIAL_CNT];

            printf("Trying special %zu/%d: ", special_idx, SUBSET_CNT);
            special_hitrate_sum += test_get(
                &cache, 
                rng_keys[special_idx],
                rng_values[special_idx],
                ENTRIES_PER_SUBSET,
                true);
        }
        // Everything
        float normal_hitrate_sum = 0;
        for (size_t j = 0; j < SUBSET_CNT; ++j) {
            printf("Trying normal %zu/%d: ", j, SUBSET_CNT);
            normal_hitrate_sum += test_get(
                &cache, 
                rng_keys[j],
                rng_values[j],
                ENTRIES_PER_SUBSET,
                true);
        }
        float special_hitrate = special_hitrate_sum / SUBSET_CNT;
        float normal_hitrate = normal_hitrate_sum / SUBSET_CNT;
        printf("Special hitrate: %.2f%%, normal hitrate: %.2f%%\n", special_hitrate * 100, normal_hitrate * 100);
        sleep(1);
    }
    madv_cache_free(&cache);
}

struct distribution {
    uint64_t prob[SUBSET_CNT];
    uint64_t sum;
};

int distribution_next(struct distribution* dist) {
    if (dist->sum == 0) {
        for (int i = 0; i < SUBSET_CNT; ++i) {
            dist->sum += dist->prob[i];
        }
    }
    uint64_t rng = random_next() % dist->sum;
    // printf("rng: %zu, sum: %zu\n", rng, dist->sum);
    uint64_t acc = 0;
    for (int i = 0; i < SUBSET_CNT; ++i) {
        acc += dist->prob[i];
        if (rng < acc) {
            // printf("Chose %d, acc: %lu \n", i + 1, acc);
            return i;
        }
    }
    return SUBSET_CNT - 1;
}

#define DECAY_FACTOR 0.9

void run_linear_subsets_test() {
    printf("\n==Starting linear subsets test\n");
    printf("Total size: %zuGb. Per set size: %.2fGb\n", TOTAL_SIZE/G, TOTAL_SIZE/(float)SUBSET_CNT/G);
    system("free -h");
    uint64_t rng_keys[SUBSET_CNT];
    uint64_t rng_values[SUBSET_CNT];
    struct distribution dist;
    memset(&dist, 0, sizeof(dist));


    float rolling_hitrate[SUBSET_CNT];
    memset(rolling_hitrate, 0, sizeof(rolling_hitrate));
    for (int i = 0; i < SUBSET_CNT; ++i) {
        rng_keys[i] = random_next();
        rng_values[i] = random_next();
        dist.prob[i] = i;
        rolling_hitrate[i] = 0.5;
    }

    struct madv_cache cache;
    madv_cache_init(&cache);
    
    // Put everything
    for (int j = 0; j < SUBSET_CNT; ++j) {
        printf("Initializing subset %d/%d: ", j+1, SUBSET_CNT);
        test_put(&cache, rng_keys[j], rng_values[j], ENTRIES_PER_SUBSET);
    }

    for (int i = 0; i < SUBSET_ITERATIONS; ++i) {    
        // Do SUBSET_CNT number of random set reads
        for (int j = 0; j < SUBSET_CNT; ++j) {
            int set_id = distribution_next(&dist);
            printf("Trying set %d/%d: ", set_id+1, SUBSET_CNT);
            float hitrate = test_get(
                &cache, 
                rng_keys[set_id], 
                rng_values[set_id], 
                ENTRIES_PER_SUBSET, 
                true);
            rolling_hitrate[set_id] *= DECAY_FACTOR;
            rolling_hitrate[set_id] += (1 - DECAY_FACTOR) * hitrate;
        }

        float diff_to_ideal = 0;
        printf("Hitrate: ");
        for (int j = 0; j < SUBSET_CNT; ++j) {
            float hitrate = rolling_hitrate[j];
            float ideal_hitrate = (float) j / (float) SUBSET_CNT;
            printf("%d: %.2f%% (ideal: %.2f%%) ", j+1, hitrate * 100, ideal_hitrate * 100);
            diff_to_ideal += fabs(hitrate - ideal_hitrate);
        }
        printf("\n");
        printf("Diff to ideal: %.2f%%\n", diff_to_ideal * 100);
        sleep(1);
    }
    madv_cache_free(&cache);
}

#define SUBSET_SIZE (1 * G)

void put_large(struct madv_cache* cache, uint64_t rng_key, uint64_t rng_value, size_t size) {
    assert(size % SUBSET_SIZE == 0);
    
    size_t subsets = size / SUBSET_SIZE;
    printf("Putting %zuGb in %zu subsets\n", size / G, subsets);
    for (size_t i = 0; i < subsets; ++i) {
        printf("Put subset %zu/%zu: ", i, subsets);
        test_put(cache, rng_key + i, rng_value + i, SUBSET_SIZE);
    }
}

float get_large(struct madv_cache* cache, uint64_t rng_key, uint64_t rng_value, size_t size, bool fix) {
    assert(size % SUBSET_SIZE == 0);
    
    size_t subsets = size / SUBSET_SIZE;
    printf("Getting %zuGb in %zu subsets\n", size / G, subsets);
    float hitrate_sum = 0;
    for (size_t i = 0; i < subsets; ++i) {
        printf("Get subset %zu/%zu: ", i, subsets);
        hitrate_sum += test_get(cache, rng_key + i, rng_value + i, SUBSET_SIZE, fix);
    }
    return hitrate_sum / subsets;
}

#define LARGE_SIZE (20 * G)
#define SMALL_SIZE (10 * M)

void run_large_then_small_test() {
    printf("\n==Starting large then small test\n");
    system("free -h");
    printf("Large size: %zuGb, small size: %zuMb\n", LARGE_SIZE/G, SMALL_SIZE/M);
    struct madv_cache cache;
    madv_cache_init(&cache);
    
    uint64_t rng_key = random_next();
    uint64_t rng_value = random_next();
    
    put_large(&cache, rng_key, rng_value, LARGE_SIZE);


    get_large(&cache, rng_key, rng_value, 1 * G, true);
    get_large(&cache, rng_key, rng_value, 1 * G, true);
    
    sleep(1);
    
    test_put(&cache, rng_key, rng_value, SMALL_SIZE / PAGE_SIZE);

    test_get(&cache, rng_key, rng_value, SMALL_SIZE / PAGE_SIZE, true);
    test_get(&cache, rng_key, rng_value, SMALL_SIZE / PAGE_SIZE, true);

    madv_cache_free(&cache);
}


void run_fix_test() {
    printf("\n==Starting fix test\n");
    system("free -h");
    printf("Large size: %zuGb, small size: %zuMb\n", LARGE_SIZE/G, SMALL_SIZE/M);
    struct madv_cache cache;
    madv_cache_init(&cache);
    
    uint64_t rng_key = random_next();
    uint64_t rng_value = random_next();
    
    put_large(&cache, rng_key, rng_value, LARGE_SIZE);


    float hitrate = test_get(&cache, rng_key, rng_value, 10 * PAGE_SIZE, true);
    assert(hitrate < 0.99);

    cache.verbose = true;
    hitrate = test_get(&cache, rng_key, rng_value, 10 * PAGE_SIZE, true);
    assert(hitrate > 0.99);

    madv_cache_free(&cache);
}


#define LADDER_ITERATIONS 20
#define LADDER_SUBSET (2 * G)
void run_ladder_test(bool fix, bool twice) {
    printf("\n==Starting ladder test, fix: %d\n", fix);
    system("free -h");


    struct madv_cache cache;
    madv_cache_init(&cache);
    
    uint64_t rng_keys[LADDER_ITERATIONS];
    uint64_t rng_values[LADDER_ITERATIONS];
    for (size_t i = 0; i < LADDER_ITERATIONS; ++i) {
        rng_keys[i] = random_next();
        rng_values[i] = random_next();
    
        test_put(&cache, rng_keys[i], rng_values[i], LADDER_SUBSET);
        for (size_t j = 0; j <= i; ++j) {
            printf("Trying batch %zu/%zu: ", j+1, i+1);
            test_get(&cache, rng_keys[j], rng_values[j], LADDER_SUBSET, fix);
            if (twice) {
                printf("Trying batch %zu/%zu: ", j+1, i+1);
                test_get(&cache, rng_keys[j], rng_values[j], LADDER_SUBSET, fix);
            }
        }
        madv_cache_print_stats(&cache);
    }
    
    madv_cache_free(&cache);
}


int main() {
    run_smoke_test();
    run_put_get_tests(1, 100 * M);
    run_put_get_tests(5, 4 * G);
    run_ladder_test(true, false);
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