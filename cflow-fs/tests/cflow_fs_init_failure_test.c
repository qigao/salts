#include <cflow/fs.h>

#include <cflow/executor.h>
#define TINYTEST_NO_MAIN
#include "tinytest.h"

#include <turbo/thread.h>

static size_t fs_init_failure_executor_calls;

static void fs_init_failure_completion(void *user, uint64_t request_id,
                                       cflow_fs_operation_kind operation, int result) {
  (void)user;
  (void)request_id;
  (void)operation;
  (void)result;
}

static void fs_init_failure_mutex_init(turbo_mutex_t *mutex) {
  if (mutex != NULL) *mutex = NULL;
}

static bool fs_init_failure_executor_init(cflow_executor *executor, size_t worker_count,
                                          size_t queue_capacity) {
  (void)executor;
  (void)worker_count;
  (void)queue_capacity;
  ++fs_init_failure_executor_calls;
  return false;
}

#define turbo_mutex_init fs_init_failure_mutex_init
#define cflow_executor_worker_init_with_capacity fs_init_failure_executor_init
#define cflow_fs_service_init cflow_fs_service_init_with_init_failure
#define cflow_fs_try_stat cflow_fs_try_stat_with_init_failure
#define cflow_fs_try_lstat cflow_fs_try_lstat_with_init_failure
#define cflow_fs_try_read_directory cflow_fs_try_read_directory_with_init_failure
#define cflow_fs_try_mkdir cflow_fs_try_mkdir_with_init_failure
#define cflow_fs_try_rmdir cflow_fs_try_rmdir_with_init_failure
#define cflow_fs_try_rename cflow_fs_try_rename_with_init_failure
#define cflow_fs_try_unlink cflow_fs_try_unlink_with_init_failure
#define cflow_fs_try_cancel cflow_fs_try_cancel_with_init_failure
#define cflow_fs_run_ready cflow_fs_run_ready_with_init_failure
#define cflow_fs_close cflow_fs_close_with_init_failure
#define cflow_fs_is_quiescent cflow_fs_is_quiescent_with_init_failure
#define cflow_fs_get_stats cflow_fs_get_stats_with_init_failure
#define cflow_fs_destroy cflow_fs_destroy_with_init_failure
#include "../src/fs.c"
#undef cflow_fs_destroy
#undef cflow_fs_get_stats
#undef cflow_fs_is_quiescent
#undef cflow_fs_close
#undef cflow_fs_run_ready
#undef cflow_fs_try_cancel
#undef cflow_fs_try_unlink
#undef cflow_fs_try_rename
#undef cflow_fs_try_rmdir
#undef cflow_fs_try_mkdir
#undef cflow_fs_try_read_directory
#undef cflow_fs_try_lstat
#undef cflow_fs_try_stat
#undef cflow_fs_service_init
#undef cflow_executor_worker_init_with_capacity
#undef turbo_mutex_init

spec("CFlow filesystem service initialization failures") {
  it("returns ENOMEM before executor initialization when its mutex is unavailable") {
    cflow_fs_service service = {0};
    const cflow_fs_config config = {.worker_count = 1u,
                                    .request_capacity = 1u,
                                    .path_capacity = 16u,
                                    .completion = fs_init_failure_completion};

    fs_init_failure_executor_calls = 0u;
    check_equal(cflow_fs_service_init_with_init_failure(&service, &config), TURBO_ENOMEM);
    check_equal(fs_init_failure_executor_calls, (size_t)0u);
    check_null(service.impl);
  }
}
