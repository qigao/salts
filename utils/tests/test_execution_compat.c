#include "platform.h"
#include "turbo_thread.h"
#include "disruptor.h"

int main(void) {
  turbo_threadpool_t *pool = turbo_threadpool_create(1);
  if (pool == NULL) return 1;
  turbo_threadpool_destroy(pool);

  turbo_sync_set_single_threaded(1);
  if (!turbo_sync_is_single_threaded()) return 2;
  turbo_sync_set_single_threaded(0);

  return turbo_hrtime() == 0 ? 3 : 0;
}
