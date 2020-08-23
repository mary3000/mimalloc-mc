#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <assert.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////

#include "test/flags.h"
#include "mimalloc/src/static.c"

///////////////////////////////////////////////////////////////////////////////////////////////////

void* thread_one(void* arg) {
  genmc_log("mi_malloc begin\n");

  void* p1 = mi_malloc(16);
  printf("p1: %p\n", p1);
  assert(p1);

  mi_free(p1);

  genmc_log("mi_free done\n");

  mi_pthread_done(mi_get_default_heap());

  genmc_log("thread done\n");

  return NULL;
}

int main() {
  genmc_log("mi_process_load begin\n");

  mi_process_load();

  pthread_t t1, t2;

  if (pthread_create(&t1, NULL, thread_one, NULL))
    abort();
  if (pthread_create(&t2, NULL, thread_one, NULL))
    abort();

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  mi_process_done();

  genmc_log("process done\n");

  return 0;
}
