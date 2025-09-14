#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "random.h"



struct {
    uint64_t seed;
    uint64_t count;
} refill_ctx;

    
uint64_t refill_expected(uint64_t key) {
    return refill_ctx.seed + key;
}

void refill_cb(void* _, uint64_t key, uint8_t *value) {
    refill_ctx.count++;
    uint64_t *real_value = (uint64_t*) value;
    
    *real_value = refill_ctx.seed + key; // Value depends on the seed and the key
}
