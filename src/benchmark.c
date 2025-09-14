#include "fallthrough_cache.h"
#include "testlib.h"

#include "lazyfree_cache.h"
#include "cache.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>


void run_full_set(struct fallthrough_cache* cache, size_t size) {
    size_t cnt = size/PAGE_SIZE - 1024;
    struct testlib_keyset keyset = testlib_init(cnt);
    testlib_check_all(cache, keyset, testlib_order_affine);
    testlib_check_all(cache, keyset, testlib_order_affine);
    testlib_drop_all(cache, keyset);
}

struct core_full_report {
    float full_hitrate;
    float core_hitrate;
    uint64_t core_latency_ms;
    uint64_t full_latency_ms;
};

struct core_full_report collect_core_full_report(struct fallthrough_cache* cache, 
    struct testlib_keyset* core_set, struct testlib_keyset* full_set) {
    struct core_full_report report;
    struct timespec start, end;
    
    clock_gettime(CLOCK_MONOTONIC, &start);
    report.full_hitrate = testlib_check_all(cache, *full_set, testlib_order_random);
    clock_gettime(CLOCK_MONOTONIC, &end);
    report.full_latency_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1e6;
    
    clock_gettime(CLOCK_MONOTONIC, &start);
    report.core_hitrate = testlib_check_all(cache, *core_set, testlib_order_random);
    clock_gettime(CLOCK_MONOTONIC, &end);
    report.core_latency_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1e6;
    
    return report;
}

void print_core_full_report(struct core_full_report report, const char* prefix) {
    printf("%s_full_hitrate=%.2f\n", prefix, report.full_hitrate);
    printf("%s_full_latency_ms=%zu\n", prefix, report.full_latency_ms);
    printf("%s_core_hitrate=%.2f\n", prefix, report.core_hitrate);
    printf("%s_core_latency_ms=%zu\n", prefix, report.core_latency_ms);
}

struct benchmark_report {
    struct core_full_report before;
    struct core_full_report after;

    float reclaim_latency;
};



struct benchmark_report run_core_and_full(struct fallthrough_cache* cache, size_t size, bool reclaim) {
    size_t core_size = 128 * M;
    size_t full_size = (size - core_size) - 1*M;
    printf("Core size: %zuMb, full size: %zuMb\n", core_size/M, full_size/M);
    struct testlib_keyset core_set = testlib_init(core_size/PAGE_SIZE);
    struct testlib_keyset full_set = testlib_init(full_size/PAGE_SIZE);

    struct benchmark_report report;

    int factor = 4;
    int attempts = 2;
    printf("Starting warmup %d times, core set is %dx likely\n", attempts, factor);
    for (int i = 0; i < attempts; ++i) {
        printf("Full %d: ", i);
        testlib_check_all(cache, full_set, testlib_order_affine);

        for (int j = 0; j < factor; ++j) {
            printf("Core %d: ", i);
            testlib_check_all(cache, core_set, testlib_order_affine);
        }
    }

    report.before = collect_core_full_report(cache, &core_set, &full_set);
    
    sleep(1);
    
    if (reclaim) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        testlib_reclaim_many(12, size/16);
        clock_gettime(CLOCK_MONOTONIC, &end);
        report.reclaim_latency = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1e6;
    }

    sleep(1);

    report.after = collect_core_full_report(cache, &core_set, &full_set);

    // printf("\nStarting final check %d times, core is %dx\n", attempts, factor);
    // for (int i = 0; i < attempts; ++i) {
    //     for (int j = 0; j < factor; ++j) {
    //         printf("Core %d: ", i);
    //         testlib_check_all(cache, core_set, testlib_order_affine);
    //     }
    //     printf("Full %d: ", i);
    //     testlib_check_all(cache, full_set, testlib_order_affine);
    // }


    testlib_drop_all(cache, core_set);
    testlib_drop_all(cache, full_set);

    return report;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        printf("Usage: %s <impl> <memory_size_gb> <cache_size_factor>\n", argv[0]);       
        printf("Impls: lazyfree, normal, disk\n");
        return 1;
    }
    size_t memory_size = atoll(argv[2]);
    if (memory_size < 1) {
        printf("Memory size must be at least 1Gb\n");
        return 1;
    }
    size_t cache_size = memory_size * G;

    float cache_size_factor = strtof(argv[3], NULL);
    if (cache_size_factor <= 0) {
        printf("Cache size factor must be positive\n");
        return 1;
    }

    struct cache_impl impl = lazyfree_cache_impl;
    if (strcmp(argv[1], "lazyfree") == 0) {
        
    } else if (strcmp(argv[1], "disk") == 0) {
        impl.mmap_impl = mmap_file;
        impl.madv_impl = madv_cold;
    } else if (strcmp(argv[1], "normal") == 0) {
        impl.madv_impl = madv_noop;
    } else {
        printf("Unknown impl: %s\n", argv[1]);
        return 1;
    }

    random_rotate();

    printf("\n== Benchmark: %s, cache_size=%zu GB ==\n", argv[1], cache_size/G);
    struct fallthrough_cache* cache = fallthrough_cache_new(impl,
                                            cache_size*cache_size_factor,
                                            sizeof(uint64_t),
                                            1,
                                            refill_cb);
    // run_full_set(cache, cache_size);
    struct benchmark_report report = run_core_and_full(cache, cache_size, true);
    printf("\n== Report ==\n");
    printf("impl=%s\n", argv[1]);
    printf("cache_size_gb=%zu\n", cache_size/G);
    print_core_full_report(report.before, "before");

    printf("reclaim_latency=%.2f ms\n", report.reclaim_latency);
    
    print_core_full_report(report.after, "after");


    fallthrough_cache_free(cache);
}







// #define SUBSET_ITERATIONS 10
// #define SUBSET_CNT 16
// #define TOTAL_SIZE (20 * G)
// #define ENTRIES_PER_SUBSET (TOTAL_SIZE / SUBSET_CNT / PAGE_SIZE)

// #define SUBSET_SPECIAL_CNT 5



// void run_special_subsets_test() {
//     printf("\n==Starting special subsets test\n");
//     float special_ratio = (float) SUBSET_SPECIAL_CNT / (float) SUBSET_CNT;
//     float special_size = special_ratio * TOTAL_SIZE;
//     printf("Total size: %zuGb. Special size: %.2fGb\n", TOTAL_SIZE/G, special_size/G);
//     system("free -h");
//     uint64_t rng_keys[SUBSET_CNT];
//     uint64_t rng_values[SUBSET_CNT];
//     size_t special_idxs[SUBSET_SPECIAL_CNT];

//     for (size_t i = 0; i < SUBSET_SPECIAL_CNT; ++i) {
//         special_idxs[i] = random_next() % SUBSET_CNT;
//     }
    
//     for (size_t i = 0; i < SUBSET_CNT; ++i) {
//         rng_keys[i] = random_next();
//         rng_values[i] = random_next();
//     }

//     struct madv_cache cache;
//     madv_cache_init(&cache, DEFAULT_CACHE_SIZE);
    
//     // Put everything
//     for (int j = 0; j < SUBSET_CNT; ++j) {
//         printf("Initializing subset %d/%d: ", j, SUBSET_CNT);
//         test_put(&cache, rng_keys[j], rng_values[j], ENTRIES_PER_SUBSET);
//     }
//     for (int i = 0; i < SUBSET_ITERATIONS; ++i) {
//         // Special
//         float special_hitrate_sum = 0;
//         for (size_t j = 0; j < SUBSET_CNT; ++j) {
//             size_t special_idx = special_idxs[j % SUBSET_SPECIAL_CNT];

//             printf("Trying special %zu/%d: ", special_idx, SUBSET_CNT);
//             special_hitrate_sum += test_get(
//                 &cache, 
//                 rng_keys[special_idx],
//                 rng_values[special_idx],
//                 ENTRIES_PER_SUBSET,
//                 true);
//         }
//         // Everything
//         float normal_hitrate_sum = 0;
//         for (size_t j = 0; j < SUBSET_CNT; ++j) {
//             printf("Trying normal %zu/%d: ", j, SUBSET_CNT);
//             normal_hitrate_sum += test_get(
//                 &cache, 
//                 rng_keys[j],
//                 rng_values[j],
//                 ENTRIES_PER_SUBSET,
//                 true);
//         }
//         float special_hitrate = special_hitrate_sum / SUBSET_CNT;
//         float normal_hitrate = normal_hitrate_sum / SUBSET_CNT;
//         printf("Special hitrate: %.2f%%, normal hitrate: %.2f%%\n", special_hitrate * 100, normal_hitrate * 100);
//         sleep(1);
//     }
//     madv_cache_free(&cache);
// }

// struct distribution {
//     uint64_t prob[SUBSET_CNT];
//     uint64_t sum;
// };

// int distribution_next(struct distribution* dist) {
//     if (dist->sum == 0) {
//         for (int i = 0; i < SUBSET_CNT; ++i) {
//             dist->sum += dist->prob[i];
//         }
//     }
//     uint64_t rng = random_next() % dist->sum;
//     // printf("rng: %zu, sum: %zu\n", rng, dist->sum);
//     uint64_t acc = 0;
//     for (int i = 0; i < SUBSET_CNT; ++i) {
//         acc += dist->prob[i];
//         if (rng < acc) {
//             // printf("Chose %d, acc: %lu \n", i + 1, acc);
//             return i;
//         }
//     }
//     return SUBSET_CNT - 1;
// }

// #define DECAY_FACTOR 0.9

// void run_linear_subsets_test() {
//     printf("\n==Starting linear subsets test\n");
//     printf("Total size: %zuGb. Per set size: %.2fGb\n", TOTAL_SIZE/G, TOTAL_SIZE/(float)SUBSET_CNT/G);
//     system("free -h");
//     uint64_t rng_keys[SUBSET_CNT];
//     uint64_t rng_values[SUBSET_CNT];
//     struct distribution dist;
//     memset(&dist, 0, sizeof(dist));


//     float rolling_hitrate[SUBSET_CNT];
//     memset(rolling_hitrate, 0, sizeof(rolling_hitrate));
//     for (int i = 0; i < SUBSET_CNT; ++i) {
//         rng_keys[i] = random_next();
//         rng_values[i] = random_next();
//         dist.prob[i] = i;
//         rolling_hitrate[i] = 0.5;
//     }

//     struct madv_cache cache;
//     madv_cache_init(&cache);
    
//     // Put everything
//     for (int j = 0; j < SUBSET_CNT; ++j) {
//         printf("Initializing subset %d/%d: ", j+1, SUBSET_CNT);
//         test_put(&cache, rng_keys[j], rng_values[j], ENTRIES_PER_SUBSET);
//     }

//     for (int i = 0; i < SUBSET_ITERATIONS; ++i) {    
//         // Do SUBSET_CNT number of random set reads
//         for (int j = 0; j < SUBSET_CNT; ++j) {
//             int set_id = distribution_next(&dist);
//             printf("Trying set %d/%d: ", set_id+1, SUBSET_CNT);
//             float hitrate = test_get(
//                 &cache, 
//                 rng_keys[set_id], 
//                 rng_values[set_id], 
//                 ENTRIES_PER_SUBSET, 
//                 true);
//             rolling_hitrate[set_id] *= DECAY_FACTOR;
//             rolling_hitrate[set_id] += (1 - DECAY_FACTOR) * hitrate;
//         }

//         float diff_to_ideal = 0;
//         printf("Hitrate: ");
//         for (int j = 0; j < SUBSET_CNT; ++j) {
//             float hitrate = rolling_hitrate[j];
//             float ideal_hitrate = (float) j / (float) SUBSET_CNT;
//             printf("%d: %.2f%% (ideal: %.2f%%) ", j+1, hitrate * 100, ideal_hitrate * 100);
//             diff_to_ideal += fabs(hitrate - ideal_hitrate);
//         }
//         printf("\n");
//         printf("Diff to ideal: %.2f%%\n", diff_to_ideal * 100);
//         sleep(1);
//     }
//     madv_cache_free(&cache);
// }

// #define SUBSET_SIZE (1 * G)

// void put_large(struct madv_cache* cache, uint64_t rng_key, uint64_t rng_value, size_t size) {
//     assert(size % SUBSET_SIZE == 0);
    
//     size_t subsets = size / SUBSET_SIZE;
//     printf("Putting %zuGb in %zu subsets\n", size / G, subsets);
//     for (size_t i = 0; i < subsets; ++i) {
//         printf("Put subset %zu/%zu: ", i, subsets);
//         test_put(cache, rng_key + i, rng_value + i, SUBSET_SIZE);
//     }
// }

// float get_large(struct madv_cache* cache, uint64_t rng_key, uint64_t rng_value, size_t size, bool fix) {
//     assert(size % SUBSET_SIZE == 0);
    
//     size_t subsets = size / SUBSET_SIZE;
//     printf("Getting %zuGb in %zu subsets\n", size / G, subsets);
//     float hitrate_sum = 0;
//     for (size_t i = 0; i < subsets; ++i) {
//         printf("Get subset %zu/%zu: ", i, subsets);
//         hitrate_sum += test_get(cache, rng_key + i, rng_value + i, SUBSET_SIZE, fix);
//     }
//     return hitrate_sum / subsets;
// }

// #define LARGE_SIZE (20 * G)
// #define SMALL_SIZE (10 * M)

// void run_large_then_small_test() {
//     printf("\n==Starting large then small test\n");
//     system("free -h");
//     printf("Large size: %zuGb, small size: %zuMb\n", LARGE_SIZE/G, SMALL_SIZE/M);
//     struct madv_cache cache;
//     madv_cache_init(&cache);
    
//     uint64_t rng_key = random_next();
//     uint64_t rng_value = random_next();
    
//     put_large(&cache, rng_key, rng_value, LARGE_SIZE);


//     get_large(&cache, rng_key, rng_value, 1 * G, true);
//     get_large(&cache, rng_key, rng_value, 1 * G, true);
    
//     sleep(1);
    
//     test_put(&cache, rng_key, rng_value, SMALL_SIZE / PAGE_SIZE);

//     test_get(&cache, rng_key, rng_value, SMALL_SIZE / PAGE_SIZE, true);
//     test_get(&cache, rng_key, rng_value, SMALL_SIZE / PAGE_SIZE, true);

//     madv_cache_free(&cache);
// }


// void run_fix_test() {
//     printf("\n==Starting fix test\n");
//     system("free -h");
//     printf("Large size: %zuGb, small size: %zuMb\n", LARGE_SIZE/G, SMALL_SIZE/M);
//     struct madv_cache cache;
//     madv_cache_init(&cache);
    
//     uint64_t rng_key = random_next();
//     uint64_t rng_value = random_next();
    
//     put_large(&cache, rng_key, rng_value, LARGE_SIZE);


//     float hitrate = test_get(&cache, rng_key, rng_value, 10 * PAGE_SIZE, true);
//     assert(hitrate < 0.99);

//     cache.verbose = true;
//     hitrate = test_get(&cache, rng_key, rng_value, 10 * PAGE_SIZE, true);
//     assert(hitrate > 0.99);

//     madv_cache_free(&cache);
// }


// #define LADDER_ITERATIONS 20
// #define LADDER_SUBSET (2 * G)
// void run_ladder_test(bool fix, bool twice) {
//     printf("\n==Starting ladder test, fix: %d\n", fix);
//     system("free -h");


//     struct madv_cache cache;
//     madv_cache_init(&cache);
    
//     uint64_t rng_keys[LADDER_ITERATIONS];
//     uint64_t rng_values[LADDER_ITERATIONS];
//     for (size_t i = 0; i < LADDER_ITERATIONS; ++i) {
//         rng_keys[i] = random_next();
//         rng_values[i] = random_next();
    
//         test_put(&cache, rng_keys[i], rng_values[i], LADDER_SUBSET);
//         for (size_t j = 0; j <= i; ++j) {
//             printf("Trying batch %zu/%zu: ", j+1, i+1);
//             test_get(&cache, rng_keys[j], rng_values[j], LADDER_SUBSET, fix);
//             if (twice) {
//                 printf("Trying batch %zu/%zu: ", j+1, i+1);
//                 test_get(&cache, rng_keys[j], rng_values[j], LADDER_SUBSET, fix);
//             }
//         }
//         madv_cache_print_stats(&cache);
//     }
    
//     madv_cache_free(&cache);
// }
