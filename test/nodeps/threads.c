
#include <stddef.h>

#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>
#include <stdatomic.h>

#include <assert.h>

void __VERIFIER_assume(int);

///////////////////////////////////////////////////////////////////////////////////////////////////

#include "mimalloc.h"
#include "mimalloc-internal.h"
#include "mimalloc-types.h"

void _mi_assert_fail(const char* assertion, const char* fname, unsigned line, const char* func ) {
    printf("mimalloc: assertion failed: at \"%s\":%u, %s\n  assertion: \"%s\"\n", fname, line, (func==NULL?"":func), assertion);
    assert(0);
}

//#include "static.c"

void _genmc_free_block_mt(mi_page_t* page, mi_block_t* block)
{
    // Try to put the block on either the page-local thread free list, or the heap delayed free list.
    mi_thread_free_t tfree;
    mi_thread_free_t tfreex;
    bool use_delayed;
    do {
        tfree = mi_atomic_read_relaxed(&page->xthread_free);
        use_delayed = (mi_tf_delayed(tfree) == MI_USE_DELAYED_FREE);
        if (mi_unlikely(use_delayed)) {
            printf("use delayed\n");
            // unlikely: this only happens on the first concurrent free in a page that is in the full list
            tfreex = mi_tf_set_delayed(tfree,MI_DELAYED_FREEING);
        }
        else {
            printf("use thread_free\n");
            // usual: directly add to page thread_free list
            //mi_block_set_next(page, block, mi_tf_block(tfree));
            block->next = (mi_encoded_t)mi_tf_block(tfree);
            tfreex = mi_tf_set_block(tfree,block);
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
                dfree = mi_atomic_read_ptr_relaxed(mi_block_t,&heap->thread_delayed_free);
                //mi_block_set_nextx(heap,block,dfree, heap->keys);
                block->next = (mi_encoded_t)dfree;
            } while (!mi_atomic_cas_ptr_weak(mi_block_t,&heap->thread_delayed_free, block, dfree));
        }

        // and reset the MI_DELAYED_FREEING flag
        do {
            tfreex = tfree = mi_atomic_read_relaxed(&page->xthread_free);
                    mi_assert_internal(mi_tf_delayed(tfree) == MI_DELAYED_FREEING);
            tfreex = mi_tf_set_delayed(tfree,MI_NO_DELAYED_FREE);
        } while (!mi_atomic_cas_weak(&page->xthread_free, tfreex, tfree));
    }

    printf("DONE!\n");
}

void genmc_page_free_list_extend(mi_page_t* const page, void* page_area, const size_t bsize, const size_t extend)
{
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
    while(block <= last) {
        mi_block_t* next = (mi_block_t*)((uint8_t*)block + bsize);
        //mi_block_set_next(page,block,next);
        block->next = (mi_encoded_t) next;
        block = next;
    }
    // prepend to free list (usually `NULL`)
    //mi_block_set_next(page, last, page->free);
    last->next = (mi_encoded_t) page->free;
    page->free = start;
}

mi_page_t* genmc_find_page(mi_heap_t* heap, size_t size) {
    return heap->pages_free_direct[0];
}

void* _genmc_malloc_generic(mi_heap_t* heap, size_t size) mi_attr_noexcept;

extern inline void* _genmc_page_malloc(mi_heap_t* heap, mi_page_t* page, size_t size) mi_attr_noexcept {
    mi_assert_internal(page->xblock_size==0||mi_page_block_size(page) >= size);
    mi_block_t* block = page->free;
    if (mi_unlikely(block == NULL)) {
        return _genmc_malloc_generic(heap, size);
    }
    mi_assert_internal(block != NULL && _mi_ptr_page(block) == page);
    // pop from the free list
    page->free = mi_block_next(page, block);
    page->used++;
    mi_assert_internal(page->free == NULL || _mi_ptr_page(page->free) == page);
#if (MI_DEBUG>0)
    //if (!page->is_zero) { memset(block, MI_DEBUG_UNINIT, size); }
#elif (MI_SECURE!=0)
    block->next = 0;  // don't leak internal data
#endif
#if (MI_STAT>1)
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
static inline void _genmc_free_block(mi_page_t* page, bool local, mi_block_t* block)
{
    // and push it on the free list
    if (mi_likely(local)) {
        // owning thread can free a block directly
        //if (mi_unlikely(mi_check_is_double_free(page, block))) return;
        //mi_check_padding(page, block);

        mi_block_set_next(page, block, page->local_free);
        page->local_free = block;
        page->used--;

        /*if (mi_unlikely(mi_page_all_free(page))) {
            _mi_page_retire(page);
        }*/

        /*else if (mi_unlikely(mi_page_is_in_full(page))) { // todo: integrate into test
            _mi_page_unfull(page);
        }*/
    }
    else {
        _genmc_free_block_mt(page,block);
    }
}

void _mi_page_use_delayed_free(mi_page_t* page, mi_delayed_t delay, bool override_never) {
    mi_thread_free_t tfree;
    mi_thread_free_t tfreex;
    mi_delayed_t     old_delay;
    do {
        tfree = mi_atomic_read(&page->xthread_free);  // note: must acquire as we can break this loop and not do a CAS
        tfreex = mi_tf_set_delayed(tfree, delay);
        old_delay = mi_tf_delayed(tfree);
        if (mi_unlikely(old_delay == MI_DELAYED_FREEING)) {
            mi_atomic_yield(); // delay until outstanding MI_DELAYED_FREEING are done.
            // tfree = mi_tf_set_delayed(tfree, MI_NO_DELAYED_FREE); // will cause CAS to busy fail
        }
        else if (delay == old_delay) {
            break; // avoid atomic operation if already equal
        }
        else if (!override_never && old_delay == MI_NEVER_DELAYED_FREE) {
            break; // leave never-delayed flag set
        }
    } while ((old_delay == MI_DELAYED_FREEING) ||
             !mi_atomic_cas_weak(&page->xthread_free, tfreex, tfree));
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
        block = mi_atomic_read_ptr_relaxed(mi_block_t,&heap->thread_delayed_free);
    } while (block != NULL && !mi_atomic_cas_ptr_weak(mi_block_t,&heap->thread_delayed_free, NULL, block));

    // and free them all
    while(block != NULL) {
        //mi_block_t* next = mi_block_nextx(heap,block, heap->keys);
        mi_block_t* next = (mi_block_t*)block->next;
        // use internal free instead of regular one to keep stats etc correct
        if (!_genmc_free_delayed_block(heap->pages_free_direct[0], block)) {
            // we might already start delayed freeing while another thread has not yet
            // reset the delayed_freeing flag; in that case delay it further by reinserting.
            mi_block_t* dfree;
            do {
                dfree = mi_atomic_read_ptr_relaxed(mi_block_t,&heap->thread_delayed_free);
                //mi_block_set_nextx(heap, block, dfree, heap->keys);
                block->next = (mi_encoded_t)next;
            } while (!mi_atomic_cas_ptr_weak(mi_block_t,&heap->thread_delayed_free, block, dfree));
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
    if (mi_unlikely(page == NULL)) { // first time out of memory, try to collect and retry the allocation once more
        mi_heap_collect(heap, true /* force */);
        page = genmc_find_page(heap, size);
    }

    if (mi_unlikely(page == NULL)) { // out of memory
        _mi_error_message(ENOMEM, "cannot allocate memory (%zu bytes requested)\n", size);
        return NULL;
    }

    //mi_assert_internal(mi_page_immediate_available(page));
    __VERIFIER_assume(page->free != NULL);

    mi_assert_internal(mi_page_block_size(page) >= size);

    // and try again, this time succeeding! (i.e. this should never recurse)
    return _genmc_page_malloc(heap, page, size);
}

void* os_alloc(size_t size) {
    char* data = malloc(size);
    printf("malloc(%zu) = [%p, %p)\n", size, data, data + size);
    /*for (size_t i = 0; i < size; ++i) {
        data[i] = 0;
    }*/
    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

typedef _Atomic (mi_block_t*) atomic_block;
atomic_block list[2] = {NULL, NULL};

mi_heap_t heaps[2];
mi_page_t pages[2];

void init_page(mi_page_t* page, mi_heap_t* heap, size_t reserved, uint32_t bsize) {
    page->free = NULL;
    page->capacity = 0;
    page->reserved = reserved;
    page->xblock_size = bsize;


    heap->pages_free_direct[0] = page;
    atomic_store_explicit(&heap->thread_delayed_free, NULL, memory_order_release);

    atomic_store_explicit(&page->xheap, (uintptr_t) heap, memory_order_release);
}

void *routine(void *arg)
{
    int tid = (int)pthread_self() - 1;
    int other_tid = (tid + 1) % 2;

    mi_heap_t* heap = &heaps[tid];
    mi_page_t* page = &pages[tid];

    size_t bsize = (1 << 5);
    size_t bnum = 1;
    init_page(page, heap, bnum, bsize);


    char* data = os_alloc(bsize * bnum);


    genmc_page_free_list_extend(page, data, bsize, bnum);


    mi_block_t* block = page->free;
    printf("block: 0x%p\n", block);
    size_t cnt = 0;
    while (block != NULL) {
        ++cnt;
        block = (mi_block_t *) block->next;
    }
    assert(cnt == bnum);

    atomic_store_explicit(&list[tid], page->free, memory_order_release);
    page->free = NULL;

    mi_block_t* other_block = atomic_load_explicit(&list[other_tid], memory_order_acquire);
    mi_page_t* other_page = &pages[other_tid];

    __VERIFIER_assume(other_block != NULL);
    assert(other_block != NULL);

    printf("other block = %p\n", other_block);

    while (other_block != NULL) {
        mi_block_t* next = (mi_block_t *) other_block->next;
        _genmc_free_block_mt(other_page, other_block);
        other_block = next;
    }

    size_t allocated = 0;
    while (allocated < bnum) {
        if (_genmc_page_malloc(heap, page, bsize)) {
            ++allocated;
        }
        break;
    }

    return NULL;
}

void *thread1(void *arg)
{
    return routine(arg);
}

void *thread2(void *arg)
{
    return routine(arg);
}

int main() {

    printf("begin\n");

    pthread_t t1, t2;

    if (pthread_create(&t1, NULL, thread1, NULL))
        abort();
    if (pthread_create(&t2, NULL, thread2, NULL))
        abort();

    //pthread_join(t1, NULL);

    return 0;
}