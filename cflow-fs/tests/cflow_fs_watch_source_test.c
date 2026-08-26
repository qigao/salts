#include <cflow/fs_watch_source.h>
#include <tinytest.h>
#include <turbo/error_codes.h>
#include <turbo/thread.h>
#include <turbo_fs.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct watch_value {
  cflow_fs_watch_event_kind kind;
  cflow_fs_watch_entry_type entry_type;
  char path[128];
  char old_path[128];
} watch_value;

static const cmeta_type_traits watch_value_traits = {.flags = CMETA_TRAIT_TRIVIAL_COPY |
                                                              CMETA_TRAIT_TRIVIAL_DESTROY};

static const cmeta_type_desc watch_value_type = {.name = "watch_value",
                                                 .size = sizeof(watch_value),
                                                 .align = _Alignof(watch_value),
                                                 .kind = CMETA_T_OBJECT,
                                                 .traits = &watch_value_traits};

static bool encode_watch_value(void *user, const cflow_fs_watch_event *event, void *out_value) {
  watch_value *value = (watch_value *)out_value;
  (void)user;
  if (event == NULL || value == NULL) return false;
  value->kind = event->kind;
  value->entry_type = event->entry_type;
  if (event->path != NULL) strncpy(value->path, event->path, sizeof(value->path) - 1u);
  if (event->old_path != NULL)
    strncpy(value->old_path, event->old_path, sizeof(value->old_path) - 1u);
  return true;
}

static bool reject_watch_value(void *user, const cflow_fs_watch_event *event, void *out_value) {
  (void)user;
  (void)event;
  (void)out_value;
  return false;
}

static cflow_fs_watch_source_config source_config(void) {
  cflow_fs_watch_source_config config = {
      .recursive = false,
      .event_capacity = 8u,
      .watch_capacity = 1u,
      .path_capacity = 128u,
      .native_buffer_capacity = 4096u,
      .name = "test-watch",
      .output_type = &watch_value_type,
      .encode = encode_watch_value,
      .encode_user = NULL,
  };
  return config;
}

typedef struct wake_probe {
  atomic_size_t count;
} wake_probe;

typedef struct blocking_wake_probe {
  turbo_mutex_t lock;
  turbo_cond_t changed;
  bool entered;
  bool released;
  bool cancel_started;
  bool cancel_returned;
} blocking_wake_probe;

typedef struct cancel_context {
  cflow_source *source;
  blocking_wake_probe *probe;
} cancel_context;

typedef struct reentrant_close_probe {
  cflow_source *source;
  cflow_fs_watch_source_owner *owner;
  atomic_bool completed;
  int close_status;
} reentrant_close_probe;

static void count_wake(void *user) {
  wake_probe *probe = (wake_probe *)user;
  if (probe != NULL) (void)atomic_fetch_add(&probe->count, 1u);
}

static void blocking_wake(void *user) {
  blocking_wake_probe *probe = (blocking_wake_probe *)user;
  if (probe == NULL) return;
  turbo_mutex_lock(&probe->lock);
  probe->entered = true;
  turbo_cond_broadcast(&probe->changed);
  while (!probe->released)
    turbo_cond_wait(&probe->changed, &probe->lock);
  turbo_mutex_unlock(&probe->lock);
}

static void cancel_source(void *user) {
  cancel_context *context = (cancel_context *)user;
  turbo_mutex_lock(&context->probe->lock);
  context->probe->cancel_started = true;
  turbo_cond_broadcast(&context->probe->changed);
  turbo_mutex_unlock(&context->probe->lock);
  cflow_source_cancel(context->source);
  turbo_mutex_lock(&context->probe->lock);
  context->probe->cancel_returned = true;
  turbo_cond_broadcast(&context->probe->changed);
  turbo_mutex_unlock(&context->probe->lock);
}

static void destroy_source_and_close_owner(void *user) {
  reentrant_close_probe *probe = (reentrant_close_probe *)user;
  if (probe == NULL) return;
  cflow_source_destroy(probe->source);
  probe->close_status = cflow_fs_watch_source_owner_close(probe->owner);
  atomic_store(&probe->completed, true);
}

static int close_owner(cflow_fs_watch_source_owner *owner) {
  size_t attempts = 0u;
  int status;
  do {
    status = cflow_fs_watch_source_owner_close(owner);
    if (status == TURBO_EBUSY) turbo_sleep_ms(1u);
  } while (status == TURBO_EBUSY && ++attempts < 5000u);
  return status;
}

typedef struct run_probe {
  watch_value value;
  size_t values;
  const char *error;
} run_probe;

static bool run_value(void *user, const cmeta_type_desc *type, const void *value) {
  run_probe *probe = (run_probe *)user;
  if (probe == NULL || value == NULL || !cmeta_type_equal(type, &watch_value_type)) return false;
  probe->value = *(const watch_value *)value;
  ++probe->values;
  return true;
}

static void run_error(void *user, const char *message) {
  run_probe *probe = (run_probe *)user;
  if (probe != NULL) probe->error = message;
}

spec("CFlow filesystem watch Source") {
  it("rejects invalid construction without taking either output") {
    cflow_source source = {0};
    cflow_fs_watch_source_owner owner = {0};
    cflow_fs_watch_source_config config = source_config();

    check_equal(cflow_fs_watch_source_open(NULL, &owner, ".", &config), TURBO_EINVAL);
    check_equal(cflow_fs_watch_source_open(&source, NULL, ".", &config), TURBO_EINVAL);
    check_equal(cflow_fs_watch_source_open(&source, &owner, NULL, &config), TURBO_EINVAL);
    config.encode = NULL;
    check_equal(cflow_fs_watch_source_open(&source, &owner, ".", &config), TURBO_EINVAL);
    check_equal(cflow_fs_watch_source_owner_acknowledge_rescan(NULL), TURBO_EINVAL);
    check_false(cflow_fs_watch_source_owner_get_stats(NULL, NULL));
    check_equal(cflow_fs_watch_source_owner_close(NULL), TURBO_EINVAL);
    check_equal(cflow_fs_watch_source_owner_close(&owner), TURBO_OK);
    check_false(cflow_source_valid(&source));
    check_null(owner.impl);
  }

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
  it("preserves occupied outputs when construction fails") {
    char *root = tt_make_temp_dir("cflow-watch-source-occupied-");
    cflow_source source = {0};
    cflow_fs_watch_source_owner owner = {0};
    cflow_fs_watch_source_config config = source_config();
    cflow_source saved_source;
    void *saved_owner;

    check_not_null(root);
    check_equal(cflow_fs_watch_source_open(&source, &owner, root, &config), TURBO_OK);
    saved_source = source;
    saved_owner = owner.impl;

    check_equal(cflow_fs_watch_source_open(&source, &owner, NULL, &config), TURBO_EINVAL);
    check_true(source.self == saved_source.self);
    check_true(owner.impl == saved_owner);

    if (!cflow_source_valid(&source)) source = saved_source;
    if (owner.impl == NULL) owner.impl = saved_owner;
    cflow_source_destroy(&source);
    check_equal(close_owner(&owner), TURBO_OK);
    check_equal(tt_remove_tree(root), TURBO_OK);
    free(root);
  }

  it("wakes an armed Source and copies one native event into its value") {
    char *root = tt_make_temp_dir("cflow-watch-source-");
    char path[1024];
    cflow_source source = {0};
    cflow_fs_watch_source_owner owner = {0};
    cflow_fs_watch_source_config config = source_config();
    cflow_resume_ctx resume = {0};
    cflow_step step;
    watch_value value = {0};
    wake_probe wake;
    size_t attempts = 0u;

    atomic_init(&wake.count, 0u);
    check_not_null(root);
    check_equal(turbo_fs_path_join(path, sizeof(path), root, "one.txt"), TURBO_OK);
    check_equal(cflow_fs_watch_source_open(&source, &owner, root, &config), TURBO_OK);

    step = cflow_source_resume(&source, &resume, &value);
    check_equal(step.kind, CFLOW_STEP_WAIT);
    check_true(cflow_waitable_arm(&step.waitable, (cflow_waker){count_wake, &wake}));
    check_equal(tt_write_file(path, "x", 1u), TURBO_OK);
    while (atomic_load(&wake.count) == 0u && attempts++ < 5000u)
      turbo_sleep_ms(1u);
    check_equal(atomic_load(&wake.count), (size_t)1u);

    step = cflow_source_resume(&source, &resume, &value);
    check_equal(step.kind, CFLOW_STEP_VALUE);
    check_true(value.kind == CFLOW_FS_WATCH_CREATED || value.kind == CFLOW_FS_WATCH_MODIFIED);
    check_equal(value.path, "one.txt");

    cflow_source_destroy(&source);
    check_equal(close_owner(&owner), TURBO_OK);
    check_null(owner.impl);
    check_equal(tt_remove_tree(root), TURBO_OK);
    free(root);
  }

  it("wakes immediately when publication wins the race before arm") {
    char *root = tt_make_temp_dir("cflow-watch-source-before-arm-");
    char path[1024];
    cflow_source source = {0};
    cflow_fs_watch_source_owner owner = {0};
    cflow_fs_watch_source_config config = source_config();
    cflow_resume_ctx resume = {0};
    cflow_fs_watch_stats stats = {0};
    cflow_step step;
    watch_value value = {0};
    wake_probe wake;
    size_t attempts = 0u;

    atomic_init(&wake.count, 0u);
    check_not_null(root);
    check_equal(turbo_fs_path_join(path, sizeof(path), root, "early.txt"), TURBO_OK);
    check_equal(cflow_fs_watch_source_open(&source, &owner, root, &config), TURBO_OK);
    step = cflow_source_resume(&source, &resume, &value);
    check_equal(step.kind, CFLOW_STEP_WAIT);
    check_equal(tt_write_file(path, "x", 1u), TURBO_OK);
    while (attempts++ < 5000u) {
      check_true(cflow_fs_watch_source_owner_get_stats(&owner, &stats));
      if (stats.queued != 0u) break;
      turbo_sleep_ms(1u);
    }
    check_greater(stats.queued, (size_t)0u);

    check_true(cflow_waitable_arm(&step.waitable, (cflow_waker){count_wake, &wake}));
    check_equal(atomic_load(&wake.count), (size_t)1u);
    step = cflow_source_resume(&source, &resume, &value);
    check_equal(step.kind, CFLOW_STEP_VALUE);
    check_equal(value.path, "early.txt");

    cflow_source_destroy(&source);
    check_equal(close_owner(&owner), TURBO_OK);
    check_equal(tt_remove_tree(root), TURBO_OK);
    free(root);
  }

  it("keeps cleanup ownership until the Source is destroyed") {
    char *root = tt_make_temp_dir("cflow-watch-source-owner-");
    cflow_source source = {0};
    cflow_fs_watch_source_owner owner = {0};
    cflow_fs_watch_source_config config = source_config();

    check_not_null(root);
    check_equal(cflow_fs_watch_source_open(&source, &owner, root, &config), TURBO_OK);
    check_equal(cflow_fs_watch_source_owner_close(&owner), TURBO_EBUSY);
    check_not_null(owner.impl);

    cflow_source_destroy(&source);
    check_equal(close_owner(&owner), TURBO_OK);
    check_null(owner.impl);
    check_equal(tt_remove_tree(root), TURBO_OK);
    free(root);
  }

  it("wakes a waiter once when Source cancellation becomes terminal") {
    char *root = tt_make_temp_dir("cflow-watch-source-cancel-");
    cflow_source source = {0};
    cflow_fs_watch_source_owner owner = {0};
    cflow_fs_watch_source_config config = source_config();
    cflow_resume_ctx resume = {0};
    cflow_step step;
    watch_value value = {0};
    wake_probe wake;

    atomic_init(&wake.count, 0u);
    check_not_null(root);
    check_equal(cflow_fs_watch_source_open(&source, &owner, root, &config), TURBO_OK);
    step = cflow_source_resume(&source, &resume, &value);
    check_equal(step.kind, CFLOW_STEP_WAIT);
    check_true(cflow_waitable_arm(&step.waitable, (cflow_waker){count_wake, &wake}));

    cflow_source_cancel(&source);
    check_equal(atomic_load(&wake.count), (size_t)1u);
    step = cflow_source_resume(&source, &resume, &value);
    check_equal(step.kind, CFLOW_STEP_DONE);
    cflow_source_cancel(&source);
    check_equal(atomic_load(&wake.count), (size_t)1u);

    cflow_source_destroy(&source);
    check_equal(close_owner(&owner), TURBO_OK);
    check_equal(tt_remove_tree(root), TURBO_OK);
    free(root);
  }

  it("keeps cancellation quiescent while an extracted waker is running") {
    enum {
      WAIT_SLICE_NS = 10 * 1000 * 1000,
      WAIT_LIMIT = 500,
      CANCEL_OBSERVATION_NS = 20 * 1000 * 1000
    };
    char *root = tt_make_temp_dir("cflow-watch-source-wake-close-");
    char path[1024];
    cflow_source source = {0};
    cflow_fs_watch_source_owner owner = {0};
    cflow_fs_watch_source_config config = source_config();
    cflow_resume_ctx resume = {0};
    cflow_step step;
    watch_value value = {0};
    blocking_wake_probe wake = {0};
    cancel_context context = {&source, &wake};
    turbo_thread_t canceller = {0};
    size_t waits = 0u;

    turbo_mutex_init(&wake.lock);
    turbo_cond_init(&wake.changed);
    check_not_null(wake.lock);
    check_not_null(wake.changed);
    check_not_null(root);
    check_equal(turbo_fs_path_join(path, sizeof(path), root, "blocked.txt"), TURBO_OK);
    check_equal(cflow_fs_watch_source_open(&source, &owner, root, &config), TURBO_OK);
    step = cflow_source_resume(&source, &resume, &value);
    check_equal(step.kind, CFLOW_STEP_WAIT);
    check_true(cflow_waitable_arm(&step.waitable, (cflow_waker){blocking_wake, &wake}));
    check_equal(tt_write_file(path, "x", 1u), TURBO_OK);

    turbo_mutex_lock(&wake.lock);
    while (!wake.entered && waits++ < WAIT_LIMIT)
      (void)turbo_cond_timedwait(&wake.changed, &wake.lock, WAIT_SLICE_NS);
    if (!wake.entered) {
      wake.released = true;
      turbo_cond_broadcast(&wake.changed);
    }
    check_true(wake.entered);
    turbo_mutex_unlock(&wake.lock);
    check_equal(turbo_thread_create(&canceller, cancel_source, &context), TURBO_OK);
    turbo_mutex_lock(&wake.lock);
    waits = 0u;
    while (!wake.cancel_started && waits++ < WAIT_LIMIT)
      (void)turbo_cond_timedwait(&wake.changed, &wake.lock, WAIT_SLICE_NS);
    check_true(wake.cancel_started);
    if (!wake.cancel_returned)
      (void)turbo_cond_timedwait(&wake.changed, &wake.lock, CANCEL_OBSERVATION_NS);
    check_false(wake.cancel_returned);
    wake.released = true;
    turbo_cond_broadcast(&wake.changed);
    turbo_mutex_unlock(&wake.lock);

    check_equal(turbo_thread_join(&canceller), TURBO_OK);
    turbo_thread_destroy(&canceller);
    check_true(wake.cancel_returned);
    cflow_source_destroy(&source);
    check_equal(close_owner(&owner), TURBO_OK);
    turbo_cond_destroy(&wake.changed);
    turbo_mutex_destroy(&wake.lock);
    check_equal(tt_remove_tree(root), TURBO_OK);
    free(root);
  }

  it("keeps the owner busy during reentrant Source destruction") {
    char *root = tt_make_temp_dir("cflow-watch-source-reentrant-close-");
    char path[1024];
    cflow_source source = {0};
    cflow_fs_watch_source_owner owner = {0};
    cflow_fs_watch_source_config config = source_config();
    cflow_resume_ctx resume = {0};
    cflow_step step;
    watch_value value = {0};
    reentrant_close_probe probe = {&source, &owner};
    size_t attempts = 0u;

    atomic_init(&probe.completed, false);
    check_not_null(root);
    check_equal(turbo_fs_path_join(path, sizeof(path), root, "reentrant.txt"), TURBO_OK);
    check_equal(cflow_fs_watch_source_open(&source, &owner, root, &config), TURBO_OK);
    step = cflow_source_resume(&source, &resume, &value);
    check_equal(step.kind, CFLOW_STEP_WAIT);
    check_true(
        cflow_waitable_arm(&step.waitable, (cflow_waker){destroy_source_and_close_owner, &probe}));
    check_equal(tt_write_file(path, "x", 1u), TURBO_OK);
    while (!atomic_load(&probe.completed) && attempts++ < 5000u)
      turbo_sleep_ms(1u);

    check_true(atomic_load(&probe.completed));
    check_equal(probe.close_status, TURBO_EBUSY);
    check_not_null(owner.impl);
    check_equal(close_owner(&owner), TURBO_OK);
    check_equal(tt_remove_tree(root), TURBO_OK);
    free(root);
  }

  it("turns encoder rejection into a Source error") {
    char *root = tt_make_temp_dir("cflow-watch-source-encoder-");
    char path[1024];
    cflow_source source = {0};
    cflow_fs_watch_source_owner owner = {0};
    cflow_fs_watch_source_config config = source_config();
    cflow_resume_ctx resume = {0};
    cflow_step step;
    watch_value value = {0};
    size_t attempts = 0u;

    config.encode = reject_watch_value;
    check_not_null(root);
    check_equal(turbo_fs_path_join(path, sizeof(path), root, "bad.txt"), TURBO_OK);
    check_equal(cflow_fs_watch_source_open(&source, &owner, root, &config), TURBO_OK);
    check_equal(tt_write_file(path, "x", 1u), TURBO_OK);
    do {
      step = cflow_source_resume(&source, &resume, &value);
      if (step.kind == CFLOW_STEP_WAIT) turbo_sleep_ms(1u);
    } while (step.kind == CFLOW_STEP_WAIT && attempts++ < 5000u);
    check_equal(step.kind, CFLOW_STEP_ERROR);
    check_equal(step.error, "filesystem watch encoder failed");

    cflow_source_destroy(&source);
    check_equal(close_owner(&owner), TURBO_OK);
    check_equal(tt_remove_tree(root), TURBO_OK);
    free(root);
  }

  it("executes native events through Graph and Run demand") {
    char *root = tt_make_temp_dir("cflow-watch-source-run-");
    char path[1024];
    cflow_source source = {0};
    cflow_fs_watch_source_owner owner = {0};
    cflow_fs_watch_source_config config = source_config();
    cflow_graph graph = {0};
    cflow_scheduler scheduler = {0};
    cflow_run run = {0};
    run_probe probe = {0};
    cflow_sink_callbacks callbacks = {
        .on_value = run_value,
        .on_error = run_error,
        .on_done = NULL,
        .user = &probe,
    };
    cflow_sink sink = cflow_sink_from_callbacks(&callbacks);
    size_t attempts = 0u;

    check_not_null(root);
    check_equal(turbo_fs_path_join(path, sizeof(path), root, "run.txt"), TURBO_OK);
    check_equal(cflow_fs_watch_source_open(&source, &owner, root, &config), TURBO_OK);
    cflow_graph_init(&graph, &watch_value_type);
    check_true(cflow_scheduler_test_init(&scheduler));
    check_true(cflow_run_open(&run, &graph, &source, &scheduler, &sink));
    check_false(cflow_source_valid(&source));
    check_true(cflow_run_request(&run, 1u));
    (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
    check_equal(tt_write_file(path, "x", 1u), TURBO_OK);
    while (probe.values == 0u && probe.error == NULL && attempts++ < 5000u) {
      (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
      if (probe.values == 0u) turbo_sleep_ms(1u);
    }

    check_null(probe.error);
    check_equal(probe.values, (size_t)1u);
    check_equal(probe.value.path, "run.txt");
    cflow_run_close(&run);
    check_equal(close_owner(&owner), TURBO_OK);
    cflow_scheduler_destroy(&scheduler);
    cflow_graph_destroy(&graph);
    check_equal(tt_remove_tree(root), TURBO_OK);
    free(root);
  }

  it("propagates encoder rejection through Run") {
    char *root = tt_make_temp_dir("cflow-watch-source-run-error-");
    char path[1024];
    cflow_source source = {0};
    cflow_fs_watch_source_owner owner = {0};
    cflow_fs_watch_source_config config = source_config();
    cflow_graph graph = {0};
    cflow_scheduler scheduler = {0};
    cflow_run run = {0};
    run_probe probe = {0};
    cflow_sink_callbacks callbacks = {
        .on_value = run_value,
        .on_error = run_error,
        .on_done = NULL,
        .user = &probe,
    };
    cflow_sink sink = cflow_sink_from_callbacks(&callbacks);
    size_t attempts = 0u;

    config.encode = reject_watch_value;
    check_not_null(root);
    check_equal(turbo_fs_path_join(path, sizeof(path), root, "run-error.txt"), TURBO_OK);
    check_equal(cflow_fs_watch_source_open(&source, &owner, root, &config), TURBO_OK);
    cflow_graph_init(&graph, &watch_value_type);
    check_true(cflow_scheduler_test_init(&scheduler));
    check_true(cflow_run_open(&run, &graph, &source, &scheduler, &sink));
    check_true(cflow_run_request(&run, 1u));
    (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
    check_equal(tt_write_file(path, "x", 1u), TURBO_OK);
    while (probe.error == NULL && attempts++ < 5000u) {
      (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
      if (probe.error == NULL) turbo_sleep_ms(1u);
    }

    check_equal(probe.error, "filesystem watch encoder failed");
    check_equal(cflow_run_error(&run), "filesystem watch encoder failed");
    check_equal(probe.values, (size_t)0u);
    cflow_run_close(&run);
    check_equal(close_owner(&owner), TURBO_OK);
    cflow_scheduler_destroy(&scheduler);
    cflow_graph_destroy(&graph);
    check_equal(tt_remove_tree(root), TURBO_OK);
    free(root);
  }

  it("exposes rescan acknowledgement through the cleanup owner") {
    char *root = tt_make_temp_dir("cflow-watch-source-rescan-");
    char first[1024];
    char second[1024];
    cflow_source source = {0};
    cflow_fs_watch_source_owner owner = {0};
    cflow_fs_watch_source_config config = source_config();
    cflow_resume_ctx resume = {0};
    cflow_fs_watch_stats stats = {0};
    watch_value value = {0};
    bool saw_rescan = false;
    size_t attempts = 0u;

    config.event_capacity = 1u;
    check_not_null(root);
    check_equal(turbo_fs_path_join(first, sizeof(first), root, "one.txt"), TURBO_OK);
    check_equal(turbo_fs_path_join(second, sizeof(second), root, "two.txt"), TURBO_OK);
    check_equal(cflow_fs_watch_source_open(&source, &owner, root, &config), TURBO_OK);
    check_equal(tt_write_file(first, "1", 1u), TURBO_OK);
    check_equal(tt_write_file(second, "2", 1u), TURBO_OK);
    while (attempts++ < 5000u) {
      check_true(cflow_fs_watch_source_owner_get_stats(&owner, &stats));
      if (stats.awaiting_rescan) break;
      turbo_sleep_ms(1u);
    }
    check_true(stats.awaiting_rescan);
    for (attempts = 0u; attempts < 4u && !saw_rescan; ++attempts) {
      const cflow_step step = cflow_source_resume(&source, &resume, &value);
      check_true(step.kind == CFLOW_STEP_VALUE || step.kind == CFLOW_STEP_VALUE_AND_DONE);
      saw_rescan = value.kind == CFLOW_FS_WATCH_RESCAN_REQUIRED;
    }
    check_true(saw_rescan);
    check_equal(cflow_fs_watch_source_owner_acknowledge_rescan(&owner), TURBO_OK);

    cflow_source_destroy(&source);
    check_equal(close_owner(&owner), TURBO_OK);
    check_equal(tt_remove_tree(root), TURBO_OK);
    free(root);
  }

  it("wakes an armed Source when acknowledgement queues another rescan") {
    char *root = tt_make_temp_dir("cflow-watch-source-rescan-wake-");
    char first[1024];
    char second[1024];
    char third[1024];
    cflow_source source = {0};
    cflow_fs_watch_source_owner owner = {0};
    cflow_fs_watch_source_config config = source_config();
    cflow_resume_ctx resume = {0};
    cflow_fs_watch_stats stats = {0};
    cflow_fs_watch_stats delivered_stats = {0};
    cflow_step step = {0};
    watch_value value = {0};
    wake_probe wake;
    bool saw_rescan = false;
    size_t attempts = 0u;

    atomic_init(&wake.count, 0u);
    config.event_capacity = 1u;
    check_not_null(root);
    check_equal(turbo_fs_path_join(first, sizeof(first), root, "one.txt"), TURBO_OK);
    check_equal(turbo_fs_path_join(second, sizeof(second), root, "two.txt"), TURBO_OK);
    check_equal(turbo_fs_path_join(third, sizeof(third), root, "three.txt"), TURBO_OK);
    check_equal(cflow_fs_watch_source_open(&source, &owner, root, &config), TURBO_OK);
    check_equal(tt_write_file(first, "1", 1u), TURBO_OK);
    check_equal(tt_write_file(second, "2", 1u), TURBO_OK);
    while (attempts++ < 5000u) {
      check_true(cflow_fs_watch_source_owner_get_stats(&owner, &stats));
      if (stats.awaiting_rescan) break;
      turbo_sleep_ms(1u);
    }
    check_true(stats.awaiting_rescan);
    for (attempts = 0u; attempts < 4u && !saw_rescan; ++attempts) {
      step = cflow_source_resume(&source, &resume, &value);
      check_true(step.kind == CFLOW_STEP_VALUE || step.kind == CFLOW_STEP_VALUE_AND_DONE);
      saw_rescan = value.kind == CFLOW_FS_WATCH_RESCAN_REQUIRED;
    }
    check_true(saw_rescan);
    check_true(cflow_fs_watch_source_owner_get_stats(&owner, &delivered_stats));

    step = cflow_source_resume(&source, &resume, &value);
    check_equal(step.kind, CFLOW_STEP_WAIT);
    check_true(cflow_waitable_arm(&step.waitable, (cflow_waker){count_wake, &wake}));
    check_equal(tt_write_file(third, "3", 1u), TURBO_OK);
    attempts = 0u;
    while (attempts++ < 5000u) {
      check_true(cflow_fs_watch_source_owner_get_stats(&owner, &stats));
      if (stats.suppressed > delivered_stats.suppressed) break;
      turbo_sleep_ms(1u);
    }
    check_greater(stats.suppressed, delivered_stats.suppressed);
    check_equal(atomic_load(&wake.count), (size_t)0u);

    check_equal(cflow_fs_watch_source_owner_acknowledge_rescan(&owner), TURBO_OK);
    check_equal(atomic_load(&wake.count), (size_t)1u);
    step = cflow_source_resume(&source, &resume, &value);
    check_true(step.kind == CFLOW_STEP_VALUE || step.kind == CFLOW_STEP_VALUE_AND_DONE);
    check_equal(value.kind, CFLOW_FS_WATCH_RESCAN_REQUIRED);

    cflow_source_destroy(&source);
    check_equal(close_owner(&owner), TURBO_OK);
    check_equal(tt_remove_tree(root), TURBO_OK);
    free(root);
  }

  it("finishes after the native backend reports the watched root gone") {
    char *root = tt_make_temp_dir("cflow-watch-source-root-");
    cflow_source source = {0};
    cflow_fs_watch_source_owner owner = {0};
    cflow_fs_watch_source_config config = source_config();
    cflow_resume_ctx resume = {0};
    watch_value value = {0};
    wake_probe wake;
    bool saw_root_changed = false;
    bool saw_done = false;
    size_t attempts = 0u;

    atomic_init(&wake.count, 0u);
    check_not_null(root);
    check_equal(cflow_fs_watch_source_open(&source, &owner, root, &config), TURBO_OK);
    check_equal(tt_remove_tree(root), TURBO_OK);

    while (!saw_done && attempts++ < 5000u) {
      cflow_step step = cflow_source_resume(&source, &resume, &value);
      if (step.kind == CFLOW_STEP_VALUE || step.kind == CFLOW_STEP_VALUE_AND_DONE) {
        if (value.kind == CFLOW_FS_WATCH_ROOT_CHANGED) saw_root_changed = true;
        if (step.kind == CFLOW_STEP_VALUE_AND_DONE) saw_done = true;
        continue;
      }
      if (step.kind == CFLOW_STEP_DONE) {
        saw_done = true;
        continue;
      }
      check_equal(step.kind, CFLOW_STEP_WAIT);
      atomic_store(&wake.count, 0u);
      check_true(cflow_waitable_arm(&step.waitable, (cflow_waker){count_wake, &wake}));
      while (atomic_load(&wake.count) == 0u && attempts++ < 5000u)
        turbo_sleep_ms(1u);
    }

    check_true(saw_root_changed);
    check_true(saw_done);
    cflow_source_destroy(&source);
    check_equal(close_owner(&owner), TURBO_OK);
    free(root);
  }
#endif
}
