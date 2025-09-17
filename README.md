# LazyFree cache

LazyFree cache is an implementation of 4KB page cache on top of discardable memory.

Discardable memory provides the Linux Kernel a way to reclaim some mmaped pages for other use in the system.
This works similarly to file-backed memory, but it saves cold pages to `/dev/null`.

Provides Zero-Copy APIs and minimizes hashtable lookups to the theoretical minimum.

Implementation is based on `MADV_FREE` ([madvise(2)](https://man7.org/linux/man-pages/man2/madvise.2.html)) flag.
It is avaliable since Linux 4.5.

The semantics of the range under `MADV_FREE` is:

- If the page was not evicted, all reads from the page return page data.
- If the page was evicted, all reads from the page return zeros.

This implementation can be useful if running on a system with occasional unpredictable memory pressure, and there is a way
to reconstruct the page data from the ground truth.

## LazyFree API

[lazyfree_cache.h](include/lazyfree_cache.h) provides default LazyFree API.

```c
// PAGE_SIZE must be equal to kernel page size.
#define PAGE_SIZE 4096

typedef struct lazyfree_cache* lazyfree_cache_t;
lazyfree_cache_t lazyfree_cache_new(size_t capacity_bytes);
void lazyfree_cache_free(lazyfree_cache_t cache);

// Key type
typedef uint64_t lazyfree_key_t;

// ================================ Read API ================================
// This is elaborate API, but it is necessary to support zero-copy reads.

typedef struct {
    const volatile uint8_t *head; // [0:PAGE_SIZE-1]
    uint8_t tail;                 // last byte of the page
} lazyfree_rlock_t;  

// LAZYFREE_LOCK_CHECK returns if lock is still valid.
// Must be called after reading the payload, to verify the page has not been dropped.
#define LAZYFREE_LOCK_CHECK(lock) ((lock).head[PAGE_SIZE-1] > 0)

// lazyfree_read is a helper to safely read from the lock.
// Returns true if successful.
static inline bool lazyfree_read(void *dest, lazyfree_rlock_t lock, size_t offset, size_t size);

// Take an optimistic read lock.
lazyfree_rlock_t lazyfree_read_lock(lazyfree_cache_t cache, lazyfree_key_t key);

// If drop is true, drops the page.
void lazyfree_read_unlock(lazyfree_cache_t cache, lazyfree_rlock_t lock, bool drop);

// ================================ Write API ================================
// Only one page can be locked for write at the time.
// There are two ways to get a write lock:

// Can be called only for new keys.
void* lazyfree_write_alloc(lazyfree_cache_t cache, lazyfree_key_t key);

// Upgrade the read lock into write lock.
// LAZYFREE_LOCK_CHECK(lock) can be used to verify if the page still has the same data.
void* lazyfree_write_upgrade(lazyfree_cache_t cache, lazyfree_rlock_t* lock);

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

The cache consists of a fixed number of chunks (e.g. 32), chunks are arranged in a circular order.
There is one persistent anonymous allocation per chunk.
New page allocations happen only to the current chunk.
After the current chunk has no more free pages,  `madvise(..., MADV_FREE);` is called, and the next chunk becomes current.
That memory can now be reclaimed by kernel at any moment.

Fortunately, this happens at page granularity, and we can detect if page was reset.
bit0 on every page is set to 1 and is used to detect page resets.
That is why the first byte is provided separately.

Also, the cache performes eviction if there are no free pages left.
If it happens to the key, the lock_check will return false as well.

The locking mechanism is designed in such way to minimize hashmap lookups over the lifecycle of a key.

Note that ABA is not possible, since locking the same page twice is not allowed.

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

 Implementation | Memory usage | Disk usage | Hot/Cold hitrate | Hot latency | Cold latency |
----------------|--------------|------------|------------------|-------------|--------------|
`LazyFree`      |     4Gb      |      0     |    45% / 23%     |    72ms     |     2.0s     |
`Disk`          |     4Gb      |     4Gb    |   100% / 100%    |   428ms     |     4.7s     |
`Anon` Full     |     4Gb      |      0     |      OOM         |     OOM     |     OOM      |
`Anon` Small    |     1Gb      |      0     |   0% / 8%        |   125ms     |     1.8s     |
`Stub`          |      0       |      0     |    0% / 0%       |     1ms     |      0%      |
----------------|--------------|------------|------------------|-------------|--------------|
Best option     | Not `Stub`!| Not `Disk` | Depends on refill vs disk costs | `LazyFree` | `LazyFree` or resizable `Anon` |

Theoretical max hitrate:
`(4Gb quota - 3Gb reclaim)/(3.5G cold set size) = 28%`

Overall, `LazyFree` is quite close to the theoretical limit. This all also heavily depends on exact benchmark implementation.

<details>
<summary>Raw data</summary>

```text
~> ./run_benchmark.sh

== Report lazyfree, capacity=4Gb, reclaim=3Gb ==
hot_before_reclaim_hitrate=1.00
hot_before_reclaim_latency=23ms
cold_before_reclaim_hitrate=1.00
cold_before_reclaim_latency=484ms
reclaim_latency=2078.05ms           
hot_after_reclaim_hitrate=0.45      <---- Higher than cold
hot_after_reclaim_latency=72ms      
cold_after_reclaim_hitrate=0.23     <---- 
cold_after_reclaim_latency=2071ms   

== Report disk, capacity=4Gb, reclaim=3Gb ==
hot_before_reclaim_hitrate=1.00
hot_before_reclaim_latency=24ms
cold_before_reclaim_hitrate=0.00
cold_before_reclaim_latency=9924ms
reclaim_latency=2112.23ms           <--- This would be worse on slow disk 
hot_after_reclaim_hitrate=1.00
hot_after_reclaim_latency=428ms     <--- Much more than lazyfreee
cold_after_reclaim_hitrate=1.00
cold_after_reclaim_latency=4740ms   <--- Paging to disk is more expensive than reconstruction!

== Report anon, capacity=1Gb, reclaim=3Gb ==
hot_before_reclaim_hitrate=1.00     <--- Kernel managed to detect hot set!
hot_before_reclaim_latency=18ms
cold_before_reclaim_hitrate=0.00 
cold_before_reclaim_latency=2106ms
reclaim_latency=1420.27ms
hot_after_reclaim_hitrate=0.00
hot_after_reclaim_latency=125ms
cold_after_reclaim_hitrate=0.08     <--- Cache has to be very small
cold_after_reclaim_latency=1889ms   <--- Unaffected by reclaim

== Report stub, capacity=4Gb, reclaim=3Gb ==
hot_before_reclaim_hitrate=0.00
hot_before_reclaim_latency=1ms
cold_before_reclaim_hitrate=0.00
cold_before_reclaim_latency=19ms
reclaim_latency=1349.56ms           <--- Represents the testing overhead
hot_after_reclaim_hitrate=0.00
hot_after_reclaim_latency=1ms
cold_after_reclaim_hitrate=0.00
cold_after_reclaim_latency=19ms
```

</details>

There are also regression tests via `./run_tests.sh`.

## Similar technologies

- Would be achievable with inside kernel with it's memory primitives. Having it in userspace is more convenient.
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

## Further improvements

1. Right now, the eviction policy is very simple: it evicts random chunk.
   - Perhaps, LFU on chunks is easy enough to implement.
   - It is also possible to have large overcommitment on memory, such that kernel actively pages it out.
   - If the two above are combined, and the eviction policy is similar to kernel's,
     it might be possible to ever reuse only evicted memory.
     Thus, all evictions would be guided by memory pressure.
2. Already can be used under RWLock, multiple writers can be supported.
3. Fallthrough cache should be able to pack multiple entries into a single page. They would be evicted together.
4. Should have measurements with different reclaim sizes.
5. Right now, disposable allocations are made with `mmap(..., MAP_NORESERVE)`.
   Perhaps it would be possible to mix `MADV_FREE` with using swap to get even better flexibility

Other than that, the library is ready to be integrated into other projects,
subject to more correctness verification and performance measurements.

## License

This software is available under 2 licenses -- choose whichever you prefer:

- The Unlicense (see [LICENSE](LICENSE))
- MIT License (see [LICENSE2](LICENSE2))
