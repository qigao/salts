#include "platform.h"
#include "cmeta_thread.h"
#include "disruptor.h"

int main(void) {
  cmeta_threadpool_t *pool = cmeta_threadpool_create(1);
  if (pool == NULL) return 1;
  cmeta_threadpool_destroy(pool);

  cmeta_sync_set_single_threaded(1);
  if (!cmeta_sync_is_single_threaded()) return 2;
  cmeta_sync_set_single_threaded(0);

  return cmeta_hrtime() == 0 ? 3 : 0;
}
