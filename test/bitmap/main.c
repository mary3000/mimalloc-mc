#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <assert.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////

void __VERIFIER_assume(int);

atomic_int field = ATOMIC_VAR_INIT(0);

atomic_int info = ATOMIC_VAR_INIT(0);

void* thread_one(void* arg) {
  atomic_store_explicit(&field, 0, memory_order_release);
  atomic_store_explicit(&info, 1, memory_order_release);

  return NULL;
}

void* thread_two(void* arg) {
  int info_val = atomic_load_explicit(&info, memory_order_relaxed); // should be acq!
  __VERIFIER_assume(info_val == 1);

  int exp = 0;
  _Bool success = atomic_compare_exchange_weak_explicit(&field, &exp, 1, memory_order_acq_rel, memory_order_acquire);
  __VERIFIER_assume(success);

  int field_val = atomic_load_explicit(&field, memory_order_relaxed);
  assert(field_val == 1);

  return NULL;
}

int main() {

  pthread_t t1, t2;

  if (pthread_create(&t1, NULL, thread_one, NULL))
    abort();
  if (pthread_create(&t2, NULL, thread_two, NULL))
    abort();

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  return 0;
}
