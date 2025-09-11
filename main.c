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

#include "madv_free_cache.h"




void smoke_test(struct madv_free_cache *cache) {
    madv_cache_put(cache, 0, (uint8_t*) "Hello, World!");
    madv_cache_put(cache, 1, (uint8_t*) "More text");
    madv_cache_put(cache, 2, (uint8_t*) "Even more text");


    char value[ENTRY_SIZE];
    
    int res = madv_cache_get(cache, 0, (uint8_t*) value);
    printf("value: %s\n", value);
    assert(res==0);
    assert(strcmp(value, "Hello, World!") == 0);


    res = madv_cache_get(cache, 1, (uint8_t*) value);
    printf("value: %s\n", value);
    assert(res==0);
    assert(strcmp(value, "More text") == 0);


    res = madv_cache_get(cache, 2, (uint8_t*) value);
    printf("value: %s\n", value);
    assert(res==0);
    assert(strcmp(value, "Even more text") == 0);
}

static uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void next_value(char value[ENTRY_SIZE], uint64_t* rng_key, uint64_t* rng_value) {
    *rng_key = splitmix64(rng_key);
    *rng_value = splitmix64(rng_value);
    sprintf(value, "Value %lu:%lu", *rng_key, *rng_value);
}

void test_put(struct madv_free_cache* cache, uint64_t rng_key, uint64_t rng_value, size_t num) {
    for (size_t i = 0; i < num; ++i) {
        char value[ENTRY_SIZE];
        next_value(value, &rng_key, &rng_value);

        bool result = false;
        while(!result) {
            result = madv_cache_put(cache, rng_key, (uint8_t*) value);
        }
    }
    printf("Put %lu K values\n", num/K);
}

float test_get(struct madv_free_cache* cache, uint64_t rng_key, uint64_t rng_value, size_t num, bool fix) {
    size_t misses = 0;
    for (size_t i = 0; i < num; ++i) {
        char expected[ENTRY_SIZE];
        next_value(expected, &rng_key, &rng_value);

        char value[ENTRY_SIZE];
        int res = madv_cache_get(cache, rng_key, (uint8_t*) value);
        assert(res>=0);
        if (res==1) {
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
    printf("Hits: %zuK of %zuK (%.2f%%)\n", (num - misses)/K, num/K, hitrate * 100);
    return hitrate;
}

uint64_t global_seed = 1;

uint64_t next_seed(void) {
    global_seed = splitmix64(&global_seed);
    return global_seed + 1; // So that we don't follow the same path every time
}

void put_get_test(struct madv_free_cache *cache, size_t iterations, size_t entries_cnt) {
    printf("==Put-get test: %zu iterations, %zuK entries per iteration\n", iterations, entries_cnt/K);
    for (size_t i = 0; i < iterations; ++i) {
        uint64_t rng_key = next_seed();
        uint64_t rng_value = next_seed();
        
        test_put(cache, rng_key, rng_value, entries_cnt);
        test_get(cache, rng_key, rng_value, entries_cnt, false);
        printf("Iteration %zu done\n\n", i + 1);
        sleep(1);
    }
}

void run_put_get_tests(size_t iterations, size_t entries_cnt){
    struct madv_free_cache cache;
    madv_cache_init(&cache);
    put_get_test(&cache, iterations, entries_cnt);
    madv_cache_free(&cache);
}

void run_flush_then_small_test(void) {
    printf("\nStarting flush then small test\n\n");

    struct madv_free_cache cache;
    madv_cache_init(&cache);
    put_get_test(&cache, 10, 3 * M);
    printf("Now small\n\n");  

    sleep(1);

    uint64_t rng_key = next_seed();
    uint64_t rng_value = next_seed();
    
    test_put(&cache, rng_key, rng_value, 10*1000);
    for (size_t i = 0; i < 10; ++i) {
        test_get(&cache, rng_key, rng_value, 10*1000, true);
        sleep(1);
    }
    madv_cache_free(&cache);
}

void run_smoke_test(void) {
    struct madv_free_cache cache;
    madv_cache_init(&cache);
    smoke_test(&cache);
    madv_cache_free(&cache);
}

#define LADDER_ITERATIONS 20
#define LADDER_PER_ITERATION (500 * K)

void run_ladder_test(bool fix) {
    printf("\n==Starting ladder test, fix: %d\n", fix);
    system("free -h");
    printf("Per iteration: %.2fGb\n", (float) LADDER_PER_ITERATION * ENTRY_SIZE / (float) G);
    struct madv_free_cache cache;
    madv_cache_init(&cache);

    
    uint64_t rng_keys[LADDER_ITERATIONS];
    uint64_t rng_values[LADDER_ITERATIONS];
    for (size_t i = 0; i < LADDER_ITERATIONS; ++i) {
        rng_keys[i] = next_seed();
        rng_values[i] = next_seed();
    
        test_put(&cache, rng_keys[i], rng_values[i], LADDER_PER_ITERATION);
        for (size_t j = 0; j < i; ++j) {
            printf("Trying previous batach %zu/%zu: ", j, i);
            test_get(&cache, rng_keys[j], rng_values[j], LADDER_PER_ITERATION, true);
        }
        sleep(1);
    }
    
    madv_cache_free(&cache);
}

#define SUBSET_ITERATIONS 10
#define SUBSET_CNT 16
#define TOTAL_SIZE (20 * G)
#define ENTRIES_PER_SUBSET (TOTAL_SIZE / SUBSET_CNT / ENTRY_SIZE)

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
        special_idxs[i] = next_seed() % SUBSET_CNT;
    }
    
    for (size_t i = 0; i < SUBSET_CNT; ++i) {
        rng_keys[i] = next_seed();
        rng_values[i] = next_seed();
    }

    struct madv_free_cache cache;
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
    uint64_t rng = next_seed() % dist->sum;
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
        rng_keys[i] = next_seed();
        rng_values[i] = next_seed();
        dist.prob[i] = i;
        rolling_hitrate[i] = 0.5;
    }

    struct madv_free_cache cache;
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


int main() {
    // run_smoke_test();
    run_put_get_tests(1, 10*K);
    // run_ladder_test(true);
    run_linear_subsets_test();
    // run_special_subsets_test();

    // run_put_get_tests(1, 100*1000);
    // run_put_get_tests(10, 1000*1000);
    // run_flush_then_small_test();
    return 0;
}