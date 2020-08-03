
#include <stddef.h>

#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>
#include <stdatomic.h>

#include <assert.h>

#include "test/macro.h"
#include "test/race_next/mimalloc_rewrite.h"

void* os_alloc(size_t size) {
    char* data = malloc(size);
    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

typedef _Atomic (mi_block_t*) atomic_block;
atomic_block thread1_block;

mi_heap_t thread1_heap;
mi_page_t thread1_page;

void init_page(mi_page_t* page, mi_heap_t* heap, size_t reserved, uint32_t bsize) {
    page->free = NULL;
    page->capacity = 0;
    page->reserved = reserved;
    page->xblock_size = bsize;

    heap->pages_free_direct[0] = page;
    atomic_store_explicit(&heap->thread_delayed_free, NULL, memory_order_release);

    atomic_store_explicit(&page->xheap, (uintptr_t) heap, memory_order_release);
}

void *routine1(void *arg)
{
    size_t bsize = (1 << 5);
    size_t bnum = 3;
    init_page(&thread1_page, &thread1_heap, bnum, bsize);

    char* data = os_alloc(bsize * bnum);
    genmc_page_free_list_extend(&thread1_page, data, bsize, bnum);

    atomic_store_explicit(&thread1_block, thread1_page.free, memory_order_release);
    thread1_page.free = NULL;

    for (size_t allocated = 0; allocated < bnum;) {
      if (_genmc_page_malloc(&thread1_heap, &thread1_page, bsize)) {
        ++allocated;
      }
    }

    return NULL;
}

void *routine2(void *arg)
{

    mi_block_t* other_block = atomic_load_explicit(&thread1_block, memory_order_acquire);

    __VERIFIER_assume(other_block != NULL);
    assert(other_block != NULL);

    while (other_block != NULL) {
        mi_block_t* next = (mi_block_t *) other_block->next;
        _genmc_free_block_mt(&thread1_page, other_block);
        other_block = next;
    }

    return NULL;
}

int main() {
    pthread_t t1, t2;

    if (pthread_create(&t1, NULL, routine1, NULL))
        abort();
    if (pthread_create(&t2, NULL, routine2, NULL))
        abort();

    return 0;
}