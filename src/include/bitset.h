#ifndef BITSET_H
#define BITSET_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>


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


// struct indirect_bitset {
//     bitset_t bitset;
//     struct{ uint64_t key; bitset_t value; } *map;
//     size_t bits_per_entry;
//     size_t allocated_cnt;
// };

// static void indirect_bitset_new(struct indirect_bitset *indirect_bitset, size_t entries, size_t bits_per_entry) {
//     if (bits_per_entry < 8) {
//         bits_per_entry = 8;
//     }
//     indirect_bitset->bits_per_entry = bits_per_entry;
//     indirect_bitset->bitset = bitset_new(entries * bits_per_entry);
//     memset(indirect_bitset->bitset, 0, entries * bits_per_entry/8);
//     indirect_bitset->allocated_cnt = 0;
// }

// static void indirect_bitset_destroy(struct indirect_bitset *indirect_bitset) {
//     bitset_free(indirect_bitset->bitset);
//     hmfree(indirect_bitset->map);
// }

// static void indirect_bitset_put(struct indirect_bitset *indirect_bitset, size_t idx, bool val) {
//     size_t entry_idx = idx / indirect_bitset->bits_per_entry;
//     size_t bit_offset = idx % indirect_bitset->bits_per_entry;
//     bitset_t bitset = hmget(indirect_bitset->map, entry_idx);
//     if (bitset == NULL) {
//         size_t new_offset = indirect_bitset->allocated_cnt * indirect_bitset->bits_per_entry;
//         bitset = &indirect_bitset->bitset[new_offset];
//         hmput(indirect_bitset->map, entry_idx, bitset);

//         indirect_bitset->allocated_cnt++;
//     }
//     bitset_put(bitset, bit_offset, val);
// }

// static bool indirect_bitset_get(struct indirect_bitset *indirect_bitset, size_t idx) {
//     size_t entry_idx = idx / indirect_bitset->bits_per_entry;
//     size_t bit_offset = idx % indirect_bitset->bits_per_entry;
//     bitset_t bitset = hmget(indirect_bitset->map, entry_idx);
//     if (bitset == NULL) {
//         return false;
//     }
//     return bitset_get(bitset, bit_offset);
// }

#endif

