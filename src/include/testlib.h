#ifndef TESTLIB_H
#define TESTLIB_H


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "fallthrough_cache.h"
#include "random.h"
#include "refill.h"
#include "util.h"


struct testlib_keyset {
    uint64_t seed;
    size_t cnt;

    size_t *permutation;
    size_t pos;
};


void testlib_set_standard_order(struct testlib_keyset *keyset) {
    for (size_t i = 0; i < keyset->cnt; ++i) {
        keyset->permutation[i] = i;
    }
}

void testlib_set_random_order(struct testlib_keyset *keyset) {
    for (size_t i = 0; i < keyset->cnt; ++i) {
        size_t j = random_next() % keyset->cnt;
        size_t tmp = keyset->permutation[i];
        keyset->permutation[i] = keyset->permutation[j];
        keyset->permutation[j] = tmp;
    }
}

void testlib_init_keyset(struct testlib_keyset *keyset, size_t cnt) {
    memset(keyset, 0, sizeof(*keyset));
    keyset->seed = random_next() + 999;
    keyset->cnt = cnt;
    keyset->permutation = malloc(cnt * sizeof(size_t));
  
    testlib_set_standard_order(keyset);
}

void testlib_free_keyset(struct testlib_keyset *keyset) {
    free(keyset->permutation);
}

static void testlib_get_one(ft_cache_t *cache, struct testlib_keyset *keyset) {
    uint64_t key = keyset->seed + keyset->permutation[keyset->pos];
    uint64_t value;
    ft_cache_get(cache, key, (uint8_t*) &value);
    if (value != refill_expected(key)) {
        printf("Key %lu: Value %lu != expected %lu\n", key, value, refill_expected(key));
        exit(1);
    }
    keyset->pos++;
    if (keyset->pos == keyset->cnt) {
        keyset->pos = 0;
        testlib_set_random_order(keyset);
    }
}


static bool testlib_verbose = false;

// Returns hitrate
static float testlib_get_all(ft_cache_t *cache, 
                               struct testlib_keyset *keyset) {
    refill_ctx.count = 0;
    for (size_t i = 0; i < keyset->cnt; ++i) {
        testlib_get_one(cache, keyset);
    }
    testlib_set_standard_order(keyset);
    float hitrate = ((float) keyset->cnt - (float) refill_ctx.count) / (float) keyset->cnt;
    if (testlib_verbose) {
        printf("size=%zuMb hitrate=%.2f%%\n", keyset->cnt * PAGE_SIZE/M, hitrate * 100);
    }
    return hitrate;
}

// Returns hitrate
static float testlib_drop_all(ft_cache_t *cache,
                              struct testlib_keyset *keyset) {
    
    // fallthrough_cache_debug(cache, false);

    size_t hits = 0;
    for (size_t i = 0; i < keyset->cnt; ++i) {
        uint64_t key = keyset->seed + i;
        hits += ft_cache_drop(cache, key);
    }

    // fallthrough_cache_debug(cache, false);
    float hitrate = ((float) hits) / (float) keyset->cnt;
    // printf("dropped %zu hitrate=%.2f%%\n", keyset->cnt, hitrate * 100);
    return hitrate;
}

void testlib_reclaim(size_t size) {
    printf("Reclaiming %zu Mb\n", size/M);
    uint8_t *mem = lazyfree_mmap_anon(size);
    for (size_t i = 0; i < size/PAGE_SIZE; ++i) {
        mem[i*PAGE_SIZE] = random_next();
    }
    munmap(mem, size);
}

void testlib_reclaim_many(size_t chunks, size_t chunk_size) {
    printf("Reclaiming %zu Mb\n", chunks*chunk_size/M);
    volatile uint8_t **mem = malloc(chunks * sizeof(uint8_t*));
    for (size_t i = 0; i < chunks; ++i) {
        // usleep(100*1000);
        mem[i] = lazyfree_mmap_anon(chunk_size);
        for (size_t j = 0; j < chunk_size/PAGE_SIZE; ++j) {
            mem[i][j*PAGE_SIZE] = random_next();
        }
    }
    for (size_t i = 0; i < chunks; ++i) {
        munmap((void*) mem[i], chunk_size);
    }
    free(mem);
}


struct testlib_report {
    float hitrate;
    double latency_ns;
};

struct testlib_report testlib_measure_set(struct fallthrough_cache* cache, struct testlib_keyset* keyset) {
    testlib_set_random_order(keyset);
 
    struct testlib_report report = {0};

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    float hitrate = testlib_get_all(cache, keyset);
    clock_gettime(CLOCK_MONOTONIC, &end);
    report.latency_ns = (end.tv_sec - start.tv_sec) * 1e9;
    report.latency_ns += (end.tv_nsec - start.tv_nsec);
    report.latency_ns /= keyset->cnt;
    report.hitrate = hitrate;

    return report;
}



void testlib_print_report(struct testlib_report report, const char* prefix) {
    printf("%s_hitrate=%.2f\n", prefix, report.hitrate);
    printf("%s_latency=%.2fns\n", prefix, report.latency_ns);
}

struct hot_cold_report {
    struct testlib_report hot_before_reclaim;
    struct testlib_report hot_after_reclaim;
    struct testlib_report cold_before_reclaim;
    struct testlib_report cold_after_reclaim;

    float reclaim_latency;
};



struct hot_cold_report run_hot_cold(struct fallthrough_cache* cache, size_t set_size, size_t reclaim_size) {
    size_t hot_size = 1*G;
    size_t junk_size = 256 * M;
    size_t cold_size = (set_size - hot_size) - junk_size;
    if (testlib_verbose) {
        printf("Hot size: %zuMb, cold size: %zuMb\n", hot_size/M, cold_size/M);
    }
    struct testlib_keyset hot_set;
    testlib_init_keyset(&hot_set, hot_size/PAGE_SIZE);
    struct testlib_keyset cold_set;
    testlib_init_keyset(&cold_set, cold_size/PAGE_SIZE);
    struct testlib_keyset junk_set;
    testlib_init_keyset(&junk_set, junk_size/PAGE_SIZE);

    struct hot_cold_report report;

    int factor_hot = 3;
    int passes = 2;

    size_t total = (factor_hot + 1) * passes * (cold_size + hot_size)/PAGE_SIZE;
    printf("Starting warmup %d passes, hot %dx likely, total %zuK\n", passes, factor_hot, total/K);
    size_t cnt_hot = 0;
    size_t cnt_cold = 0;
    for (size_t i = 0; i < total; ++i) {
        bool is_cold = random_next() % (factor_hot + 1) == 0;
        if (is_cold) {
            cnt_cold++;
            testlib_get_one(cache, &cold_set);
        } else {
            cnt_hot++;
            testlib_get_one(cache, &hot_set);
        }
    }
    testlib_get_all(cache, &junk_set);
    printf("Warmup finished: hot %zu, cold %zu\n", cnt_hot, cnt_cold);

    report.hot_before_reclaim = testlib_measure_set(cache, &hot_set);
    report.cold_before_reclaim = testlib_measure_set(cache, &cold_set);

    if (reclaim_size) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        testlib_reclaim_many(8, reclaim_size/8);
        clock_gettime(CLOCK_MONOTONIC, &end);
        report.reclaim_latency = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1e6;
    }

    testlib_set_random_order(&hot_set);
    printf("Measuring hot (%zuK pages)...\n", hot_set.cnt/K);
    report.hot_after_reclaim = testlib_measure_set(cache, &hot_set);

    testlib_set_random_order(&cold_set);
    printf("Measuring cold (%zuK pages)...\n", cold_set.cnt/K);
    report.cold_after_reclaim = testlib_measure_set(cache, &cold_set);

    // printf("\nStarting final check %d times, core is %dx\n", attempts, factor);
    // for (int i = 0; i < attempts; ++i) {
    //     for (int j = 0; j < factor; ++j) {
    //         printf("Core %d: ", i);
    //         testlib_check_all(cache, core_set, testlib_order_affine);
    //     }
    //     printf("Full %d: ", i);
    //     testlib_check_all(cache, full_set, testlib_order_affine);
    // }


    testlib_drop_all(cache, &hot_set);
    testlib_drop_all(cache, &cold_set);
    testlib_drop_all(cache, &junk_set);
    testlib_free_keyset(&hot_set);
    testlib_free_keyset(&cold_set);
    testlib_free_keyset(&junk_set);

    return report;
}

#endif
