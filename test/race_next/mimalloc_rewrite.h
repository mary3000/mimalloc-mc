#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////

#include "mimalloc-custom/mimalloc.h"
#include "mimalloc-custom/mimalloc-internal.h"
#include "mimalloc-custom/mimalloc-types.h"

void __VERIFIER_assume(int);

void _mi_assert_fail(const char* assertion, const char* fname, unsigned line, const char* func) {
  printf("mimalloc: assertion failed: at \"%s\":%u, %s\n  assertion: \"%s\"\n",
         fname,
         line,
         (func == NULL ? "" : func),
         assertion);
  assert(0);
}

void _genmc_free_block_mt(mi_page_t* page, mi_block_t* block) {
  // Try to put the block on either the page-local thread free list, or the heap delayed free list.
  mi_thread_free_t tfree;
  mi_thread_free_t tfreex;
  bool use_delayed;
  do {
    tfree = mi_atomic_read_relaxed(&page->xthread_free);
    use_delayed = (mi_tf_delayed(tfree) == MI_USE_DELAYED_FREE);
    if (mi_unlikely(use_delayed)) {
      genmc_log("use delayed\n");
      // unlikely: this only happens on the first concurrent free in a page that is in the full list
      tfreex = mi_tf_set_delayed(tfree, MI_DELAYED_FREEING);
    } else {
      genmc_log("use thread_free\n");
      // usual: directly add to page thread_free list
      //mi_block_set_next(page, block, mi_tf_block(tfree));
      block->next = (mi_encoded_t) mi_tf_block(tfree);
      tfreex = mi_tf_set_block(tfree, block);
    }
  } while (!mi_atomic_cas_weak(&page->xthread_free, tfreex, tfree));

  if (mi_unlikely(use_delayed)) {
    // racy read on `heap`, but ok because MI_DELAYED_FREEING is set (see `mi_heap_delete` and `mi_heap_collect_abandon`)
    mi_heap_t* const heap = mi_page_heap(page);
        mi_assert_internal(heap != NULL);
    if (heap != NULL) {
      // add to the delayed free list of this heap. (do this atomically as the lock only protects heap memory validity)
      mi_block_t* dfree;
      do {
        dfree = mi_atomic_read_ptr_relaxed(mi_block_t, &heap->thread_delayed_free);
        //mi_block_set_nextx(heap,block,dfree, heap->keys);
        block->next = (mi_encoded_t) dfree;
      } while (!mi_atomic_cas_ptr_weak(mi_block_t, &heap->thread_delayed_free, block, dfree));
      genmc_log("thread_delayed_free put, heap = %p\n", heap);
    }

    // and reset the MI_DELAYED_FREEING flag
    do {
      tfreex = tfree = mi_atomic_read_relaxed(&page->xthread_free);
          mi_assert_internal(mi_tf_delayed(tfree) == MI_DELAYED_FREEING);
      tfreex = mi_tf_set_delayed(tfree, MI_NO_DELAYED_FREE);
    } while (!mi_atomic_cas_weak(&page->xthread_free, tfreex, tfree));
  }

  genmc_log("DONE!\n\n");
}

void genmc_page_free_list_extend(mi_page_t* const page, void* page_area, const size_t bsize, const size_t extend) {
#if (MI_SECURE <= 2)
      mi_assert_internal(page->free == NULL);
      mi_assert_internal(page->local_free == NULL);
#endif
      mi_assert_internal(page->capacity + extend <= page->reserved);
      mi_assert_internal(bsize == mi_page_block_size(page));

  mi_block_t* const start = page_area;

  // initialize a sequential free list
  mi_block_t* const last = page_area + bsize * (extend - 1);
  mi_block_t* block = start;
  while (block <= last) {
    mi_block_t* next = (mi_block_t*) ((uint8_t*) block + bsize);
    //mi_block_set_next(page,block,next);
    block->next = (mi_encoded_t) next;
    block = next;
  }
  // prepend to free list (usually `NULL`)
  //mi_block_set_next(page, last, page->free);
  last->next = (mi_encoded_t) page->free;
  page->free = start;

  page->capacity += (uint16_t) extend;
}

mi_page_t* genmc_find_page(mi_heap_t* heap, size_t size) {
  _mi_page_free_collect(heap->pages_free_direct[0], false);
  return heap->pages_free_direct[0];
}

void* _genmc_malloc_generic(mi_heap_t* heap, size_t size) mi_attr_noexcept;

extern inline void* _genmc_page_malloc(mi_heap_t* heap, mi_page_t* page, size_t size) mi_attr_noexcept {
      mi_assert_internal(page->xblock_size == 0 || mi_page_block_size(page) >= size);
  mi_block_t* block = page->free;
  if (mi_unlikely(block == NULL)) {
    return _genmc_malloc_generic(heap, size);
  }
      mi_assert_internal(block != NULL/* && _mi_ptr_page(block) == page*/);
  // pop from the free list
  page->free = mi_block_next(page, block);
  page->used++;
  //mi_assert_internal(page->free == NULL || _mi_ptr_page(page->free) == page);
#if (MI_DEBUG > 0)
  //if (!page->is_zero) { memset(block, MI_DEBUG_UNINIT, size); }
#elif (MI_SECURE != 0)
  block->next = 0;  // don't leak internal data
#endif
#if (MI_STAT > 1)
  const size_t bsize = mi_page_usable_block_size(page);
  if (bsize <= MI_LARGE_OBJ_SIZE_MAX) {
      const size_t bin = _mi_bin(bsize);
      mi_heap_stat_increase(heap, normal[bin], 1);
  }
#endif
#if (MI_PADDING > 0) && defined(MI_ENCODE_FREELIST)
  mi_padding_t* const padding = (mi_padding_t*)((uint8_t*)block + mi_page_usable_block_size(page));
ptrdiff_t delta = ((uint8_t*)padding - (uint8_t*)block - (size - MI_PADDING_SIZE));
mi_assert_internal(delta >= 0 && mi_page_usable_block_size(page) >= (size - MI_PADDING_SIZE + delta));
padding->canary = (uint32_t)(mi_ptr_encode(page,block,page->keys));
padding->delta  = (uint32_t)(delta);
uint8_t* fill = (uint8_t*)padding - delta;
const size_t maxpad = (delta > MI_MAX_ALIGN_SIZE ? MI_MAX_ALIGN_SIZE : delta); // set at most N initial padding bytes
for (size_t i = 0; i < maxpad; i++) { fill[i] = MI_DEBUG_PADDING; }
#endif
  return block;
}

// regular free
static inline void _genmc_free_block(mi_page_t* page, bool local, mi_block_t* block) {
  // and push it on the free list
  if (mi_likely(local)) {
    // owning thread can free a block directly
    //if (mi_unlikely(mi_check_is_double_free(page, block))) return;
    //mi_check_padding(page, block);

    mi_block_set_next(page, block, page->local_free);
    page->local_free = block;
    page->used--;

    genmc_log("local_free %p\n", page->local_free);

    /*if (mi_unlikely(mi_page_all_free(page))) {
        _mi_page_retire(page);
    }*/

    /*else if (mi_unlikely(mi_page_is_in_full(page))) { // todo: integrate into test
        _mi_page_unfull(page);
    }*/
  } else {
    _genmc_free_block_mt(page, block);
  }
}

void _mi_page_use_delayed_free(mi_page_t* page, mi_delayed_t delay, bool override_never) {
  mi_thread_free_t tfree;
  mi_thread_free_t tfreex;
  mi_delayed_t old_delay;
  do {
    tfree = mi_atomic_read(&page->xthread_free);  // note: must acquire as we can break this loop and not do a CAS
    tfreex = mi_tf_set_delayed(tfree, delay);
    old_delay = mi_tf_delayed(tfree);
    if (mi_unlikely(old_delay == MI_DELAYED_FREEING)) {
      mi_atomic_yield(); // delay until outstanding MI_DELAYED_FREEING are done.
      // tfree = mi_tf_set_delayed(tfree, MI_NO_DELAYED_FREE); // will cause CAS to busy fail
    } else if (delay == old_delay) {
      break; // avoid atomic operation if already equal
    } else if (!override_never && old_delay == MI_NEVER_DELAYED_FREE) {
      break; // leave never-delayed flag set
    }
  } while ((old_delay == MI_DELAYED_FREEING) ||
      !mi_atomic_cas_weak(&page->xthread_free, tfreex, tfree));
}

// Collect the local `thread_free` list using an atomic exchange.
// Note: The exchange must be done atomically as this is used right after
// moving to the full list in `mi_page_collect_ex` and we need to
// ensure that there was no race where the page became unfull just before the move.
static void _mi_page_thread_free_collect(mi_page_t* page) {
  mi_block_t* head;
  mi_thread_free_t tfree;
  mi_thread_free_t tfreex;
  do {
    tfree = mi_atomic_read_relaxed(&page->xthread_free);
    head = mi_tf_block(tfree);
    tfreex = mi_tf_set_block(tfree, NULL);
  } while (!mi_atomic_cas_weak(&page->xthread_free, tfreex, tfree));

  // return if the list is empty
  if (head == NULL) return;

  // find the tail -- also to get a proper count (without data races)
  uint32_t max_count = page->capacity; // cannot collect more than capacity
  uint32_t count = 1;
  mi_block_t* tail = head;
  mi_block_t* next;
  while ((next = mi_block_next(page, tail)) != NULL && count <= max_count) {
    count++;
    tail = next;
  }
  // if `count > max_count` there was a memory corruption (possibly infinite list due to double multi-threaded free)
  //mi_assert_internal(count > max_count);
  if (count > max_count) {
    _mi_error_message(EFAULT, "corrupted thread-free list\n");
    return; // the thread-free items cannot be freed
  }

  // and append the current local free list
  mi_block_set_next(page, tail, page->local_free);
  page->local_free = head;
  genmc_log("local_free\n");

  // update counts now
  page->used -= count;
}

void _mi_page_free_collect(mi_page_t* page, bool force) {
      mi_assert_internal(page != NULL);
  genmc_log("_mi_page_free_collect\n");


  // collect the thread free list
  if (force || mi_page_thread_free(page) != NULL) {  // quick test to avoid an atomic operation
    genmc_log("not free\n");
    _mi_page_thread_free_collect(page);
  }

  genmc_log("page->local_free = %p\n", page->local_free);

  // and the local free list
  if (page->local_free != NULL) {
    if (mi_likely(page->free == NULL)) {
      // usual case
      genmc_log("localfree to free\n");
      page->free = page->local_free;
      page->local_free = NULL;
      page->is_zero = false;
    } else if (force) {
      // append -- only on shutdown (force) as this is a linear operation
      mi_block_t* tail = page->local_free;
      mi_block_t* next;
      while ((next = mi_block_next(page, tail)) != NULL) {
        tail = next;
      }
      mi_block_set_next(page, tail, page->free);
      page->free = page->local_free;
      page->local_free = NULL;
      page->is_zero = false;
    }
  }

      mi_assert_internal(!force || page->local_free == NULL);
}

bool _genmc_free_delayed_block(mi_page_t* page, mi_block_t* block) {

  // Clear the no-delayed flag so delayed freeing is used again for this page.
  // This must be done before collecting the free lists on this page -- otherwise
  // some blocks may end up in the page `thread_free` list with no blocks in the
  // heap `thread_delayed_free` list which may cause the page to be never freed!
  // (it would only be freed if we happen to scan it in `mi_page_queue_find_free_ex`)
  _mi_page_use_delayed_free(page, MI_USE_DELAYED_FREE, false /* dont overwrite never delayed */);

  // collect all other non-local frees to ensure up-to-date `used` count
  _mi_page_free_collect(page, false);

  // and free the block (possibly freeing the page as well since used is updated)
  _genmc_free_block(page, true, block);
  return true;
}

void _mi_heap_delayed_free(mi_heap_t* heap) {
  // take over the list (note: no atomic exchange is it is often NULL)
  mi_block_t* block;
  do {
    block = mi_atomic_read_ptr_relaxed(mi_block_t, &heap->thread_delayed_free);
  } while (block != NULL && !mi_atomic_cas_ptr_weak(mi_block_t, &heap->thread_delayed_free, NULL, block));

  genmc_log("grab thread_delayed_free, heap = %p, block = %p\n", heap, block);

  // and free them all
  while (block != NULL) {
    //mi_block_t* next = mi_block_nextx(heap,block, heap->keys);
    mi_block_t* next = (mi_block_t*) block->next;
    // use internal free instead of regular one to keep stats etc correct
    genmc_log("_genmc_free_delayed_block\n");
    if (!_genmc_free_delayed_block(heap->pages_free_direct[0], block)) {
      // we might already start delayed freeing while another thread has not yet
      // reset the delayed_freeing flag; in that case delay it further by reinserting.
      mi_block_t* dfree;
      do {
        dfree = mi_atomic_read_ptr_relaxed(mi_block_t, &heap->thread_delayed_free);
        //mi_block_set_nextx(heap, block, dfree, heap->keys);
        block->next = (mi_encoded_t) next;
      } while (!mi_atomic_cas_ptr_weak(mi_block_t, &heap->thread_delayed_free, block, dfree));
    }
    block = next;
  }
}

// Generic allocation routine if the fast path (`alloc.c:mi_page_malloc`) does not succeed.
// Note: in debug mode the size includes MI_PADDING_SIZE and might have overflowed.
void* _genmc_malloc_generic(mi_heap_t* heap, size_t size) mi_attr_noexcept
{
      mi_assert_internal(heap != NULL);

  // free delayed frees from other threads
  _mi_heap_delayed_free(heap);

  // find (or allocate) a page of the right size
  mi_page_t* page = genmc_find_page(heap, size);
  assert(page != NULL);
  if (mi_unlikely(page == NULL)) { // first time out of memory, try to collect and retry the allocation once more
    mi_heap_collect(heap, true /* force */);
    page = genmc_find_page(heap, size);
  }

  mi_assert_internal(page != NULL);
  if (mi_unlikely(page == NULL)) { // out of memory
    _mi_error_message(ENOMEM, "cannot allocate memory (%zu bytes requested)\n", size);
    return NULL;
  }

  genmc_log("here %p\n", page->free);
  //mi_assert_internal(mi_page_immediate_available(page));
  __VERIFIER_assume(page->free != NULL);
  genmc_log("here 2\n");

      mi_assert_internal(mi_page_block_size(page) >= size);

  // and try again, this time succeeding! (i.e. this should never recurse)
  return _genmc_page_malloc(heap, page, size);
}
