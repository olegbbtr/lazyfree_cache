#ifndef REFILL_H
#define REFILL_H

#include <stdbool.h>
#include <stdint.h>

#include "random.h"


struct refill_ctx{
    uint64_t seed;
    uint64_t count;
};

extern struct refill_ctx refill_ctx;
    
uint64_t refill_expected(uint64_t key);

void refill_cb(void* _, uint64_t key, uint8_t *value);

#endif
