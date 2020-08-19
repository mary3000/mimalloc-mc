
#include <stddef.h>

#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>
#include <stdatomic.h>

#include <assert.h>

#include "test/macro.h"
#include "test/race_next/mimalloc_rewrite.h"

#define THREADS 2
#define ALLOCS 1

void* os_alloc(size_t size) {
  char* data = malloc(size);
  return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

mi_block_t* thread1_list[THREADS];
atomic_bool ready = ATOMIC_VAR_INIT(0);

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

void* routine1(void* arg) {
  size_t bsize = (1 << 5);
  size_t bnum = THREADS * ALLOCS;
  init_page(&thread1_page, &thread1_heap, bnum, bsize);

  char* data = os_alloc(bsize * bnum);

  genmc_page_free_list_extend(&thread1_page, data, bsize, bnum);

  mi_block_t* cur = thread1_page.free;
  for (size_t i = 0; i < THREADS; ++i) {
    thread1_list[i] = cur;
    for (size_t j = 0; j < ALLOCS - 1; ++j) {
      cur = mi_block_next(&thread1_page, cur);
    }
    mi_block_t* next = mi_block_next(&thread1_page, cur);
    mi_block_set_next(&thread1_page, cur, NULL);
    cur = next;
  }

  atomic_store_explicit(&ready, true, memory_order_release);
  thread1_page.free = NULL;

  for (size_t allocated = 0; allocated < bnum;) {
    if (_genmc_page_malloc(&thread1_heap, &thread1_page, bsize)) {
      ++allocated;
    }
  }

  free(data);

  return NULL;
}

void* routine2(void* arg) {
  int tid = (int) pthread_self() - 2;

  bool is_ready = atomic_load_explicit(&ready, memory_order_acquire);
  __VERIFIER_assume(is_ready);

  mi_block_t* cur = thread1_list[tid];
  int cnt = 0;
  while (cur != NULL) {
    mi_block_t* next = mi_block_next(&thread1_page, cur);
    _genmc_free_block_mt(&thread1_page, cur);
    cur = next;
    ++cnt;
    assert(cnt <= ALLOCS);
  }
  assert(cnt == ALLOCS);

  return NULL;
}

int main() {
  pthread_t t1;
  pthread_t ts[THREADS];

  if (pthread_create(&t1, NULL, routine1, NULL)) {
    abort();
  }

  for (size_t i = 0; i < THREADS; ++i) {
    if (pthread_create(&ts[i], NULL, routine2, NULL)) {
      abort();
    }
  }

  pthread_join(t1, NULL); // ensure we will free data

  return 0;
}