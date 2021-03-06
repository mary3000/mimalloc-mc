#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

///////////////////////////////////////////////////////////////////////////////////////////////////

#include "test/flags.h"
#include "mimalloc/src/static.c"

///////////////////////////////////////////////////////////////////////////////////////////////////

int main() {
  printf("mi_process_load begin\n");

  mi_process_load();

  printf("mi_malloc begin\n");

  void* p1 = mi_malloc(16);
  printf("p1: %p\n", p1);
  assert(p1);

  mi_free(p1);

  printf("mi_free done\n");

  mi_pthread_done(mi_get_default_heap());

  mi_process_done();

  return 0;
}
