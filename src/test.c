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

#include "random.h"
#include "refill.h"
#include "testlib.h"


#define SMOKE_TEST_CNT 10
void run_smoke_test(struct fallthrough_cache *cache) {
    refill_ctx.count = 0;

    uint64_t keys[SMOKE_TEST_CNT];
    for (size_t i = 0; i < SMOKE_TEST_CNT; ++i) {
        keys[i] = i;
        uint64_t value;
        ft_cache_get(cache, keys[i], (uint8_t*) &value);

        // Refills one more time
        assert(refill_ctx.count == i + 1);
    }

    uint64_t values[SMOKE_TEST_CNT];
    for (size_t i = 0; i < SMOKE_TEST_CNT; ++i) {
        ft_cache_get(cache, keys[i], (uint8_t*) &values[i]);
        if (values[i] != refill_expected(keys[i])) {
            printf("Value %zu: %lu != expected %lu\n", i, values[i], refill_expected(keys[i]));
            exit(1);
        }
        // Refill should not be called
        assert(refill_ctx.count == 10);
    }

    for (size_t i = 0; i < SMOKE_TEST_CNT; ++i) {
        bool ok = ft_cache_drop(cache, keys[i]);
        assert(ok);
    }

    assert(refill_ctx.count == 10);
}


float check_hitrate(struct fallthrough_cache *cache, size_t size) {
    struct testlib_keyset keyset;
    testlib_init_keyset(&keyset, size/PAGE_SIZE);
    
    
    float hitrate1 = testlib_get_all(cache, &keyset);
    if (hitrate1 > 0) {
        printf("Hitrate1 must be 0\n");
        exit(1);
    }
    
    float hitrate2 = testlib_get_all(cache, &keyset);

    if (hitrate2 < hitrate1) {
        printf("hitrate2=%.2f < hitrate1=%.2f\n", hitrate2, hitrate1);
        exit(1);
    }
    
    testlib_drop_all(cache, &keyset);
    return hitrate2;
}


// void mem_pressure_test(size_t size) {
//     struct testlib_keyset keyset = testlib_init(get_cnt(size));
    
//     uint64_t attempts = 5;
//     for (uint64_t i = 0; i < attempts; ++i) {
//         float hitrate = testlib_check_all(cache, keyset, testlib_order_affine_chunk);
//         printf("Hitrate before reclaim: %.2f%%\n", hitrate * 100);
//         sleep(1);
//         // fallthrough_cache_debug(cache, false);
//     }
//     // fallthrough_cache_debug(cache, false);
    
    
//     testlib_reclaim_many(3, size/4);
//     sleep(1);
    
//     for (uint64_t i = 0; i < attempts; ++i) {
//         float hitrate = testlib_check_all(cache, keyset, testlib_order_affine_chunk);
//         printf("Hitrate after reclaim: %.2f%%\n", hitrate * 100);
//         // fallthrough_cache_debug(cache, false);
//     }

//     testlib_drop_all(cache, keyset);
//     // fallthrough_cache_debug(cache, false);
//     sleep(1);

//     for (uint64_t i = 0; i < attempts; ++i) {
//         float hitrate = testlib_check_all(cache, keyset, testlib_order_affine_chunk);
//         printf("Hitrate after reclaim and drop: %.2f%%\n", hitrate * 100);
//         // fallthrough_cache_debug(cache, false);
//     }
// }

size_t get_set_size(size_t memory_size) {
    size_t num_entries = memory_size/PAGE_SIZE - 128;
    size_t set_size = num_entries*PAGE_SIZE;
    printf("Number of entries: %zu\n", num_entries);
    return set_size;
}    


void suite_lazyfree(size_t memory_size, bool full) {
    size_t set_size = get_set_size(memory_size);

    struct lazyfree_impl impl = lazyfree_impl();
    ft_cache_t cache;
    ft_cache_init(&cache, impl, 
        set_size/PAGE_SIZE, sizeof(uint64_t), 
        refill_cb, NULL);


    run_smoke_test(&cache);

    if (!full) {
        return;
    }
    
    float hitrate = check_hitrate(&cache, set_size);
    if (hitrate < 0.7) {
        printf("set_size=%zuMb hitrate=%.2f, expect >= 0.8\n", set_size/M, hitrate);
        exit(1);
    }

    // 2x hitrate should be >15%
    hitrate = check_hitrate(&cache, 2*set_size);
    if (hitrate < 0.15) {
        printf("set_size=%zuMb hitrate=%.5f, expect >= 0.15\n", set_size/M, hitrate);
        exit(1);
    }
}

void suite_anon(size_t memory_size) {
    struct lazyfree_impl impl = lazyfree_anon_impl();   
    size_t set_size = get_set_size(memory_size);
    ft_cache_t cache;

    ft_cache_init(&cache, impl, 
        set_size/PAGE_SIZE, sizeof(uint64_t), 
        refill_cb, NULL);

    run_smoke_test(&cache);    

    float hitrate = check_hitrate(&cache, set_size);
    if (hitrate < 1) {
        printf("set_size=%zuMb hitrate=%.2f, expect >= 1\n", set_size/M, hitrate);
        exit(1);
    }
}

void suite_disk(size_t memory_size) {
    struct lazyfree_impl impl = lazyfree_disk_impl();
    size_t set_size = get_set_size(memory_size);
    ft_cache_t cache;

    ft_cache_init(&cache, impl, 
        set_size/PAGE_SIZE, sizeof(uint64_t), 
        refill_cb, NULL);

    run_smoke_test(&cache);

    float hitrate = check_hitrate(&cache, set_size);
    assert(hitrate == 1);
}


int main(int argc, char **argv) {
    testlib_verbose = true;


    if (argc < 3) {
        printf("Usage: %s <suite> <memory_size_gb>\n", argv[0]);       
        printf("Suites: lazyfree, lazyfree_full, anon, disk\n");
        return 1;
    }
    size_t memory_size_gb = atoll(argv[2]);
    if (memory_size_gb < 1) {
        printf("Memory size must be at least 1Gb\n");
        return 1;
    }
    size_t memory_size = memory_size_gb * G;


    random_rotate();
    refill_ctx.seed = random_next();
    refill_ctx.count = 0;

    printf("== Starting suite %s ==\n", argv[1]);
    if (strcmp(argv[1], "lazyfree") == 0) {
        suite_lazyfree(memory_size, false);
    } else if (strcmp(argv[1], "lazyfree_full") == 0) {
        suite_lazyfree(memory_size, true);
    } else if (strcmp(argv[1], "anon") == 0) {
        suite_anon(memory_size);
    } else if (strcmp(argv[1], "disk") == 0) {
        suite_disk(memory_size);
    } else {
        printf("Unknown suite: %s\n", argv[1]);
        return 1;
    }

    return 0;
}
