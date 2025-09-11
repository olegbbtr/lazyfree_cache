# MADV_FREE cache

This is a simple implementation of the in-memory cache with automated eviction policy based on the MADV_FREE [madvise(2)](https://man7.org/linux/man-pages/man2/madvise.2.html) parameter.

All memory allocated by the cache is reclaimable by the kernel when the process is under memory pressure.

TLDR API:

```c
// ENTRY_SIZE must be >= (4KB - 8). Extra space will be stored in normal memory.
#define ENTRY_SIZE 4096 

// value must point to the ENTRY_SIZE bytes of memory
// madv_cache_put returns false if the cache is full, only possible when memory limit is reached.
bool madv_cache_put(struct madv_free_cache* cache, uint64_t key, const uint8_t* value);


// madv_cache_get returns -1 if the entry was never put into the cache.
// madv_cache_get returns 0 if the entry was found. It set *value to the entry content.
// madv_cache_get returns 1 if the entry was evicted by kernel. It set *value to 0.
int madv_cache_get(struct madv_free_cache* cache, uint64_t key, uint8_t* value);
```

## Implementation details

The cache consists of a fixed number of chunks (e.g. 32).
Chunks are arranged in a circular buffer.

Each chunk has a fixed size (e.g. 2GB). 
It is allocated at the startup as `mmap` with `MAP_PRIVATE|MAP_ANONYMOUS`.

New entries are stored in the current chunk.
When the current chunk is full, that memory gets `madvise(..., MADV_FREE);`.
From that point, kernel can selectively reclaim any pages from that memory, but until that, entries are available to read.
`madv_cache_get` is able to detect when page gets cleared, and gracefully handle that case.

Simultaneously with writing to the current chunk, during `madv_cache_put()` amortized compaction is performed on the next chunk.
Compaction brings all non-reclaimed entries to the front of the chunk.
When this next chunk becomes current chunk, new entries can be written to the cleared right part of the chunk.