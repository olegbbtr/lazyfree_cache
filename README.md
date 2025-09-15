# LazyFree userspace cache

This is an implementation of LazyFree cache in userspace.
It provides zero-copy API to allocate pages.
In this implementation, kernel can evict any page when there is memory pressure.

This is possible due to the `MADV_FREE` ([madvise(2)](https://man7.org/linux/man-pages/man2/madvise.2.html)) flag. It is avaliable since Linux 4.5.

This implementation can be useful if running on a system with occasional unpredictable memory pressure, such
that all the data is reconstructable from other sources.

## LazyFree API

[lazyfree_cache.h](include/lazyfree_cache.h) provides default LazyFree API.

```c
// == Core API ==
typedef struct lazyfree_cache* lazyfree_cache_t;
lazyfree_cache_t lazyfree_cache_new(size_t cache_capacity);
void lazyfree_cache_free(lazyfree_cache_t cache);

typedef uint64_t lazyfree_key_t;


// == Optimistic Read Lock API ==
// This might look complicated, but it is necessary to support
// zero-copy reads.

typedef struct {
    /* ... */
    uint8_t head;       // First byte of the page
    uint8_t* tail;      // [1:PAGE_SIZE]
} lazyfree_rlock_t;  

// Take an optimistic read lock.
// Returns the handle with two fields:
//    head - page[0]
//    tail - ptr to page[1:PAGE_SIZE] if found, NULL otherwise
// The lock can be upgraded to a write lock.
lazyfree_rlock_t lazyfree_read_lock(lazyfree_cache_t cache, lazyfree_key_t key);

// Check the lock is still valid.
// Needs to be used after every read from tail, to verify the page has not been dropped.
bool lazyfree_read_lock_check(lazyfree_cache_t cache, lazyfree_rlock_t lock);

// Unlock the read lock.
// If drop is true, drops the page.
void lazyfree_read_unlock(lazyfree_cache_t cache, lazyfree_rlock_t lock, bool drop);


// == Write Lock API ==
// Only one page can be locked for write at the time.
// There are two ways to get a write lock:

// Allocates a new page in the cache.
// Must not be called for existing keys, instead use upgrade.
// Returns ptr to page[0:PAGE_SIZE]
void* lazyfree_write_alloc(lazyfree_cache_t cache, lazyfree_key_t key);

// Upgrade the read lock into write lock.
// Returns ptr to page[0:PAGE_SIZE]
void* lazyfree_write_upgrade(lazyfree_cache_t cache, lazyfree_rlock_t* lock);

// Unlocks the write lock.
// If drop is true, drops the page.
void lazyfree_write_unlock(lazyfree_cache_t cache, bool drop);
```

### Fallthrough API

[fallthrough_cache.h](include/fallthrough_cache.h) provides higher-level API on top of generic implemmentation.
On miss, this cache will refill entry from ground truth.

```c
// Init Fallthrough Cache
// Takes generic lazyfree_impl and refill callback.
void ft_cache_init(ft_cache_t *cache, struct lazyfree_impl impl, 
                   size_t capacity, size_t entry_size,
                   ft_refill_t refill_cb, void *refill_opaque);

// Destroy Fallthrough Cache
void ft_cache_destroy(ft_cache_t *cache);

// Get value from cache, or repopulate
void ft_cache_get(ft_cache_t *cache, 
                  lazyfree_key_t key, 
                  uint8_t *value);

// Returns true if found, false if not found.
bool ft_cache_drop(ft_cache_t *cache, lazyfree_key_t key);
```


### Other headers

- `cache.h` - generic cache interface.
- `stub_cache.h` - stub cache implementation.
  - It never stores any pages, and claims all read lock attempts are unsuccessful.
- `hashmap.h` - taken from [hashmap.h](https://github.com/sheredom/hashmap.h).
  - Previously had [stb_ds.h](https://nothings.org/stb_ds/), but it had some issues with deletion.
  - No other public domain hashmaps could be found :(
  - Open Addressing Hashmap can probably be implemented inside the cache, for even better performance.
- `testlib.h` - includes tools to build cache tests and benchmarks.

## Implementation details

The cache consists of a fixed number of chunks (e.g. 32).
There is one persistent annonymous allocation per chunk.
Chunks are arranged in a circular order.
New page allocations happen only to the current chunk.
After the current chunk has no more free pages,  `madvise(..., MADV_FREE);` is called.
That memory can now be reclaimed by kernel at any moment.

Fortunately, this happens at page granularity, and we can detect if page was reset.
bit0 on every page is set to 1 and is used to detect page resets.
That is why the first byte is provided separately.

The locking mechanism is designed in a way to minimize hashmap lookups over the lifecycle of a key.

## Benchmarks

The aim of the benchmark is to simulate a system with unpredictable memory pressure.

The following on top of `ft_cache_t` is performed (see [./run_benchmark.sh](run_benchmark.sh)):

1. Docker container is setup with soft memory limit of `4Gb`, hard limit of `4.5Gb`
2. Two separate sets of keys are defined defined: `hot_size = 256MB`, `cold_size = 3.5Gb`
3. Hot keys are accessed through the cache `8` times, cold keys are accessed `2` times.
4. Hitrate and latency for both sets are measured on a random permutation of keys.
5. Reclaim simulation happens: benchmarks performs several allocations in total equal to `3Gb`.
   In theory this means only `1.5Gb` of memory is left for the cache.
6. Hitrate and latency are measured once again.

 Implementation | Memory usage | Disk usage | Cold latency | Cold hitrate | Reclaim latency |
----------------|--------------|------------|--------------|--------------|-----------------|
`LazyFree`      |     4Gb      |      0     |     2.9s     |     23%      |      2.3s       |
`Disk`          |     4Gb      |     4Gb    |     5.2s     |      0       |      2.2s       |
`Anon` Full     |     4Gb      |      0     |      OOM     |     OOM      |       OOM       |
`Anon` Small    |     1Gb      |      0     |     2.8s     |      6%      |      1.5s       |
`Stub`          |      0       |      0     |     0.11s    |      0%      |      1.2s       |
================|==============|============|==============|==============|=================|
What is best    | `Stub`    | Any `Anon` |`LazyFree`|Depends on reconstruction cost|Depends on disk|

Theoretical max hitrate:
`(4Gb quota - 3Gb reclaim)/(3.5G cold set size) = 28%`

Overall, `LazyFree` is quite close to the theoretical limit. This all also heavily depends on exact benchmark implementation.

<details>
<summary>Raw data</summary>

```text
~> ./run_benchmark.sh

== Report lazyfree, cache_size=4Gb, set_size=4Gb ==
hot_before_reclaim_hitrate=1.00
hot_before_reclaim_latency=62ms
cold_before_reclaim_hitrate=1.00
cold_before_reclaim_latency=918ms   
reclaim_latency=2309.12ms           
hot_after_reclaim_hitrate=0.00
hot_after_reclaim_latency=285ms
cold_after_reclaim_hitrate=0.23     <---- or   
cold_after_reclaim_latency=2981ms   <----

== Report disk, cache_size=4Gb, set_size=4Gb ==
hot_before_reclaim_hitrate=1.00
hot_before_reclaim_latency=43ms
cold_before_reclaim_hitrate=1.00
cold_before_reclaim_latency=665ms
reclaim_latency=2266.80ms           <--- need slower disk!
hot_after_reclaim_hitrate=1.00
hot_after_reclaim_latency=415ms     
cold_after_reclaim_hitrate=1.00     <--- 
cold_after_reclaim_latency=5281ms   <--- Paging in more expensive than reconstrictuion

== Report anon, cache_size=1Gb, set_size=4Gb == 
hot_before_reclaim_hitrate=0.00
hot_before_reclaim_latency=209ms
cold_before_reclaim_hitrate=0.06    <--- Cache has to be very small
cold_before_reclaim_latency=2804ms
reclaim_latency=1512.10ms
hot_after_reclaim_hitrate=0.00
hot_after_reclaim_latency=221ms
cold_after_reclaim_hitrate=0.06
cold_after_reclaim_latency=2836ms

== Report stub, cache_size=4Gb, set_size=4Gb ==
hot_before_reclaim_hitrate=0.00
hot_before_reclaim_latency=5ms
cold_before_reclaim_hitrate=0.00
cold_before_reclaim_latency=85ms
reclaim_latency=1204.77ms       <--- Represents the testing overhead
hot_after_reclaim_hitrate=0.00
hot_after_reclaim_latency=7ms
cold_after_reclaim_hitrate=0.00
cold_after_reclaim_latency=114ms
```

</details>

There are also regression tests via `./run_tests.sh`.

## Similar technologies

- Would be achivable with inside kernel with it's memory primitives. Having it in userspace is more convienient.
- Chromium has [discardable memory](https://chromium.googlesource.com/chromium/src/%2B/main/docs/memory-infra/probe-cc.md?utm_source=chatgpt.com#Discardable-Category).
- [Purgable](https://github.com/skeeto/purgeable) does mmap on every allocation - too slow.

## Possible application

Suppose there is a [disaggregated storage](https://en.wikipedia.org/wiki/Disaggregated_storage) system.
The "compute" part of it operates on a relatively tight working set.
That working set can be stored in the cache.
On cache miss, it can easily be repopulated from the storage over the low-latency network.

Using LazyCache might be preferable in order to:

- Avoid the need for node to have persistent storage.
- Don't spend latency on disk IO when cheaper reconstruction is avaliable.

## Futher improvements

1. Right now, the eviction policy is very simple: it evicts random chunk.
   - Perhaps, LFU on chunks is easy enough to implement.
   - It is also possible to have large overcommitment on memory, such that kernel actively pages it out.
   - If the two above are combined, and the eviction policy is similar to kernel's,
     it might be possible to ever reuse only evicted memory.
     Thus, all evictions would be guided by memory pressure.
2. Already has RWLock semantics, can have multiple writers if writing to different chunks.
3. Fallthrough cache should be able to pack multiple entries into a single page. They would be evicted together.
4. Should have mesurements with different reclaim sizes.
