#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <assert.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////

void __VERIFIER_assume(int);

atomic_int bitmap = ATOMIC_VAR_INIT(0);

atomic_int info = ATOMIC_VAR_INIT(0);

void* thread_one(void* arg) {
  // 1. Initialize bitmap
  atomic_store_explicit(&bitmap, 0, memory_order_release);
  // 2. Publish region
  atomic_store_explicit(&info, 1, memory_order_release);

  return NULL;
}

void* thread_two(void* arg) {
  // 1. Read info and assume another thread already published the region
#if defined(GENMC_BUG_BITMAP)
  int info_val = atomic_load_explicit(&info, memory_order_relaxed); // should be acquire!
#else
  int info_val = atomic_load_explicit(&info, memory_order_acquire);
#endif
  __VERIFIER_assume(info_val == 1);

  // 2. Claim bits in bitmap
  int exp = 0;
  _Bool success = atomic_compare_exchange_weak_explicit(&bitmap, &exp, 1, memory_order_acq_rel, memory_order_acquire);
  __VERIFIER_assume(success);

  // 3. Read bitmap again in order to check if bits are claimed
  int bitmap_val = atomic_load_explicit(&bitmap, memory_order_relaxed);
  // assertion fails because there is no guarantee bitmap was read from T2's CAS, instead it can read from T1's store.
  assert(bitmap_val == 1);

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
