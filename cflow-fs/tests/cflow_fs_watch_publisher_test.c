#include <cflow/fs_watch_publisher.h>
#include <tinytest.h>
#include <salts/error_codes.h>
#include <salts/thread.h>
#include <salts_fs.h>

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

static cflow_fs_watch_publisher_config source_config(void) {
  cflow_fs_watch_publisher_config config = {
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
  salts_mutex_t lock;
  salts_cond_t changed;
  bool entered;
  bool released;
  bool cancel_started;
  bool cancel_returned;
} blocking_wake_probe;

typedef struct cancel_context {
  cflow_publisher *source;
  blocking_wake_probe *probe;
} cancel_context;

typedef struct reentrant_close_probe {
  cflow_publisher *source;
  cflow_fs_watch_publisher_owner *owner;
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
  salts_mutex_lock(&probe->lock);
  probe->entered = true;
  salts_cond_broadcast(&probe->changed);
  while (!probe->released)
    salts_cond_wait(&probe->changed, &probe->lock);
  salts_mutex_unlock(&probe->lock);
}

static void cancel_source(void *user) {
  cancel_context *context = (cancel_context *)user;
  salts_mutex_lock(&context->probe->lock);
  context->probe->cancel_started = true;
  salts_cond_broadcast(&context->probe->changed);
  salts_mutex_unlock(&context->probe->lock);
  cflow_publisher_cancel(context->source);
  salts_mutex_lock(&context->probe->lock);
  context->probe->cancel_returned = true;
  salts_cond_broadcast(&context->probe->changed);
  salts_mutex_unlock(&context->probe->lock);
}

static void destroy_source_and_close_owner(void *user) {
  reentrant_close_probe *probe = (reentrant_close_probe *)user;
  if (probe == NULL) return;
  cflow_publisher_destroy(probe->source);
  probe->close_status = cflow_fs_watch_publisher_owner_close(probe->owner);
  atomic_store(&probe->completed, true);
}

static int close_owner(cflow_fs_watch_publisher_owner *owner) {
  size_t attempts = 0u;
  int status;
  do {
    status = cflow_fs_watch_publisher_owner_close(owner);
    if (status == SALTS_EBUSY) salts_sleep_ms(1u);
  } while (status == SALTS_EBUSY && ++attempts < 5000u);
  return status;
}

typedef struct run_probe {
  watch_value value;
  size_t values;
  const char *error;
  const char *expected_path;
  bool saw_expected_path;
} run_probe;

static bool run_value(void *user, const cmeta_type_desc *type, const void *value) {
  run_probe *probe = (run_probe *)user;
  if (probe == NULL || value == NULL || !cmeta_type_equal(type, &watch_value_type)) return false;
  probe->value = *(const watch_value *)value;
  ++probe->values;
  if (probe->expected_path != NULL && strcmp(probe->value.path, probe->expected_path) == 0)
    probe->saw_expected_path = true;
  return true;
}

static void run_error(void *user, const char *message) {
  run_probe *probe = (run_probe *)user;
  if (probe != NULL) probe->error = message;
}

spec("CFlow filesystem watch Publisher") {
  it("rejects invalid construction without taking either output") {
    cflow_publisher source = {0};
    cflow_fs_watch_publisher_owner owner = {0};
    cflow_fs_watch_publisher_config config = source_config();

    check_equal(cflow_fs_watch_publisher_open(NULL, &owner, ".", &config), SALTS_EINVAL);
    check_equal(cflow_fs_watch_publisher_open(&source, NULL, ".", &config), SALTS_EINVAL);
    check_equal(cflow_fs_watch_publisher_open(&source, &owner, NULL, &config), SALTS_EINVAL);
    config.encode = NULL;
    check_equal(cflow_fs_watch_publisher_open(&source, &owner, ".", &config), SALTS_EINVAL);
    check_equal(cflow_fs_watch_publisher_owner_acknowledge_rescan(NULL), SALTS_EINVAL);
    check_false(cflow_fs_watch_publisher_owner_get_stats(NULL, NULL));
    check_equal(cflow_fs_watch_publisher_owner_close(NULL), SALTS_EINVAL);
    check_equal(cflow_fs_watch_publisher_owner_close(&owner), SALTS_OK);
    check_false(cflow_publisher_valid(&source));
    check_null(owner.impl);
  }

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
  it("preserves occupied outputs when construction fails") {
    char *root = tt_make_temp_dir("cflow-watch-source-occupied-");
    cflow_publisher source = {0};
    cflow_fs_watch_publisher_owner owner = {0};
    cflow_fs_watch_publisher_config config = source_config();
    cflow_publisher saved_source;
    void *saved_owner;

    check_not_null(root);
    check_equal(cflow_fs_watch_publisher_open(&source, &owner, root, &config), SALTS_OK);
    saved_source = source;
    saved_owner = owner.impl;

    check_equal(cflow_fs_watch_publisher_open(&source, &owner, NULL, &config), SALTS_EINVAL);
    check_true(source.self == saved_source.self);
    check_true(owner.impl == saved_owner);

    if (!cflow_publisher_valid(&source)) source = saved_source;
    if (owner.impl == NULL) owner.impl = saved_owner;
    cflow_publisher_destroy(&source);
    check_equal(close_owner(&owner), SALTS_OK);
    check_equal(tt_remove_tree(root), SALTS_OK);
    free(root);
  }

  it("wakes an armed Publisher and copies one native event into its value") {
    char *root = tt_make_temp_dir("cflow-watch-source-");
    char path[1024];
    cflow_publisher source = {0};
    cflow_fs_watch_publisher_owner owner = {0};
    cflow_fs_watch_publisher_config config = source_config();
    cflow_publish_context resume = {0};
    cflow_step step;
    watch_value value = {0};
    wake_probe wake;
    bool saw_file = false;
    size_t attempts = 0u;

    atomic_init(&wake.count, 0u);
    check_not_null(root);
    check_equal(salts_fs_path_join(path, sizeof(path), root, "one.txt"), SALTS_OK);
    check_equal(cflow_fs_watch_publisher_open(&source, &owner, root, &config), SALTS_OK);

    step = cflow_publisher_resume(&source, &resume, &value);
    check_equal(step.kind, CFLOW_STEP_WAIT);
    check_true(cflow_waitable_arm(&step.waitable, (cflow_waker){count_wake, &wake}));
    check_equal(tt_write_file(path, "x", 1u), SALTS_OK);
    while (atomic_load(&wake.count) == 0u && attempts++ < 5000u)
      salts_sleep_ms(1u);
    check_equal(atomic_load(&wake.count), (size_t)1u);

    attempts = 0u;
    while (!saw_file && attempts++ < 5000u) {
      step = cflow_publisher_resume(&source, &resume, &value);
      if (step.kind == CFLOW_STEP_VALUE || step.kind == CFLOW_STEP_VALUE_AND_DONE) {
        saw_file = strcmp(value.path, "one.txt") == 0;
        if (value.kind == CFLOW_FS_WATCH_RESCAN_REQUIRED)
          check_equal(cflow_fs_watch_publisher_owner_acknowledge_rescan(&owner), SALTS_OK);
        continue;
      }
      if (step.kind != CFLOW_STEP_WAIT) {
        check_equal(step.kind, CFLOW_STEP_WAIT);
        break;
      }
      atomic_store(&wake.count, 0u);
      check_true(cflow_waitable_arm(&step.waitable, (cflow_waker){count_wake, &wake}));
      while (atomic_load(&wake.count) == 0u && attempts++ < 5000u)
        salts_sleep_ms(1u);
    }
    check_true(saw_file);

    cflow_publisher_destroy(&source);
    check_equal(close_owner(&owner), SALTS_OK);
    check_null(owner.impl);
    check_equal(tt_remove_tree(root), SALTS_OK);
    free(root);
  }

  it("wakes immediately when publication wins the race before arm") {
    char *root = tt_make_temp_dir("cflow-watch-source-before-arm-");
    char path[1024];
    cflow_publisher source = {0};
    cflow_fs_watch_publisher_owner owner = {0};
    cflow_fs_watch_publisher_config config = source_config();
    cflow_publish_context resume = {0};
    cflow_fs_watch_stats stats = {0};
    cflow_step step;
    watch_value value = {0};
    wake_probe wake;
    size_t attempts = 0u;

    atomic_init(&wake.count, 0u);
    check_not_null(root);
    check_equal(salts_fs_path_join(path, sizeof(path), root, "early.txt"), SALTS_OK);
    check_equal(cflow_fs_watch_publisher_open(&source, &owner, root, &config), SALTS_OK);
    step = cflow_publisher_resume(&source, &resume, &value);
    check_equal(step.kind, CFLOW_STEP_WAIT);
    check_equal(tt_write_file(path, "x", 1u), SALTS_OK);
    while (attempts++ < 5000u) {
      check_true(cflow_fs_watch_publisher_owner_get_stats(&owner, &stats));
      if (stats.queued != 0u) break;
      salts_sleep_ms(1u);
    }
    check_greater(stats.queued, (size_t)0u);

    check_true(cflow_waitable_arm(&step.waitable, (cflow_waker){count_wake, &wake}));
    check_equal(atomic_load(&wake.count), (size_t)1u);
    step = cflow_publisher_resume(&source, &resume, &value);
    check_equal(step.kind, CFLOW_STEP_VALUE);

    cflow_publisher_destroy(&source);
    check_equal(close_owner(&owner), SALTS_OK);
    check_equal(tt_remove_tree(root), SALTS_OK);
    free(root);
  }

  it("keeps cleanup ownership until the Publisher is destroyed") {
    char *root = tt_make_temp_dir("cflow-watch-source-owner-");
    cflow_publisher source = {0};
    cflow_fs_watch_publisher_owner owner = {0};
    cflow_fs_watch_publisher_config config = source_config();

    check_not_null(root);
    check_equal(cflow_fs_watch_publisher_open(&source, &owner, root, &config), SALTS_OK);
    check_equal(cflow_fs_watch_publisher_owner_close(&owner), SALTS_EBUSY);
    check_not_null(owner.impl);

    cflow_publisher_destroy(&source);
    check_equal(close_owner(&owner), SALTS_OK);
    check_null(owner.impl);
    check_equal(tt_remove_tree(root), SALTS_OK);
    free(root);
  }

  it("wakes a waiter once when Publisher cancellation becomes terminal") {
    char *root = tt_make_temp_dir("cflow-watch-source-cancel-");
    cflow_publisher source = {0};
    cflow_fs_watch_publisher_owner owner = {0};
    cflow_fs_watch_publisher_config config = source_config();
    cflow_publish_context resume = {0};
    cflow_step step;
    watch_value value = {0};
    wake_probe wake;

    atomic_init(&wake.count, 0u);
    check_not_null(root);
    check_equal(cflow_fs_watch_publisher_open(&source, &owner, root, &config), SALTS_OK);
    step = cflow_publisher_resume(&source, &resume, &value);
    check_equal(step.kind, CFLOW_STEP_WAIT);
    check_true(cflow_waitable_arm(&step.waitable, (cflow_waker){count_wake, &wake}));

    cflow_publisher_cancel(&source);
    check_equal(atomic_load(&wake.count), (size_t)1u);
    step = cflow_publisher_resume(&source, &resume, &value);
    check_equal(step.kind, CFLOW_STEP_DONE);
    cflow_publisher_cancel(&source);
    check_equal(atomic_load(&wake.count), (size_t)1u);

    cflow_publisher_destroy(&source);
    check_equal(close_owner(&owner), SALTS_OK);
    check_equal(tt_remove_tree(root), SALTS_OK);
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
    cflow_publisher source = {0};
    cflow_fs_watch_publisher_owner owner = {0};
    cflow_fs_watch_publisher_config config = source_config();
    cflow_publish_context resume = {0};
    cflow_step step;
    watch_value value = {0};
    blocking_wake_probe wake = {0};
    cancel_context context = {&source, &wake};
    salts_thread_t canceller = {0};
    size_t waits = 0u;

    salts_mutex_init(&wake.lock);
    salts_cond_init(&wake.changed);
    check_not_null(wake.lock);
    check_not_null(wake.changed);
    check_not_null(root);
    check_equal(salts_fs_path_join(path, sizeof(path), root, "blocked.txt"), SALTS_OK);
    check_equal(cflow_fs_watch_publisher_open(&source, &owner, root, &config), SALTS_OK);
    step = cflow_publisher_resume(&source, &resume, &value);
    check_equal(step.kind, CFLOW_STEP_WAIT);
    check_true(cflow_waitable_arm(&step.waitable, (cflow_waker){blocking_wake, &wake}));
    check_equal(tt_write_file(path, "x", 1u), SALTS_OK);

    salts_mutex_lock(&wake.lock);
    while (!wake.entered && waits++ < WAIT_LIMIT)
      (void)salts_cond_timedwait(&wake.changed, &wake.lock, WAIT_SLICE_NS);
    if (!wake.entered) {
      wake.released = true;
      salts_cond_broadcast(&wake.changed);
    }
    check_true(wake.entered);
    salts_mutex_unlock(&wake.lock);
    check_equal(salts_thread_create(&canceller, cancel_source, &context), SALTS_OK);
    salts_mutex_lock(&wake.lock);
    waits = 0u;
    while (!wake.cancel_started && waits++ < WAIT_LIMIT)
      (void)salts_cond_timedwait(&wake.changed, &wake.lock, WAIT_SLICE_NS);
    check_true(wake.cancel_started);
    if (!wake.cancel_returned)
      (void)salts_cond_timedwait(&wake.changed, &wake.lock, CANCEL_OBSERVATION_NS);
    check_false(wake.cancel_returned);
    wake.released = true;
    salts_cond_broadcast(&wake.changed);
    salts_mutex_unlock(&wake.lock);

    check_equal(salts_thread_join(&canceller), SALTS_OK);
    salts_thread_destroy(&canceller);
    check_true(wake.cancel_returned);
    cflow_publisher_destroy(&source);
    check_equal(close_owner(&owner), SALTS_OK);
    salts_cond_destroy(&wake.changed);
    salts_mutex_destroy(&wake.lock);
    check_equal(tt_remove_tree(root), SALTS_OK);
    free(root);
  }

  it("keeps the owner busy during reentrant Publisher destruction") {
    char *root = tt_make_temp_dir("cflow-watch-source-reentrant-close-");
    char path[1024];
    cflow_publisher source = {0};
    cflow_fs_watch_publisher_owner owner = {0};
    cflow_fs_watch_publisher_config config = source_config();
    cflow_publish_context resume = {0};
    cflow_step step;
    watch_value value = {0};
    reentrant_close_probe probe = {0};
    size_t attempts = 0u;

    probe.source = &source;
    probe.owner = &owner;
    atomic_init(&probe.completed, false);
    check_not_null(root);
    check_equal(salts_fs_path_join(path, sizeof(path), root, "reentrant.txt"), SALTS_OK);
    check_equal(cflow_fs_watch_publisher_open(&source, &owner, root, &config), SALTS_OK);
    step = cflow_publisher_resume(&source, &resume, &value);
    check_equal(step.kind, CFLOW_STEP_WAIT);
    check_true(
        cflow_waitable_arm(&step.waitable, (cflow_waker){destroy_source_and_close_owner, &probe}));
    check_equal(tt_write_file(path, "x", 1u), SALTS_OK);
    while (!atomic_load(&probe.completed) && attempts++ < 5000u)
      salts_sleep_ms(1u);

    check_true(atomic_load(&probe.completed));
    check_equal(probe.close_status, SALTS_EBUSY);
    check_not_null(owner.impl);
    check_equal(close_owner(&owner), SALTS_OK);
    check_equal(tt_remove_tree(root), SALTS_OK);
    free(root);
  }

  it("turns encoder rejection into a Publisher error") {
    char *root = tt_make_temp_dir("cflow-watch-source-encoder-");
    char path[1024];
    cflow_publisher source = {0};
    cflow_fs_watch_publisher_owner owner = {0};
    cflow_fs_watch_publisher_config config = source_config();
    cflow_publish_context resume = {0};
    cflow_step step;
    watch_value value = {0};
    size_t attempts = 0u;

    config.encode = reject_watch_value;
    check_not_null(root);
    check_equal(salts_fs_path_join(path, sizeof(path), root, "bad.txt"), SALTS_OK);
    check_equal(cflow_fs_watch_publisher_open(&source, &owner, root, &config), SALTS_OK);
    check_equal(tt_write_file(path, "x", 1u), SALTS_OK);
    do {
      step = cflow_publisher_resume(&source, &resume, &value);
      if (step.kind == CFLOW_STEP_WAIT) salts_sleep_ms(1u);
    } while (step.kind == CFLOW_STEP_WAIT && attempts++ < 5000u);
    check_equal(step.kind, CFLOW_STEP_ERROR);
    check_equal(step.error, "filesystem watch encoder failed");

    cflow_publisher_destroy(&source);
    check_equal(close_owner(&owner), SALTS_OK);
    check_equal(tt_remove_tree(root), SALTS_OK);
    free(root);
  }

  it("executes native events through Graph and Subscription demand") {
    char *root = tt_make_temp_dir("cflow-watch-source-run-");
    char path[1024];
    cflow_publisher source = {0};
    cflow_fs_watch_publisher_owner owner = {0};
    cflow_fs_watch_publisher_config config = source_config();
    cflow_graph graph = {0};
    cflow_scheduler scheduler = {0};
    cflow_subscription run = {0};
    run_probe probe = {.expected_path = "run.txt"};
    cflow_subscriber_callbacks callbacks = {
        .on_value = run_value,
        .on_error = run_error,
        .on_done = NULL,
        .user = &probe,
    };
    cflow_subscriber sink = cflow_subscriber_from_callbacks(&callbacks);
    size_t attempts = 0u;

    check_not_null(root);
    check_equal(salts_fs_path_join(path, sizeof(path), root, "run.txt"), SALTS_OK);
    check_equal(cflow_fs_watch_publisher_open(&source, &owner, root, &config), SALTS_OK);
    cflow_graph_init(&graph, &watch_value_type);
    check_true(cflow_scheduler_test_init(&scheduler));
    check_true(cflow_subscribe(&run, &graph, &source, &scheduler, &sink));
    check_false(cflow_publisher_valid(&source));
    check_true(cflow_subscription_request(&run, 8u));
    (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
    check_equal(tt_write_file(path, "x", 1u), SALTS_OK);
    while (!probe.saw_expected_path && probe.error == NULL && attempts++ < 5000u) {
      (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
      if (!probe.saw_expected_path) salts_sleep_ms(1u);
    }

    check_null(probe.error);
    check_greater(probe.values, (size_t)0u);
    check_true(probe.saw_expected_path);
    cflow_subscription_close(&run);
    check_equal(close_owner(&owner), SALTS_OK);
    cflow_scheduler_destroy(&scheduler);
    cflow_graph_destroy(&graph);
    check_equal(tt_remove_tree(root), SALTS_OK);
    free(root);
  }

  it("propagates encoder rejection through Subscription") {
    char *root = tt_make_temp_dir("cflow-watch-source-run-error-");
    char path[1024];
    cflow_publisher source = {0};
    cflow_fs_watch_publisher_owner owner = {0};
    cflow_fs_watch_publisher_config config = source_config();
    cflow_graph graph = {0};
    cflow_scheduler scheduler = {0};
    cflow_subscription run = {0};
    run_probe probe = {0};
    cflow_subscriber_callbacks callbacks = {
        .on_value = run_value,
        .on_error = run_error,
        .on_done = NULL,
        .user = &probe,
    };
    cflow_subscriber sink = cflow_subscriber_from_callbacks(&callbacks);
    size_t attempts = 0u;

    config.encode = reject_watch_value;
    check_not_null(root);
    check_equal(salts_fs_path_join(path, sizeof(path), root, "run-error.txt"), SALTS_OK);
    check_equal(cflow_fs_watch_publisher_open(&source, &owner, root, &config), SALTS_OK);
    cflow_graph_init(&graph, &watch_value_type);
    check_true(cflow_scheduler_test_init(&scheduler));
    check_true(cflow_subscribe(&run, &graph, &source, &scheduler, &sink));
    check_true(cflow_subscription_request(&run, 1u));
    (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
    check_equal(tt_write_file(path, "x", 1u), SALTS_OK);
    while (probe.error == NULL && attempts++ < 5000u) {
      (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
      if (probe.error == NULL) salts_sleep_ms(1u);
    }

    check_equal(probe.error, "filesystem watch encoder failed");
    check_equal(cflow_subscription_error(&run), "filesystem watch encoder failed");
    check_equal(probe.values, (size_t)0u);
    cflow_subscription_close(&run);
    check_equal(close_owner(&owner), SALTS_OK);
    cflow_scheduler_destroy(&scheduler);
    cflow_graph_destroy(&graph);
    check_equal(tt_remove_tree(root), SALTS_OK);
    free(root);
  }

  it("exposes rescan acknowledgement through the cleanup owner") {
    char *root = tt_make_temp_dir("cflow-watch-source-rescan-");
    char first[1024];
    char second[1024];
    cflow_publisher source = {0};
    cflow_fs_watch_publisher_owner owner = {0};
    cflow_fs_watch_publisher_config config = source_config();
    cflow_publish_context resume = {0};
    cflow_fs_watch_stats stats = {0};
    watch_value value = {0};
    bool saw_rescan = false;
    size_t attempts = 0u;

    config.event_capacity = 1u;
    check_not_null(root);
    check_equal(salts_fs_path_join(first, sizeof(first), root, "one.txt"), SALTS_OK);
    check_equal(salts_fs_path_join(second, sizeof(second), root, "two.txt"), SALTS_OK);
    check_equal(cflow_fs_watch_publisher_open(&source, &owner, root, &config), SALTS_OK);
    check_equal(tt_write_file(first, "1", 1u), SALTS_OK);
    check_equal(tt_write_file(second, "2", 1u), SALTS_OK);
    while (attempts++ < 5000u) {
      check_true(cflow_fs_watch_publisher_owner_get_stats(&owner, &stats));
      if (stats.awaiting_rescan) break;
      salts_sleep_ms(1u);
    }
    check_true(stats.awaiting_rescan);
    for (attempts = 0u; attempts < 4u && !saw_rescan; ++attempts) {
      const cflow_step step = cflow_publisher_resume(&source, &resume, &value);
      check_true(step.kind == CFLOW_STEP_VALUE || step.kind == CFLOW_STEP_VALUE_AND_DONE);
      saw_rescan = value.kind == CFLOW_FS_WATCH_RESCAN_REQUIRED;
    }
    check_true(saw_rescan);
    check_equal(cflow_fs_watch_publisher_owner_acknowledge_rescan(&owner), SALTS_OK);

    cflow_publisher_destroy(&source);
    check_equal(close_owner(&owner), SALTS_OK);
    check_equal(tt_remove_tree(root), SALTS_OK);
    free(root);
  }

  it("wakes an armed Publisher when acknowledgement queues another rescan") {
    char *root = tt_make_temp_dir("cflow-watch-source-rescan-wake-");
    char first[1024];
    char second[1024];
    char third[1024];
    cflow_publisher source = {0};
    cflow_fs_watch_publisher_owner owner = {0};
    cflow_fs_watch_publisher_config config = source_config();
    cflow_publish_context resume = {0};
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
    check_equal(salts_fs_path_join(first, sizeof(first), root, "one.txt"), SALTS_OK);
    check_equal(salts_fs_path_join(second, sizeof(second), root, "two.txt"), SALTS_OK);
    check_equal(salts_fs_path_join(third, sizeof(third), root, "three.txt"), SALTS_OK);
    check_equal(cflow_fs_watch_publisher_open(&source, &owner, root, &config), SALTS_OK);
    check_equal(tt_write_file(first, "1", 1u), SALTS_OK);
    check_equal(tt_write_file(second, "2", 1u), SALTS_OK);
    while (attempts++ < 5000u) {
      check_true(cflow_fs_watch_publisher_owner_get_stats(&owner, &stats));
      if (stats.awaiting_rescan) break;
      salts_sleep_ms(1u);
    }
    check_true(stats.awaiting_rescan);
    for (attempts = 0u; attempts < 4u && !saw_rescan; ++attempts) {
      step = cflow_publisher_resume(&source, &resume, &value);
      check_true(step.kind == CFLOW_STEP_VALUE || step.kind == CFLOW_STEP_VALUE_AND_DONE);
      saw_rescan = value.kind == CFLOW_FS_WATCH_RESCAN_REQUIRED;
    }
    check_true(saw_rescan);
    check_true(cflow_fs_watch_publisher_owner_get_stats(&owner, &delivered_stats));

    step = cflow_publisher_resume(&source, &resume, &value);
    check_equal(step.kind, CFLOW_STEP_WAIT);
    check_true(cflow_waitable_arm(&step.waitable, (cflow_waker){count_wake, &wake}));
    check_equal(tt_write_file(third, "3", 1u), SALTS_OK);
    attempts = 0u;
    while (attempts++ < 5000u) {
      check_true(cflow_fs_watch_publisher_owner_get_stats(&owner, &stats));
      if (stats.suppressed > delivered_stats.suppressed) break;
      salts_sleep_ms(1u);
    }
    check_greater(stats.suppressed, delivered_stats.suppressed);
    check_equal(atomic_load(&wake.count), (size_t)0u);

    check_equal(cflow_fs_watch_publisher_owner_acknowledge_rescan(&owner), SALTS_OK);
    check_equal(atomic_load(&wake.count), (size_t)1u);
    step = cflow_publisher_resume(&source, &resume, &value);
    check_true(step.kind == CFLOW_STEP_VALUE || step.kind == CFLOW_STEP_VALUE_AND_DONE);
    check_equal(value.kind, CFLOW_FS_WATCH_RESCAN_REQUIRED);

    cflow_publisher_destroy(&source);
    check_equal(close_owner(&owner), SALTS_OK);
    check_equal(tt_remove_tree(root), SALTS_OK);
    free(root);
  }

  it("delivers root changes and honors the native backend lifecycle") {
    char *root = tt_make_temp_dir("cflow-watch-source-root-");
    cflow_publisher source = {0};
    cflow_fs_watch_publisher_owner owner = {0};
    cflow_fs_watch_publisher_config config = source_config();
    cflow_publish_context resume = {0};
    watch_value value = {0};
    wake_probe wake;
    bool saw_root_changed = false;
    bool saw_done = false;
  #if defined(_WIN32)
    const bool require_done = true;
  #else
    const bool require_done = false;
  #endif
    size_t attempts = 0u;

    atomic_init(&wake.count, 0u);
    check_not_null(root);
    check_equal(cflow_fs_watch_publisher_open(&source, &owner, root, &config), SALTS_OK);
    check_equal(tt_remove_tree(root), SALTS_OK);

    while ((!saw_root_changed || (require_done && !saw_done)) && attempts++ < 5000u) {
      cflow_step step = cflow_publisher_resume(&source, &resume, &value);
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
        salts_sleep_ms(1u);
    }

    check_true(saw_root_changed);
    if (require_done) check_true(saw_done);
    cflow_publisher_destroy(&source);
    check_equal(close_owner(&owner), SALTS_OK);
    free(root);
  }
#endif
}
