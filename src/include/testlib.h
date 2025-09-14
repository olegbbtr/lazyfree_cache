#ifndef TESTLIB_H
#define TESTLIB_H


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "fallthrough_cache.h"
#include "refill.h"


struct testlib_keyset {
    uint64_t seed;
    size_t cnt;
};

struct testlib_keyset testlib_init(size_t cnt) {
    struct testlib_keyset keyset;
    keyset.seed = random_next();
    keyset.cnt = cnt;
    return keyset;
}

const char *testlib_order_random = "random";
const char *testlib_order_affine = "affine";
const char *testlib_order_affine_chunk = "affine_chunk";

static float testlib_check_all(struct fallthrough_cache *cache, 
                               struct testlib_keyset keyset, 
                               const char *order) {
    refill_ctx.count = 0;
    printf("checking pages=%zu order=%s ", keyset.cnt, order);
    size_t affine_chunks = 1;
    if (order == testlib_order_affine_chunk) {
        affine_chunks = 4*K;
    }
    size_t affine_per_chunk = keyset.cnt / affine_chunks;
    size_t affine_chunk = random_next() % affine_chunks;
    size_t affine_offset = random_next() % affine_per_chunk;
    for (size_t i = 0; i < keyset.cnt; ++i) {
        uint64_t key = keyset.seed;
        if (order == testlib_order_random) {
            key += random_next() % keyset.cnt;
        } else {
            key += affine_chunk * affine_per_chunk + affine_offset;
        }
        uint64_t value;
        fallthrough_cache_get(cache, key, (uint8_t*) &value);

        if (value != refill_expected(key)) {
            printf("Key %lu: Value %lu != expected %lu\n", key, value, refill_expected(key));
            exit(1);
        }
        affine_offset = (affine_offset + 1) % affine_per_chunk;
        if (affine_offset == 0) {
            affine_chunk = (affine_chunk + 1) % affine_chunks;
        }
        assert(value == refill_expected(key));
    }
    float hitrate = ((float) keyset.cnt - (float) refill_ctx.count) / (float) keyset.cnt;
    printf("hitrate=%.2f%%\n", hitrate * 100);
    return hitrate;
}

// Returns hitrate
static float testlib_drop_all(struct fallthrough_cache *cache,
                             struct testlib_keyset keyset) {
    
    // fallthrough_cache_debug(cache, false);

    size_t hits = 0;
    for (size_t i = 0; i < keyset.cnt; ++i) {
        uint64_t key = keyset.seed + i;
        hits += fallthrough_cache_drop(cache, key);
    }

    // fallthrough_cache_debug(cache, false);
    float hitrate = ((float) hits) / (float) keyset.cnt;
    printf("dropped %zu hitrate=%.2f%%\n", keyset.cnt, hitrate * 100);
    return hitrate;
}



void testlib_reclaim(size_t size) {
    printf("Reclaiming %zu Mb\n", size/M);
    uint8_t *mem = mmap_normal(size);
    for (size_t i = 0; i < size/PAGE_SIZE; ++i) {
        mem[i*PAGE_SIZE] = random_next();
    }
    munmap(mem, size);
}

void testlib_reclaim_many(size_t chunks, size_t chunk_size) {
    printf("Reclaiming %zu Mb\n", chunks*chunk_size/M);
    uint8_t **mem = malloc(chunks * sizeof(uint8_t*));
    for (size_t i = 0; i < chunks; ++i) {
        // usleep(100*1000);
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

#endif
