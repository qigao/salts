#include <salts/native_io_sharded.h>

#include <salts/thread.h>
#include <salts_coro_executor.h>

#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct native_io_sharded_shard native_io_sharded_shard;
typedef struct native_io_sharded_slot native_io_sharded_slot;
typedef struct native_io_sharded_request_ownership native_io_sharded_request_ownership;
typedef struct native_io_sharded_ownership_settlement native_io_sharded_ownership_settlement;

struct native_io_sharded_context {
  native_io_sharded *runtime;
  size_t shard;
};

struct native_io_sharded_slot {
  native_io_sharded *runtime;
  native_io_sharded_shard *shard;
  native_io_sharded_task task;
  size_t index;
  int active;
};

struct native_io_sharded_request_ownership {
  native_io_sharded_ownership ownership;
  uint32_t generation;
  int active;
};

struct native_io_sharded_ownership_settlement {
  native_io_sharded_ownership ownership;
  int active;
};

struct native_io_sharded_shard {
  native_io_sharded *runtime;
  size_t index;
  native_io_backend backend;
  native_io_sharded_context context;
  native_io_completion *completion_scratch;
  native_io_sharded_ownership_settlement *ownership_settlements;
  size_t completion_capacity;
  native_io_sharded_request_ownership *request_ownerships;
  size_t request_ownership_capacity;
  int observe_active;

  native_io_sharded_slot *slots;
  size_t *free_slots;
  size_t slot_capacity;
  size_t free_count;
  salts_mutex_t slot_lock;
  salts_cond_t slot_space;

  atomic_int bootstrap_done;
  atomic_int backend_initialized;
  atomic_int init_status;
  atomic_int teardown_done;
  atomic_int teardown_status;
  int teardown_submitted;
};

struct native_io_sharded {
  salts_coro_executor_t *executor;
  native_io_sharded_shard *shards;
  native_io_backend_config backend_config;
  size_t shard_count;
  size_t queue_capacity_per_shard;
  size_t command_slot_capacity_per_shard;
  uint64_t identity;

  salts_mutex_t admission_lock;
  salts_cond_t admission_idle;
  size_t inflight_dispatches;
  atomic_int accepting;
  int executor_shutdown;

  atomic_uint_fast64_t submitted_tasks;
  atomic_uint_fast64_t same_shard_direct_tasks;
  atomic_uint_fast64_t queued_dispatches;
  atomic_uint_fast64_t completed_tasks;
  atomic_uint_fast64_t cancelled_tasks;
  atomic_uint_fast64_t rejected_tasks;
  atomic_uint_fast64_t active_command_slots;
  atomic_uint_fast64_t peak_command_slots;
};

static atomic_uint_fast64_t native_io_sharded_identity_source = 0u;

static uint64_t native_io_sharded_next_identity(void) {
  uint64_t identity = (uint64_t)atomic_fetch_add(&native_io_sharded_identity_source, 1u) + 1u;
  if (identity == 0u)
    identity = (uint64_t)atomic_fetch_add(&native_io_sharded_identity_source, 1u) + 1u;
  return identity;
}

static int native_io_sharded_is_power_of_two(size_t value) {
  return value != 0u && (value & (value - 1u)) == 0u;
}

static void native_io_sharded_update_peak(atomic_uint_fast64_t *peak, uint64_t candidate) {
  uint_fast64_t seen = atomic_load(peak);
  while (candidate > seen &&
         !atomic_compare_exchange_weak(peak, &seen, (uint_fast64_t)candidate)) {
  }
}

static int native_io_sharded_in_callback(const native_io_sharded *runtime) {
  return runtime != NULL && runtime->executor != NULL &&
         salts_coro_executor_current() == runtime->executor;
}

static native_io_sharded_shard *
native_io_sharded_context_owner(native_io_sharded_context *context) {
  native_io_sharded *runtime;
  if (context == NULL || context->runtime == NULL) return NULL;
  runtime = context->runtime;
  if (runtime->executor == NULL || context->shard >= runtime->shard_count ||
      salts_coro_executor_current_shard(runtime->executor) != context->shard)
    return NULL;
  return &runtime->shards[context->shard];
}

static native_io_operation
native_io_sharded_native_operation(const native_io_sharded_operation *operation) {
  native_io_operation native_operation = {0};
  if (operation == NULL) return native_operation;
  native_operation.kind = operation->kind;
  native_operation.endpoint = operation->endpoint.native_endpoint;
  native_operation.buffer = operation->buffer;
  native_operation.length = operation->length;
  native_operation.user_data = operation->user_data;
  native_operation.address = operation->address;
  native_operation.address_capacity = operation->address_capacity;
  native_operation.address_length = operation->address_length;
  return native_operation;
}

static int native_io_sharded_endpoint_access(native_io_sharded_context *context,
                                             native_io_sharded_endpoint endpoint,
                                             native_io_sharded_shard **out_shard) {
  native_io_sharded_shard *shard = native_io_sharded_context_owner(context);
  if (out_shard != NULL) *out_shard = NULL;
  if (shard == NULL || !native_io_sharded_endpoint_valid(endpoint)) return SALTS_EINVAL;
  if (endpoint.owner_identity != shard->runtime->identity) return SALTS_ENOENT;
  if ((size_t)endpoint.owner_shard != shard->index) return SALTS_EPERM;
  if (out_shard != NULL) *out_shard = shard;
  return SALTS_OK;
}

static int native_io_sharded_request_access(native_io_sharded_context *context,
                                            native_io_sharded_request request,
                                            native_io_sharded_shard **out_shard) {
  native_io_sharded_shard *shard = native_io_sharded_context_owner(context);
  if (out_shard != NULL) *out_shard = NULL;
  if (shard == NULL || !native_io_sharded_request_valid(request)) return SALTS_EINVAL;
  if (request.owner_identity != shard->runtime->identity) return SALTS_ENOENT;
  if ((size_t)request.owner_shard != shard->index) return SALTS_EPERM;
  if (out_shard != NULL) *out_shard = shard;
  return SALTS_OK;
}

static int native_io_sharded_dispatch_begin(native_io_sharded *runtime) {
  salts_mutex_lock(&runtime->admission_lock);
  if (!atomic_load(&runtime->accepting)) {
    salts_mutex_unlock(&runtime->admission_lock);
    return SALTS_ESHUTDOWN;
  }
  runtime->inflight_dispatches++;
  salts_mutex_unlock(&runtime->admission_lock);
  return SALTS_OK;
}

static void native_io_sharded_dispatch_end(native_io_sharded *runtime) {
  salts_mutex_lock(&runtime->admission_lock);
  if (runtime->inflight_dispatches != 0u) runtime->inflight_dispatches--;
  if (runtime->inflight_dispatches == 0u) salts_cond_broadcast(&runtime->admission_idle);
  salts_mutex_unlock(&runtime->admission_lock);
}

static void native_io_sharded_release_slot(native_io_sharded_slot *slot) {
  native_io_sharded_shard *shard;
  native_io_sharded *runtime;

  if (slot == NULL || !slot->active) return;
  shard = slot->shard;
  runtime = slot->runtime;

  salts_mutex_lock(&shard->slot_lock);
  if (slot->active) {
    slot->active = 0;
    slot->task = (native_io_sharded_task){0};
    shard->free_slots[shard->free_count++] = slot->index;
    atomic_fetch_sub(&runtime->active_command_slots, 1u);
    salts_cond_signal(&shard->slot_space);
  }
  salts_mutex_unlock(&shard->slot_lock);
}

static int native_io_sharded_claim_slot(native_io_sharded *runtime,
                                        native_io_sharded_shard *shard,
                                        const native_io_sharded_task *task, int blocking,
                                        native_io_sharded_slot **out_slot) {
  native_io_sharded_slot *slot;
  uint64_t active;
  size_t index;

  if (out_slot != NULL) *out_slot = NULL;
  if (runtime == NULL || shard == NULL || task == NULL || out_slot == NULL) return SALTS_EINVAL;

  salts_mutex_lock(&shard->slot_lock);
  for (;;) {
    if (!atomic_load(&runtime->accepting)) {
      salts_mutex_unlock(&shard->slot_lock);
      return SALTS_ESHUTDOWN;
    }
    if (shard->free_count != 0u) break;
    if (!blocking) {
      salts_mutex_unlock(&shard->slot_lock);
      return SALTS_ENOBUFS;
    }
    if (native_io_sharded_in_callback(runtime)) {
      salts_mutex_unlock(&shard->slot_lock);
      return SALTS_EBUSY;
    }
    salts_cond_wait(&shard->slot_space, &shard->slot_lock);
  }

  index = shard->free_slots[--shard->free_count];
  slot = &shard->slots[index];
  slot->task = *task;
  slot->active = 1;
  active = atomic_fetch_add(&runtime->active_command_slots, 1u) + 1u;
  native_io_sharded_update_peak(&runtime->peak_command_slots, active);
  salts_mutex_unlock(&shard->slot_lock);

  *out_slot = slot;
  return SALTS_OK;
}

static void native_io_sharded_routed_run(coro_t *coroutine, void *arg) {
  native_io_sharded_slot *slot = (native_io_sharded_slot *)arg;
  native_io_sharded *runtime = slot->runtime;
  (void)coroutine;

  slot->task.run(&slot->shard->context, slot->task.arg);
  atomic_fetch_add(&runtime->completed_tasks, 1u);
}

static void native_io_sharded_routed_cancel(void *arg, int status) {
  native_io_sharded_slot *slot = (native_io_sharded_slot *)arg;
  native_io_sharded *runtime = slot->runtime;

  if (slot->task.cancel != NULL) slot->task.cancel(slot->task.arg, status);
  atomic_fetch_add(&runtime->cancelled_tasks, 1u);
}

static void native_io_sharded_routed_finalize(void *arg) {
  native_io_sharded_slot *slot = (native_io_sharded_slot *)arg;
  native_io_sharded_finalize_fn finalize = slot->task.finalize;
  void *user_arg = slot->task.arg;

  if (finalize != NULL) finalize(user_arg);
  native_io_sharded_release_slot(slot);
}

static void native_io_sharded_bootstrap(coro_t *coroutine, void *arg) {
  native_io_sharded_shard *shard = (native_io_sharded_shard *)arg;
  int status;
  (void)coroutine;

  status = native_io_backend_init(&shard->backend, &shard->runtime->backend_config);
  atomic_store(&shard->init_status, status);
  if (status == SALTS_OK) atomic_store(&shard->backend_initialized, 1);
  atomic_store(&shard->bootstrap_done, 1);
}

static void native_io_sharded_teardown(coro_t *coroutine, void *arg) {
  native_io_sharded_shard *shard = (native_io_sharded_shard *)arg;
  int status = SALTS_OK;
  (void)coroutine;

  if (atomic_load(&shard->backend_initialized)) {
    status = native_io_backend_close(&shard->backend);
    if (status == SALTS_OK) status = native_io_backend_destroy(&shard->backend);
    if (status == SALTS_OK) atomic_store(&shard->backend_initialized, 0);
  }
  atomic_store(&shard->teardown_status, status);
  atomic_store(&shard->teardown_done, 1);
}

static void native_io_sharded_wake_slot_waiters(native_io_sharded *runtime) {
  if (runtime == NULL || runtime->shards == NULL) return;
  for (size_t index = 0u; index < runtime->shard_count; ++index) {
    native_io_sharded_shard *shard = &runtime->shards[index];
    salts_mutex_lock(&shard->slot_lock);
    salts_cond_broadcast(&shard->slot_space);
    salts_mutex_unlock(&shard->slot_lock);
  }
}

static void native_io_sharded_destroy_storage(native_io_sharded *runtime) {
  if (runtime == NULL) return;
  if (runtime->shards != NULL) {
    for (size_t index = 0u; index < runtime->shard_count; ++index) {
      native_io_sharded_shard *shard = &runtime->shards[index];
      free(shard->request_ownerships);
      free(shard->ownership_settlements);
      free(shard->completion_scratch);
      free(shard->free_slots);
      free(shard->slots);
      salts_mutex_destroy(&shard->slot_lock);
      salts_cond_destroy(&shard->slot_space);
    }
  }
  free(runtime->shards);
  salts_cond_destroy(&runtime->admission_idle);
  salts_mutex_destroy(&runtime->admission_lock);
  free(runtime);
}

static void native_io_sharded_cleanup_failed_create(native_io_sharded *runtime) {
  if (runtime == NULL) return;
  if (runtime->executor != NULL) {
    for (size_t index = 0u; index < runtime->shard_count; ++index) {
      native_io_sharded_shard *shard = &runtime->shards[index];
      if (atomic_load(&shard->backend_initialized)) {
        const salts_coro_executor_task_t task = {
            native_io_sharded_teardown, NULL, NULL, shard};
        if (salts_coro_executor_submit_to(runtime->executor, index, &task) == SALTS_OK)
          shard->teardown_submitted = 1;
      }
    }
    (void)salts_coro_executor_shutdown(runtime->executor);
    (void)salts_coro_executor_wait(runtime->executor);
    (void)salts_coro_executor_destroy(runtime->executor);
    runtime->executor = NULL;
  }
  native_io_sharded_destroy_storage(runtime);
}

int native_io_sharded_create(const native_io_sharded_config *config,
                             native_io_sharded **out_runtime) {
  salts_coro_executor_config_t executor_config = SALTS_CORO_EXECUTOR_CONFIG_DEFAULT;
  native_io_sharded *runtime;
  size_t slot_capacity;
  int status = SALTS_OK;

  if (out_runtime != NULL) *out_runtime = NULL;
  if (config == NULL || out_runtime == NULL || config->shard_count == 0u ||
      !native_io_sharded_is_power_of_two(config->queue_capacity_per_shard))
    return SALTS_EINVAL;
  if (config->queue_capacity_per_shard > (size_t)INT64_MAX ||
      config->queue_capacity_per_shard == SIZE_MAX ||
      config->shard_count > (size_t)INT_MAX ||
      config->shard_count > SIZE_MAX / sizeof(native_io_sharded_shard))
    return SALTS_ERANGE;
  if (!native_io_backend_kind_supported(config->backend.kind)) return SALTS_ENOTSUP;

  slot_capacity = config->queue_capacity_per_shard + 1u;
  if (slot_capacity > SIZE_MAX / sizeof(native_io_sharded_slot) ||
      slot_capacity > SIZE_MAX / sizeof(size_t) ||
      (config->backend.completion_batch_capacity != 0u &&
       (config->backend.completion_batch_capacity > SIZE_MAX / sizeof(native_io_completion) ||
        config->backend.completion_batch_capacity >
            SIZE_MAX / sizeof(native_io_sharded_ownership_settlement))) ||
      (config->backend.request_capacity != 0u &&
       config->backend.request_capacity >
           SIZE_MAX / sizeof(native_io_sharded_request_ownership)))
    return SALTS_ERANGE;

  runtime = (native_io_sharded *)calloc(1, sizeof(*runtime));
  if (runtime == NULL) return SALTS_ENOMEM;
  runtime->backend_config = config->backend;
  runtime->shard_count = config->shard_count;
  runtime->queue_capacity_per_shard = config->queue_capacity_per_shard;
  runtime->command_slot_capacity_per_shard = slot_capacity;
  runtime->identity = native_io_sharded_next_identity();
  salts_mutex_init(&runtime->admission_lock);
  salts_cond_init(&runtime->admission_idle);
  if (runtime->admission_lock == NULL || runtime->admission_idle == NULL) {
    native_io_sharded_destroy_storage(runtime);
    return SALTS_ENOMEM;
  }

  runtime->shards =
      (native_io_sharded_shard *)calloc(runtime->shard_count, sizeof(*runtime->shards));
  if (runtime->shards == NULL) {
    native_io_sharded_destroy_storage(runtime);
    return SALTS_ENOMEM;
  }

  for (size_t shard_index = 0u; shard_index < runtime->shard_count; ++shard_index) {
    native_io_sharded_shard *shard = &runtime->shards[shard_index];
    shard->runtime = runtime;
    shard->index = shard_index;
    shard->context.runtime = runtime;
    shard->context.shard = shard_index;
    shard->slot_capacity = slot_capacity;
    shard->free_count = slot_capacity;
    shard->completion_capacity = runtime->backend_config.completion_batch_capacity;
    shard->request_ownership_capacity = runtime->backend_config.request_capacity;
    salts_mutex_init(&shard->slot_lock);
    salts_cond_init(&shard->slot_space);
    shard->slots = (native_io_sharded_slot *)calloc(slot_capacity, sizeof(*shard->slots));
    shard->free_slots = (size_t *)calloc(slot_capacity, sizeof(*shard->free_slots));
    if (shard->completion_capacity != 0u) {
      shard->completion_scratch = (native_io_completion *)calloc(
          shard->completion_capacity, sizeof(*shard->completion_scratch));
      shard->ownership_settlements = (native_io_sharded_ownership_settlement *)calloc(
          shard->completion_capacity, sizeof(*shard->ownership_settlements));
    }
    if (shard->request_ownership_capacity != 0u)
      shard->request_ownerships = (native_io_sharded_request_ownership *)calloc(
          shard->request_ownership_capacity, sizeof(*shard->request_ownerships));
    if (shard->slot_lock == NULL || shard->slot_space == NULL || shard->slots == NULL ||
        shard->free_slots == NULL ||
        (shard->completion_capacity != 0u &&
         (shard->completion_scratch == NULL || shard->ownership_settlements == NULL)) ||
        (shard->request_ownership_capacity != 0u && shard->request_ownerships == NULL)) {
      native_io_sharded_cleanup_failed_create(runtime);
      return SALTS_ENOMEM;
    }
    for (size_t slot = 0u; slot < slot_capacity; ++slot) {
      shard->slots[slot].runtime = runtime;
      shard->slots[slot].shard = shard;
      shard->slots[slot].index = slot;
      shard->free_slots[slot] = slot_capacity - slot - 1u;
    }
  }

  executor_config.worker_count = runtime->shard_count;
  executor_config.queue_capacity_per_worker = runtime->queue_capacity_per_shard;
  executor_config.coroutine_pool.initial_capacity = 1u;
  executor_config.coroutine_pool.max_capacity = 1u;
  runtime->executor = salts_coro_executor_create(&executor_config);
  if (runtime->executor == NULL) {
    native_io_sharded_cleanup_failed_create(runtime);
    return SALTS_ENOMEM;
  }

  for (size_t shard_index = 0u; shard_index < runtime->shard_count; ++shard_index) {
    const salts_coro_executor_task_t task = {
        native_io_sharded_bootstrap, NULL, NULL, &runtime->shards[shard_index]};
    status = salts_coro_executor_submit_to(runtime->executor, shard_index, &task);
    if (status != SALTS_OK) break;
  }
  {
    const int wait_status = salts_coro_executor_wait(runtime->executor);
    if (status == SALTS_OK) status = wait_status;
  }

  if (status == SALTS_OK) {
    for (size_t shard_index = 0u; shard_index < runtime->shard_count; ++shard_index) {
      native_io_sharded_shard *shard = &runtime->shards[shard_index];
      if (!atomic_load(&shard->bootstrap_done)) {
        status = SALTS_EIO;
        break;
      }
      if (atomic_load(&shard->init_status) != SALTS_OK) {
        status = atomic_load(&shard->init_status);
        break;
      }
    }
  }

  if (status != SALTS_OK) {
    native_io_sharded_cleanup_failed_create(runtime);
    return status;
  }

  atomic_store(&runtime->accepting, 1);
  *out_runtime = runtime;
  return SALTS_OK;
}

static int native_io_sharded_submit_internal(native_io_sharded *runtime, size_t shard_index,
                                             const native_io_sharded_task *task, int blocking) {
  native_io_sharded_shard *shard;
  native_io_sharded_slot *slot = NULL;
  size_t current_shard;
  int status;

  if (runtime == NULL) return SALTS_EINVAL;
  if (task == NULL || task->run == NULL || shard_index >= runtime->shard_count) {
    atomic_fetch_add(&runtime->rejected_tasks, 1u);
    return SALTS_EINVAL;
  }

  status = native_io_sharded_dispatch_begin(runtime);
  if (status != SALTS_OK) {
    atomic_fetch_add(&runtime->rejected_tasks, 1u);
    return status;
  }

  current_shard = salts_coro_executor_current_shard(runtime->executor);
  if (current_shard == shard_index) {
    atomic_fetch_add(&runtime->submitted_tasks, 1u);
    atomic_fetch_add(&runtime->same_shard_direct_tasks, 1u);
    task->run(&runtime->shards[shard_index].context, task->arg);
    atomic_fetch_add(&runtime->completed_tasks, 1u);
    if (task->finalize != NULL) task->finalize(task->arg);
    native_io_sharded_dispatch_end(runtime);
    return SALTS_OK;
  }

  shard = &runtime->shards[shard_index];
  status = native_io_sharded_claim_slot(runtime, shard, task, blocking, &slot);
  if (status != SALTS_OK) {
    native_io_sharded_dispatch_end(runtime);
    atomic_fetch_add(&runtime->rejected_tasks, 1u);
    return status;
  }

  {
    const salts_coro_executor_task_t routed = {
        native_io_sharded_routed_run, native_io_sharded_routed_cancel,
        native_io_sharded_routed_finalize, slot};
    status = blocking ? salts_coro_executor_submit_to(runtime->executor, shard_index, &routed)
                      : salts_coro_executor_try_submit_to(runtime->executor, shard_index, &routed);
  }

  if (status == SALTS_OK) {
    atomic_fetch_add(&runtime->submitted_tasks, 1u);
    atomic_fetch_add(&runtime->queued_dispatches, 1u);
  } else {
    native_io_sharded_release_slot(slot);
  }
  native_io_sharded_dispatch_end(runtime);

  if (status != SALTS_OK) atomic_fetch_add(&runtime->rejected_tasks, 1u);
  return status;
}

int native_io_sharded_submit_to(native_io_sharded *runtime, size_t shard,
                                const native_io_sharded_task *task) {
  return native_io_sharded_submit_internal(runtime, shard, task, 1);
}

int native_io_sharded_try_submit_to(native_io_sharded *runtime, size_t shard,
                                    const native_io_sharded_task *task) {
  return native_io_sharded_submit_internal(runtime, shard, task, 0);
}

int native_io_sharded_shutdown(native_io_sharded *runtime) {
  int first_status = SALTS_OK;
  int all_teardowns_submitted = 1;

  if (runtime == NULL) return SALTS_EINVAL;
  if (native_io_sharded_in_callback(runtime)) return SALTS_EBUSY;

  salts_mutex_lock(&runtime->admission_lock);
  atomic_store(&runtime->accepting, 0);
  native_io_sharded_wake_slot_waiters(runtime);
  while (runtime->inflight_dispatches != 0u)
    salts_cond_wait(&runtime->admission_idle, &runtime->admission_lock);

  for (size_t shard_index = 0u; shard_index < runtime->shard_count; ++shard_index) {
    native_io_sharded_shard *shard = &runtime->shards[shard_index];
    int status;
    if (shard->teardown_submitted) continue;
    if (!atomic_load(&shard->backend_initialized)) {
      atomic_store(&shard->teardown_status, SALTS_OK);
      atomic_store(&shard->teardown_done, 1);
      shard->teardown_submitted = 1;
      continue;
    }

    {
      const salts_coro_executor_task_t task = {
          native_io_sharded_teardown, NULL, NULL, shard};
      status = salts_coro_executor_submit_to(runtime->executor, shard_index, &task);
    }
    if (status == SALTS_OK) {
      shard->teardown_submitted = 1;
    } else {
      all_teardowns_submitted = 0;
      if (first_status == SALTS_OK) first_status = status;
    }
  }

  if (all_teardowns_submitted && !runtime->executor_shutdown) {
    const int status = salts_coro_executor_shutdown(runtime->executor);
    if (status == SALTS_OK)
      runtime->executor_shutdown = 1;
    else if (first_status == SALTS_OK)
      first_status = status;
  }
  salts_mutex_unlock(&runtime->admission_lock);
  return first_status;
}

int native_io_sharded_wait(native_io_sharded *runtime) {
  int status;

  if (runtime == NULL) return SALTS_EINVAL;
  if (native_io_sharded_in_callback(runtime)) return SALTS_EBUSY;
  status = salts_coro_executor_wait(runtime->executor);
  if (status != SALTS_OK) return status;

  for (size_t shard_index = 0u; shard_index < runtime->shard_count; ++shard_index) {
    native_io_sharded_shard *shard = &runtime->shards[shard_index];
    if (!shard->teardown_submitted) continue;
    if (!atomic_load(&shard->teardown_done)) return SALTS_EIO;
    status = atomic_load(&shard->teardown_status);
    if (status != SALTS_OK) return status;
  }
  return SALTS_OK;
}

int native_io_sharded_destroy(native_io_sharded *runtime) {
  int status;

  if (runtime == NULL) return SALTS_EINVAL;
  if (native_io_sharded_in_callback(runtime)) return SALTS_EBUSY;

  status = native_io_sharded_shutdown(runtime);
  if (status != SALTS_OK) return status;
  status = native_io_sharded_wait(runtime);
  if (status != SALTS_OK) return status;
  status = salts_coro_executor_destroy(runtime->executor);
  if (status != SALTS_OK) return status;
  runtime->executor = NULL;
  native_io_sharded_destroy_storage(runtime);
  return SALTS_OK;
}

size_t native_io_sharded_current_shard(const native_io_sharded *runtime) {
  if (runtime == NULL || runtime->executor == NULL) return SIZE_MAX;
  return salts_coro_executor_current_shard(runtime->executor);
}

size_t native_io_sharded_context_shard(const native_io_sharded_context *context) {
  return context == NULL ? SIZE_MAX : context->shard;
}

bool native_io_sharded_endpoint_valid(native_io_sharded_endpoint endpoint) {
  return endpoint.owner_identity != 0u && endpoint.reserved == 0u &&
         native_io_endpoint_valid(endpoint.native_endpoint);
}

bool native_io_sharded_request_valid(native_io_sharded_request request) {
  return request.owner_identity != 0u && request.reserved == 0u &&
         native_io_request_valid(request.native_request);
}

bool native_io_sharded_operation_valid(const native_io_sharded_operation *operation) {
  native_io_operation native_operation;
  if (operation == NULL || !native_io_sharded_endpoint_valid(operation->endpoint)) return false;
  native_operation = native_io_sharded_native_operation(operation);
  return native_io_operation_valid(&native_operation);
}

size_t native_io_sharded_endpoint_owner_shard(native_io_sharded_endpoint endpoint) {
  return native_io_sharded_endpoint_valid(endpoint) ? (size_t)endpoint.owner_shard : SIZE_MAX;
}

size_t native_io_sharded_request_owner_shard(native_io_sharded_request request) {
  return native_io_sharded_request_valid(request) ? (size_t)request.owner_shard : SIZE_MAX;
}

int native_io_sharded_context_attach_socket(native_io_sharded_context *context,
                                            uintptr_t native_socket,
                                            native_io_sharded_endpoint *out_endpoint) {
  native_io_sharded_shard *shard = native_io_sharded_context_owner(context);
  native_io_endpoint endpoint = {0};
  int status;
  if (out_endpoint != NULL) *out_endpoint = (native_io_sharded_endpoint){0};
  if (shard == NULL || out_endpoint == NULL) return SALTS_EINVAL;
  status = native_io_backend_attach_socket(&shard->backend, native_socket, &endpoint);
  if (status == SALTS_OK) {
    out_endpoint->owner_identity = shard->runtime->identity;
    out_endpoint->owner_shard = (uint32_t)shard->index;
    out_endpoint->native_endpoint = endpoint;
  }
  return status;
}

int native_io_sharded_context_attach_pipe(native_io_sharded_context *context,
                                          uintptr_t native_handle, uint32_t flags,
                                          native_io_sharded_endpoint *out_endpoint) {
  native_io_sharded_shard *shard = native_io_sharded_context_owner(context);
  native_io_endpoint endpoint = {0};
  int status;
  if (out_endpoint != NULL) *out_endpoint = (native_io_sharded_endpoint){0};
  if (shard == NULL || out_endpoint == NULL) return SALTS_EINVAL;
  status = native_io_backend_attach_pipe(&shard->backend, native_handle, flags, &endpoint);
  if (status == SALTS_OK) {
    out_endpoint->owner_identity = shard->runtime->identity;
    out_endpoint->owner_shard = (uint32_t)shard->index;
    out_endpoint->native_endpoint = endpoint;
  }
  return status;
}

int native_io_sharded_context_release_socket(native_io_sharded_context *context,
                                             native_io_sharded_endpoint endpoint) {
  native_io_sharded_shard *shard = NULL;
  int status = native_io_sharded_endpoint_access(context, endpoint, &shard);
  if (status != SALTS_OK) return status;
  return native_io_backend_release_socket(&shard->backend, endpoint.native_endpoint);
}

int native_io_sharded_context_release_pipe(native_io_sharded_context *context,
                                           native_io_sharded_endpoint endpoint) {
  native_io_sharded_shard *shard = NULL;
  int status = native_io_sharded_endpoint_access(context, endpoint, &shard);
  if (status != SALTS_OK) return status;
  return native_io_backend_release_pipe(&shard->backend, endpoint.native_endpoint);
}

static int native_io_sharded_context_admit(native_io_sharded_context *context,
                                           const native_io_sharded_operation *operation,
                                           const native_io_sharded_ownership *ownership,
                                           native_io_sharded_request *out_request, bool prepared) {
  native_io_sharded_shard *shard = NULL;
  native_io_operation native_operation;
  native_io_request request = {0};
  int status;
  if (out_request != NULL) *out_request = (native_io_sharded_request){0};
  if (operation == NULL || out_request == NULL || !native_io_sharded_operation_valid(operation) ||
      (ownership != NULL && ownership->finalize == NULL))
    return SALTS_EINVAL;
  status = native_io_sharded_endpoint_access(context, operation->endpoint, &shard);
  if (status != SALTS_OK) return status;
  native_operation = native_io_sharded_native_operation(operation);
  status = prepared ? native_io_backend_prepare(&shard->backend, &native_operation, &request)
                    : native_io_backend_submit(&shard->backend, &native_operation, &request);
  if (status == SALTS_OK) {
    out_request->owner_identity = shard->runtime->identity;
    out_request->owner_shard = (uint32_t)shard->index;
    out_request->native_request = request;
    if (ownership != NULL) {
      native_io_sharded_request_ownership *record =
          &shard->request_ownerships[request.slot - 1u];
      record->ownership = *ownership;
      record->generation = request.generation;
      record->active = 1;
    }
  }
  return status;
}

int native_io_sharded_context_submit(native_io_sharded_context *context,
                                     const native_io_sharded_operation *operation,
                                     native_io_sharded_request *out_request) {
  return native_io_sharded_context_admit(context, operation, NULL, out_request, false);
}

int native_io_sharded_context_submit_owned(native_io_sharded_context *context,
                                           const native_io_sharded_operation *operation,
                                           const native_io_sharded_ownership *ownership,
                                           native_io_sharded_request *out_request) {
  return native_io_sharded_context_admit(context, operation, ownership, out_request, false);
}

int native_io_sharded_context_prepare(native_io_sharded_context *context,
                                      const native_io_sharded_operation *operation,
                                      native_io_sharded_request *out_request) {
  return native_io_sharded_context_admit(context, operation, NULL, out_request, true);
}

int native_io_sharded_context_prepare_owned(native_io_sharded_context *context,
                                            const native_io_sharded_operation *operation,
                                            const native_io_sharded_ownership *ownership,
                                            native_io_sharded_request *out_request) {
  return native_io_sharded_context_admit(context, operation, ownership, out_request, true);
}

int native_io_sharded_context_flush(native_io_sharded_context *context) {
  native_io_sharded_shard *shard = native_io_sharded_context_owner(context);
  return shard == NULL ? SALTS_EINVAL : native_io_backend_flush(&shard->backend);
}

int native_io_sharded_context_cancel(native_io_sharded_context *context,
                                     native_io_sharded_request request) {
  native_io_sharded_shard *shard = NULL;
  int status = native_io_sharded_request_access(context, request, &shard);
  if (status != SALTS_OK) return status;
  return native_io_backend_cancel(&shard->backend, request.native_request);
}

int native_io_sharded_context_observe(native_io_sharded_context *context,
                                      native_io_sharded_completion *events,
                                      size_t event_capacity, uint32_t timeout_ms,
                                      size_t *out_count) {
  native_io_sharded_shard *shard = native_io_sharded_context_owner(context);
  size_t count = 0u;
  size_t limit;
  int status;
  if (out_count != NULL) *out_count = 0u;
  if (shard == NULL || events == NULL || event_capacity == 0u || out_count == NULL ||
      shard->completion_scratch == NULL || shard->completion_capacity == 0u)
    return SALTS_EINVAL;
  if (shard->observe_active) return SALTS_EBUSY;
  shard->observe_active = 1;
  limit = event_capacity < shard->completion_capacity ? event_capacity : shard->completion_capacity;
  status = native_io_backend_observe(&shard->backend, shard->completion_scratch, limit,
                                     timeout_ms, &count);
  if (status != SALTS_OK) {
    shard->observe_active = 0;
    return status;
  }
  /*
   * Detach ownership for the complete dequeued batch before running any
   * terminal callback. Raw NativeIO may make every slot in this batch reusable
   * before observe returns, so a callback that immediately submits new work
   * must not overwrite an ownership record belonging to a later completion in
   * the same batch.
   */
  for (size_t index = 0u; index < count; ++index) {
    const native_io_completion *native_event = &shard->completion_scratch[index];
    native_io_sharded_completion *event = &events[index];
    native_io_sharded_ownership_settlement *settlement =
        &shard->ownership_settlements[index];
    memset(event, 0, sizeof(*event));
    memset(settlement, 0, sizeof(*settlement));
    event->request.owner_identity = shard->runtime->identity;
    event->request.owner_shard = (uint32_t)shard->index;
    event->request.native_request = native_event->request;
    event->endpoint.owner_identity = shard->runtime->identity;
    event->endpoint.owner_shard = (uint32_t)shard->index;
    event->endpoint.native_endpoint = native_event->endpoint;
    event->kind = native_event->kind;
    event->bytes = native_event->bytes;
    event->status = native_event->status;
    event->native_status = native_event->native_status;
    event->user_data = native_event->user_data;
    event->address_length = native_event->address_length;

    if (native_event->request.slot != 0u &&
        native_event->request.slot <= shard->request_ownership_capacity) {
      native_io_sharded_request_ownership *record =
          &shard->request_ownerships[native_event->request.slot - 1u];
      if (record->active && record->generation == native_event->request.generation) {
        settlement->ownership = record->ownership;
        settlement->active = 1;
        memset(record, 0, sizeof(*record));
      }
    }
  }

  for (size_t index = 0u; index < count; ++index) {
    native_io_sharded_ownership_settlement *settlement =
        &shard->ownership_settlements[index];
    if (settlement->active) {
      native_io_sharded_ownership ownership = settlement->ownership;
      memset(settlement, 0, sizeof(*settlement));
      if (ownership.terminal != NULL) ownership.terminal(context, &events[index], ownership.arg);
      ownership.finalize(ownership.arg);
    }
  }
  shard->observe_active = 0;
  *out_count = count;
  return SALTS_OK;
}

bool native_io_sharded_get_stats(const native_io_sharded *runtime,
                                 native_io_sharded_stats *out_stats) {
  native_io_sharded_stats stats = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;

  if (runtime == NULL || out_stats == NULL ||
      out_stats->abi_version != NATIVE_IO_SHARDED_STATS_ABI_V1 ||
      out_stats->struct_size != sizeof(*out_stats))
    return false;

  stats.shard_count = runtime->shard_count;
  stats.queue_capacity_per_shard = runtime->queue_capacity_per_shard;
  stats.command_slot_capacity_per_shard = runtime->command_slot_capacity_per_shard;
  stats.submitted_tasks = atomic_load(&runtime->submitted_tasks);
  stats.same_shard_direct_tasks = atomic_load(&runtime->same_shard_direct_tasks);
  stats.queued_dispatches = atomic_load(&runtime->queued_dispatches);
  stats.completed_tasks = atomic_load(&runtime->completed_tasks);
  stats.cancelled_tasks = atomic_load(&runtime->cancelled_tasks);
  stats.rejected_tasks = atomic_load(&runtime->rejected_tasks);
  stats.active_command_slots = atomic_load(&runtime->active_command_slots);
  stats.peak_command_slots = atomic_load(&runtime->peak_command_slots);
  stats.accepting = atomic_load(&runtime->accepting) != 0;
  *out_stats = stats;
  return true;
}
