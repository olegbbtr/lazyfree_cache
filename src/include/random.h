#include <stdint.h>

#ifndef __random_h_
#define __random_h_

static uint64_t random_mix(uint64_t *state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static uint64_t __global_seed = 1;

static uint64_t random_next(void) {
    __global_seed = random_mix(&__global_seed);
    return __global_seed ^ 0xdeadbeef; // So that we don't follow the same path every time
}

#endif
