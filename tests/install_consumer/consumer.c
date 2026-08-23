#if defined(CONSUME_PLATFORM)
#include <turbo/clock.h>

int main(void) { return turbo_hrtime() == 0u; }

#elif defined(CONSUME_CONCURRENCY)
#include <turbo/thread_pool.h>

int main(void) {
  turbo_threadpool_t *pool = turbo_threadpool_create(1);
  if (!pool) return 1;
  turbo_threadpool_destroy(pool);
  return 0;
}

#elif defined(CONSUME_CMETA)
#include <cmeta/cmeta.h>

int main(void) {
  return cmeta_type_equal(&cmeta_type_int, &cmeta_type_int) ? 0 : 1;
}

#elif defined(CONSUME_CFLOW)
#include <cflow/clock.h>

int main(void) {
  cflow_clock clock = {0};
  if (!cflow_clock_system_init(&clock)) return 1;
  cflow_clock_destroy(&clock);
  return 0;
}

#elif defined(CONSUME_STL)
#include <turbostl/typed.h>

typed(Vec, InstalledInts, int);

int main(void) {
  InstalledInts values = {0};
  if (InstalledInts_init(&values, 1u) != STL_OK) return 1;
  InstalledInts_destroy(&values);
  return 0;
}

#elif defined(CONSUME_CORE)
#include <platform.h>
#include <turbo_thread.h>

int main(void) {
  turbo_threadpool_t *pool = turbo_threadpool_create(1);
  if (!pool) return 1;
  turbo_threadpool_destroy(pool);
  return turbo_hrtime() == 0u;
}

#else
#error "one TurboUtils consumer contract is required"
#endif
