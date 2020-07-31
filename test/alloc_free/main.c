#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <assert.h>

///////////////////////////////////////////////////////////////////////////////////////////////////

#include "test/flags.h"
#include "mimalloc-custom//static.c"

///////////////////////////////////////////////////////////////////////////////////////////////////

int main() {

#if defined(GENMC_REMOVE_CONSTRUCTOR)
    mi_process_load();
#endif

    void* p1 = mi_malloc(16);
    printf("p1: %p\n", p1);
    assert(p1);

    mi_free(p1);

#if defined(GENMC_REMOVE_CONSTRUCTOR)
    mi_process_done();
#endif

	return 0;
}
