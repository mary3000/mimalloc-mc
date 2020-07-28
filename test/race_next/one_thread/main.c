
#include <stddef.h>

#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>
#include <stdatomic.h>

#include <assert.h>

// #define GENMC_LOG 1

#include "test/macro.h"
#include "test/race_next/mimalloc_rewrite.h"

void* os_alloc(size_t size) {
    char* data = malloc(size);
    genmc_log("malloc(%zu) = [%p, %p)\n", size, data, data + size);
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

void *routine1(void *arg)
{
    int tid = (int)pthread_self() - 1;

    mi_heap_t* heap = &heaps[tid];
    mi_page_t* page = &pages[tid];

    size_t bsize = (1 << 5);
    size_t bnum = 3;
    init_page(page, heap, bnum, bsize);

    char* data = os_alloc(bsize * bnum);

    genmc_page_free_list_extend(page, data, bsize, bnum);

    atomic_store_explicit(&list[tid], page->free, memory_order_release);
    page->free = NULL;

    size_t allocated = 0;
    while (allocated < bnum) {
        if (_genmc_page_malloc(heap, page, bsize)) {
            ++allocated;
        }
    }

    return NULL;
}

void *routine2(void *arg)
{
    int tid = (int)pthread_self() - 1;
    int other_tid = (tid + 1) % 2;

    mi_block_t* other_block = atomic_load_explicit(&list[other_tid], memory_order_acquire);
    mi_page_t* other_page = &pages[other_tid];

    __VERIFIER_assume(other_block != NULL);
    assert(other_block != NULL);

    genmc_log("other block = %p\n", other_block);

    while (other_block != NULL) {
        mi_block_t* next = (mi_block_t *) other_block->next;
        _genmc_free_block_mt(other_page, other_block);
        other_block = next;
    }

    return NULL;
}

int main() {

    genmc_log("begin\n");

    pthread_t t1, t2;

    if (pthread_create(&t1, NULL, routine1, NULL))
        abort();
    if (pthread_create(&t2, NULL, routine2, NULL))
        abort();

    //pthread_join(t1, NULL);

    return 0;
}