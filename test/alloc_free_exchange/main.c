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

void* mem;
atomic_bool ready = ATOMIC_VAR_INIT(0);

void* thread_one(void* arg) {
  printf("1 mi_malloc begin\n");

  mem = mi_malloc(16);
  printf("1 mem: %p\n", mem);
  assert(mem);

  atomic_store_explicit(&ready, true, memory_order_release);

  void* mem2 = mi_malloc(16);
  printf("1 mem2: %p\n", mem2);
  mi_free(mem2);
  printf("1 freed mem2\n");

  mi_pthread_done(mi_get_default_heap());

  return NULL;
}

void* thread_two(void* arg) {
  printf("2 mi_malloc begin\n");

  bool is_ready = atomic_load_explicit(&ready, memory_order_acquire);
  __VERIFIER_assume(is_ready);

  printf("2 mem: %p\n", mem);
  mi_free(mem);
  printf("2 mi_free done\n");

  mi_pthread_done(mi_get_default_heap());

  return NULL;
}

int main() {
  printf("mi_process_load begin\n");

  mi_process_load();

  pthread_t t1, t2;

  if (pthread_create(&t1, NULL, thread_one, NULL))
    abort();
  if (pthread_create(&t2, NULL, thread_two, NULL))
    abort();

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  mi_process_done();

  return 0;
}
