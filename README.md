# LazyFree userspace cache

This is an implementation of LazyFree cache in userspace.
In this implementation, kernel can evict any page when there is memory pressure.
It is based on `MADV_FREE` ([madvise(2)](https://man7.org/linux/man-pages/man2/madvise.2.html)) parameter. (TODO version linux kernel)

This implementation can be useful if running on a system with occasional unpredictable memory pressure, such
that all the data is reconstructable from other sources.

## API

```c
// Must be equal to kernel PAGE_SIZE
#define PAGE_SIZE 4096

// Cache capacity is in bytes. 
// If the cache is full, it will start evicting random chunks.
lfcache_t lazyfree_cache_new(size_t cache_capacity);

void lazyfree_cache_free(lfcache_t cache);

// == Low-level API ==
// This might look complex, but it is required to support zero-copy.

// Lock the cache to write to the key.
//  - Sets 'value' to point at the page inside the cache.
//  - Next call must be lazyfree_cache_unlock.
//    Pointer is valid until then.
//  - Returns true if the key has existing data.
//    Returns false if the key was just allocated and is empty.
bool lazyfree_cache_write_lock(lfcache_t cache, 
                               cache_key_t key,
                               uint8_t **value);
                           
//  Try to take optmistic read lock.
//  If fails, returns false and leaves the cache unlocked.
//  Otherwise:
//  - Sets 'head' to the first byte of the page.
//  - Sets 'tail' to point at the [1:PAGE_SIZE] of the page.
//  - After reading from 'tail', must call lazyfree_cache_read_lock_check 
//    to see if the lock is still valid.
//  - 'tail' is valid until lazyfree_cache_unlock.
bool lazyfree_cache_read_try_lock(lfcache_t cache, 
                                  cache_key_t key,
                                  uint8_t *head,
                                  uint8_t **tail);

// Check if the read lock is still valid.
bool lazyfree_cache_read_lock_check(lfcache_t cache);

// Unlock the cache.
// If 'drop' is true, the key is dropped from the cache.
void lazyfree_cache_unlock(lfcache_t cache, bool drop);
```

## Implementation details

The cache consists of a fixed number of chunks (e.g. 32).
Chunks are arranged in a circular buffer.

New entries are stored in the current chunk.
When the current chunk is full, that memory gets `madvise(..., MADV_FREE);`.
From that point, kernel can selectively reclaim any pages from that memory, but until that, entries are available to read.
`madv_cache_get` is able to detect when page gets cleared, and gracefully handle that case.

Simultaneously with writing to the current chunk, during `madv_cache_put()` amortized compaction is performed on the next chunk.
Compaction brings all non-reclaimed entries to the front of the chunk.
When this next chunk becomes current chunk, new entries can be written to the cleared right part of the chunk.

## Extra implementations

This repository also includes:

1. Fallthrough cache - wraps around LazyFree cache and provides simpler API:

```c
ft_cache_t fallthrough_cache_new(struct lfcache_impl impl, 
                                  size_t cache_size,
                                  size_t entry_size,
                                  refill_cb_t refill_cb, 
                                  void *refill_opaque);

void fallthrough_cache_free(ft_cache_t cache);

void fallthrough_cache_get(ft_cache_t cache, 
                           cache_key_t key, 
                           uint8_t *value);

void fallthrough_cache_drop(ft_cache_t cache, cache_key_t key);
```


2. Disk cache - Same implementation as LazyFree, but stores data on disk instead of LazyFree memory.

```c
lfcache_t disk_cache_new(size_t capacity, char *path);

// Use all other functions from LazyFree cache.
```


## Benchmark results

```text
impl=lazyfree
cache_size_gb=4
before_full_hitrate=1.00
before_full_latency_ms=373
before_core_hitrate=1.00
before_core_latency_ms=9
reclaim_latency=1620.75 ms       <--
after_full_hitrate=0.56          <--
after_full_latency_ms=13419      <--
after_core_hitrate=1.00
after_core_latency_ms=13


impl=disk
cache_size_gb=4
before_full_hitrate=1.00
before_full_latency_ms=420
before_core_hitrate=1.00
before_core_latency_ms=9
reclaim_latency=2028.91 ms       <--
after_full_hitrate=1.00          <--
after_full_latency_ms=13349      <--
after_core_hitrate=1.00
after_core_latency_ms=11


impl=normal
cache_size_gb=4
before_full_hitrate=0.24
before_full_latency_ms=1875
before_core_hitrate=0.37
before_core_latency_ms=63
reclaim_latency=1271.18 ms       <--
after_full_hitrate=0.23          <--
after_full_latency_ms=1564       <--
after_core_hitrate=0.37
after_core_latency_ms=32
```




## Futher improvements

Can be made multithread safe with RWLock semantics, if our hashmap would support it. Can even have multiple writers, if we have multiple open chunks.


Fallthrough cache should be able to pack multiple entries into a single page.
