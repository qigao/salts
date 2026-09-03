#include "platform.h"
#include "salts_thread.h"
#include "disruptor.h"

int main(void) {
  salts_threadpool_t *pool = salts_threadpool_create(1);
  if (pool == NULL) return 1;
  salts_threadpool_destroy(pool);

  salts_sync_set_single_threaded(1);
  if (!salts_sync_is_single_threaded()) return 2;
  salts_sync_set_single_threaded(0);

  return salts_hrtime() == 0 ? 3 : 0;
}
