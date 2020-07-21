
#include <stddef.h>



#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>
#include <stdatomic.h>

#include <assert.h>

///////////////////////////////////////////////////////////////////////////////////////////////////

#include "flags.h"

#include "../static.c"

///////////////////////////////////////////////////////////////////////////////////////////////////

__thread int a = 5;

void *thread_one(void *arg)
{
    //return NULL;

    printf("\n\nTID: %lx\n\n", _mi_thread_id());
    ++a;
    printf("a: %p, %d\n\n", &a, a);

    printf("pthread_self = %ld\n", pthread_self());
    genmc_end = (unsigned long)pthread_self() * MI_REGION_SIZE;

    printf("after genmc_end\n");

    //assert(!_mi_heap_init());

    //return NULL;

    //mi_heap_t* heap = mi_get_default_heap();

    //return NULL;

    size_t bsize = (1 << 5);
    size_t bnum = 10;
    char* data = malloc(bsize * bnum);
    for (size_t i = 0; i < bsize * bnum; ++i) {
        //printf("val = %d\n", data[i]);
        data[i] = 0;
    }
    //mi_page_t* page = _mi_segment_page_alloc(heap, bsize, &heap->tld->segments, &heap->tld->os);
    char* page_data = malloc(sizeof(mi_page_t));
    for (size_t i = 0; i < sizeof(mi_page_t); ++i) {
        page_data[i] = 0;
    }
    mi_page_t* page = (mi_page_t *)page_data;

    //mi_page_init(heap, page, bsize, heap->tld);
    //mi_page_free_list_extend(page, bsize, 10, NULL);

    mi_block_t* const start = (mi_block_t *const) data;

    // initialize a sequential free list
    mi_block_t* const last = (mi_block_t *const) (data + bsize * (bnum - 1));
    mi_block_t* block = start;
    while(block <= last) {
        mi_block_t* next = (mi_block_t*)((uint8_t*)block + bsize);
        mi_block_set_next(page,block,next);
        block = next;
    }
    // prepend to free list (usually `NULL`)
    mi_block_set_next(page, last, page->free);
    page->free = start;

    block = page->free;
    printf("block: 0x%p\n", block);
    size_t cnt = 0;
    while (block != NULL) {
        ++cnt;
        //printf("block: 0x%p\n", block);
        block = mi_block_nextx(NULL, block, NULL);
    }
    printf("block count = %zu\n", cnt);

    return NULL;
}

int main() {

    printf("begin\n");

    //return 0;

    printf("\n\nTID: %lx\n\n", _mi_thread_id());
    printf("a: %p, %d\n\n", &a, a);

    //mi_process_load();

    //return 0;

    printf("main: inited\n");

    pthread_t t1, t2;

    if (pthread_create(&t1, NULL, thread_one, NULL))
        abort();
    /*if (pthread_create(&t2, NULL, thread_one, NULL))
        abort();*/

    //pthread_join(t1, NULL);

    return 0;
}