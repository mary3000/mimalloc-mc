
#include <stddef.h>



#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>
#include <stdatomic.h>

#include <assert.h>

///////////////////////////////////////////////////////////////////////////////////////////////////

#include "flags.h"

#include "mimalloc/static.c"

///////////////////////////////////////////////////////////////////////////////////////////////////


int main() {

    printf("pthread_self = %ld\n", pthread_self());



#if defined(GENMC_REMOVE_CONSTRUCTOR)
    mi_process_load();
#endif

    assert(!_mi_heap_init());


    mi_heap_t* heap = mi_get_default_heap();
    size_t block_size = (1 << 5);
    mi_page_t* page = _mi_segment_page_alloc(heap, block_size, &heap->tld->segments, &heap->tld->os);

    mi_page_init(heap, page, block_size, heap->tld);

    mi_block_t* block = page->free;
    printf("block: 0x%p\n", block);
    size_t cnt = 0;
    while (block != NULL) {
        ++cnt;
        //printf("block: 0x%p\n", block);
        block = mi_block_nextx(heap, block, heap->keys);
    }
    printf("block count = %zu\n", cnt);


    //mi_process_done();

    return 0;
}
