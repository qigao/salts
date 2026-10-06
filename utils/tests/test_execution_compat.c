#include "platform.h"
#include "cmeta_thread.h"
#include "disruptor.h"
#include "tinytest.h"

suite("Core execution compatibility headers") {
  group("transitive Platform and Concurrency linkage") {
    static cmeta_threadpool_t *pool;
    after_each() {
      if (pool != NULL) cmeta_threadpool_destroy(pool);
      pool = NULL;
      cmeta_sync_set_single_threaded(0);
    }
    it("creates a thread pool through the compatibility header") {
      pool = cmeta_threadpool_create(1);
      check_not_null(pool);
    }
    it("exposes the single-threaded synchronization mode") {
      cmeta_sync_set_single_threaded(1);
      check_true(cmeta_sync_is_single_threaded());
    }
    it("exposes a nonzero high-resolution clock") {
      check_not_equal(cmeta_hrtime(), 0u);
    }
  }
}
