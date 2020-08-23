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

#define ABANDONED_THREADS 2
#define FREEING_THREADS 3
#define ALLOCS 2
#define ALLOCS_AFTER 3

void* mem[ABANDONED_THREADS][FREEING_THREADS][ALLOCS];
atomic_bool ready[ABANDONED_THREADS];

void* routine1(void* arg) {
  printf("1 mi_malloc begin\n");

  int tid = pthread_self() - 1;

  for (size_t i = 0; i < FREEING_THREADS; ++i) {
    for (size_t j = 0; j < ALLOCS; ++j) {
        mem[tid][i][j] = mi_malloc(16);
        printf("1 mem[%zu][%zu][%zu]: %p\n", tid, i, j, mem[tid][i][j]);
        assert(mem[tid][i][j]);
    }
  }

  mi_pthread_done(mi_get_default_heap());

  atomic_store_explicit(&ready[tid], true, memory_order_release);

  return NULL;
}

void* routine2(void* arg) {
  printf("2 mi_malloc begin\n");

  int tid = pthread_self() - ABANDONED_THREADS - 1;

  for (size_t i = 0; i < ABANDONED_THREADS; ++i) {

    bool is_ready = atomic_load_explicit(&ready[i], memory_order_acquire);
    __VERIFIER_assume(is_ready);

    void** local_mem = mem[i][tid];

    for (size_t j = 0; j < ALLOCS; ++j) {
      printf("2 mem: %p\n", local_mem[j]);
      mi_free(local_mem[j]);
      printf("2 mi_free done\n");
    }

  }

  char* after_mem[ALLOCS_AFTER];
  for (size_t i = 0; i < ALLOCS_AFTER; ++i) {
    after_mem[i] = mi_malloc(16);
    assert(after_mem[i]);
  }
  for (size_t i = 0; i < ALLOCS_AFTER; ++i) {
    mi_free(after_mem[i]);
  }

  mi_pthread_done(mi_get_default_heap());

  return NULL;
}

int main() {

  printf("mi_process_load begin\n");

  mi_process_load();

  pthread_t t1[ABANDONED_THREADS];
  pthread_t ts[FREEING_THREADS];

  for (size_t i = 0; i < ABANDONED_THREADS; ++i) {
    if (pthread_create(&t1[i], NULL, routine1, NULL)) {
      abort();
    }
  }
  for (size_t i = 0; i < FREEING_THREADS; ++i) {
    if (pthread_create(&ts[i], NULL, routine2, NULL)) {
      abort();
    }
  }

  for (size_t i = 0; i < ABANDONED_THREADS; ++i) {
    pthread_join(t1[i], NULL);
  }
  for (size_t i = 0; i < FREEING_THREADS; ++i) {
    pthread_join(ts[i], NULL);
  }

  mi_process_done();

  return 0;
}
