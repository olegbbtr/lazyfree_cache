#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef BITSET_H
#define BITSET_H

typedef uint8_t* bitset_t;

static bitset_t bitset_new(size_t size) {
    return malloc(size/8);
}

static void bitset_free(bitset_t bitset) {
    free(bitset);
}

static void bitset_put(bitset_t bitset, size_t idx, bool val) {
    if (val) {
        bitset[idx/8] |= 1 << (idx % 8);
    } else {
        // printf("DEBUG: Setting bit %zu to 0\n", idx);
        bitset[idx/8] &= ~(1 << (idx % 8));
    }
}

static bool bitset_get(bitset_t bitset, size_t idx) {
    return (bitset[idx/8] & (1 << (idx % 8))) != 0;
}

#endif

