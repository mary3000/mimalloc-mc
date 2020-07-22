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

typedef struct {
    int  valid;
} mystruct;

int main() {



#if defined(GENMC_REMOVE_CONSTRUCTOR)
    mi_process_load();
#endif

    //assert(0 && "main assert");


	//printf("fffff\n");

	//assert(0);

	//void* pp = malloc(268435456);
	//assert(pp);

    void* p1 = mi_malloc(16);
    printf("p1: %p\n", p1);
    assert(p1);

    mi_free(p1);

    mi_process_done();

	return 0;
}
