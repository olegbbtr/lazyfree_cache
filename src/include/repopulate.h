#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "random.h"

struct repopulate_context {
    uint64_t seed;
    uint64_t hits;
};

void repopulate_init(struct repopulate_context *ctx, uint64_t seed) {
    ctx->seed = seed + 0x12345;
    ctx->hits = 0;
}

void repopulate_job(void *opaque, uint64_t key, uint8_t *value) {
    struct repopulate_context *ctx = (struct repopulate_context*) opaque;
    uint64_t *real_value = (uint64_t*) value;
    
    ctx->hits++;
    *real_value = ctx->seed + key; // Value depends on the seed and the key
}
