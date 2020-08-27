# Model checking mimalloc with GenMC

## Current issues

### Race on `next` field 

#### Reproduce  
`./test.sh test/race_next/one_thread --bug 1`  
or  
`./test.sh test/race_next/n_threads --bug 1`  

#### Description 

Data race on `block->next` field because [cas in mimalloc](https://github.com/microsoft/mimalloc/blob/71160e2bac443c0dd35c7ee13993466efcee57b2/include/mimalloc-atomic.h#L204) has only release memory order.  

ThreadSanitizer also finds this.

### Race on `_mi_numa_node_count` variable

#### Reproduce  

`./test.sh test/alloc_free_mt --bug 1`  

#### Description

`mi_numa_node_count` serves as a cache for number of numa nodes. Is is [lazy-initialized](https://github.com/microsoft/mimalloc/blob/ef8e5d18a65f653bbef9cf57694aff37d2e85b9d/src/os.c#L1132). But cached value accessed by many threads, which leads to race between initialization / accessing. 

As for now stress-tests in mimalloc under ThreadSanitizer don't find this issue.

### Too weak ordering for `region->info` read

#### Reproduce

`./test.sh test/alloc_free_mt --bug 2`  
or  
`./test.sh test/bitmap --bug 1` 

#### Description

This is a tricky one. It's not a race, it's an assertion violation. 

[Assertion](https://github.com/microsoft/mimalloc/blob/ef8e5d18a65f653bbef9cf57694aff37d2e85b9d/src/region.c#L280) fails after region was successfully claimed. 
Assertion checks that bits in a bitmap responsible for the region are claimed. 

[test/bitmap](test/bitmap) contains refined version without allocator itself. Bug is easier to understand with this version.

**Bug description:**

Suppose there are T1 and T2, both allocating memory from mimalloc.
  
Thread T1:
1. T1 is the first one, so it allocates region from OS.  
2. T1 initializes region descriptor (`regions[idx]`). In this descriptor, T1 sets bitmap to 0 (with `relaxed` memory order).  
3. T1 publishes this region through the `release` store to `region->value`.  

Thread T2:
1. T2 finds T1's region and reads `region->value` with `relaxed` order to check if this region is ready.  
2. T2 decides region is ready, and claimes bits in it via `acq_req/acq` CAS.  
3. T2 checks if bits in a bitmap are claimed - it reads bitmap with `relaxed` memory order. And here assertion fails, because this read is allowed to read not only from CAS, but also from T1's store to this bitmap!

**Fix:** make `region->value` read with `acquire` memory order instead of `relaxed`. This will create _synchronizes-with_ edge between threads, and will forbid T2 to read "stale" value, because store of this value will be before T2's store (via CAS) in happens-before order.

Code for [CppMem](http://svr-pes20-cppmem.cl.cam.ac.uk/cppmem/) (emulates CAS with load/store, which in this particular case is ok):
```cpp
int main() {
  atomic_int bitmap = 0; 
  atomic_int info = 0;
  {{{ { bitmap.store(0, memory_order_release);
        info.store(1, memory_order_release); }
  ||| { info.load(memory_order_relaxed).readsvalue(1);
        bitmap.load(memory_order_acquire).readsvalue(0);
        bitmap.store(1, memory_order_release);
        bitmap.load(memory_order_relaxed).readsvalue(0); }
  }}};
  return 0; }
```
Memory relations from CppMem:  
<img src="https://i.ibb.co/TvMxjVD/Screenshot-2020-08-27-at-17-23-00.png" width="300"/>

## Potential issues

- Non-trivial algorithm in multithread freeing (using `delayed` flags) due to full pages logic.

- Abandoned pages / segments

- Concurrent bitmap, fixed ABA
