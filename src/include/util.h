#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

#define K 1024ul
#define M (K*K)
#define G (K*M)

#define UNUSED(x) (void)(x)


// == Memory allocation ==

// Allocate anonymous memory.
void *lazyfree_mmap_anon(size_t size);
// Allocate file memory.
void *lazyfree_mmap_file(size_t size);

// MADV_FREE
void lazyfree_madv_free(void *memory, size_t size);

// MADV_COLD
void lazyfree_madv_cold(void *memory, size_t size);

#endif
