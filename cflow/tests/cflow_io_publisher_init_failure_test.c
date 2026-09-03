#include <cflow/cflow.h>

#define TINYTEST_NO_MAIN
#include "tinytest.h"

static size_t io_source_init_failure_actor_calls;

static int io_source_init_failure_submit(void *backend_user, cflow_io_actor *actor,
                                         cflow_io_request_id request_id, cflow_io_lease_id lease_id,
                                         void *operation_user) {
  (void)backend_user;
  (void)actor;
  (void)request_id;
  (void)lease_id;
  (void)operation_user;
  return SALTS_OK;
}

static cflow_io_publisher_prepare_status
io_source_init_failure_prepare(void *user, cflow_io_operation *operation, const char **error) {
  (void)user;
  (void)operation;
  (void)error;
  return CFLOW_IO_PUBLISHER_PREPARE_DONE;
}

static cflow_read_status io_source_init_failure_encode(void *user, cflow_io_request_id request_id,
                                                       cflow_io_lease_id lease_id,
                                                       void *operation_user,
                                                       const cflow_io_completion *completion,
                                                       void *out_value, const char **error) {
  (void)user;
  (void)request_id;
  (void)lease_id;
  (void)operation_user;
  (void)completion;
  (void)out_value;
  (void)error;
  return CFLOW_READ_DONE;
}

static void io_source_init_failure_drive(void *user) { (void)user; }

static int io_source_init_failure_actor_init(cflow_io_actor *actor,
                                             const cflow_io_actor_config *config) {
  (void)actor;
  (void)config;
  ++io_source_init_failure_actor_calls;
  return SALTS_EINVAL;
}

#define cflow_io_actor_init io_source_init_failure_actor_init
#define cflow_publisher_from_io_actor cflow_publisher_from_io_actor_with_init_failure
#define cflow_publisher_from_io_actor_windowed cflow_publisher_from_io_actor_windowed_with_init_failure
#define cflow_io_publisher_owner_run_ready cflow_io_publisher_owner_run_ready_with_init_failure
#define cflow_io_publisher_owner_run_serial_batch_phase_internal                                  \
  cflow_io_publisher_owner_run_serial_batch_phase_internal_with_init_failure
#define cflow_io_publisher_owner_is_quiescent cflow_io_publisher_owner_is_quiescent_with_init_failure
#define cflow_io_publisher_owner_get_stats cflow_io_publisher_owner_get_stats_with_init_failure
#define cflow_io_publisher_owner_get_window_stats                                                     \
  cflow_io_publisher_owner_get_window_stats_with_init_failure
#define cflow_io_publisher_owner_close cflow_io_publisher_owner_close_with_init_failure
#include "../src/io_publisher.c"
#undef cflow_io_publisher_owner_close
#undef cflow_io_publisher_owner_get_window_stats
#undef cflow_io_publisher_owner_get_stats
#undef cflow_io_publisher_owner_is_quiescent
#undef cflow_io_publisher_owner_run_ready
#undef cflow_io_publisher_owner_run_serial_batch_phase_internal
#undef cflow_publisher_from_io_actor_windowed
#undef cflow_publisher_from_io_actor
#undef cflow_io_actor_init

spec("CFlow IO source initialization failures") {
  it("preserves an Actor invalid-configuration error") {
    cflow_publisher source = {0};
    cflow_io_publisher_owner owner = {0};
    const cflow_io_publisher_config config = {.name = "init-failure",
                                           .type = &cmeta_type_int,
                                           .backend = {.submit = io_source_init_failure_submit},
                                           .prepare = io_source_init_failure_prepare,
                                           .encode = io_source_init_failure_encode,
                                           .drive = io_source_init_failure_drive};

    io_source_init_failure_actor_calls = 0u;
    check_equal(cflow_publisher_from_io_actor_with_init_failure(&source, &owner, &config),
                SALTS_EINVAL);
    check_equal(io_source_init_failure_actor_calls, (size_t)1u);
    check_false(cflow_publisher_valid(&source));
    check_null(owner.impl);
  }
}
