#include <cflow/clock.h>
#include <cflow/executor.h>
#include <cflow/time.h>
#include <turbo/thread.h>
#include "timer_queue.h"
#include "tinytest.h"

#include <stdatomic.h>

static atomic_int executor_counter;
static atomic_int active_callbacks;
static atomic_int max_active_callbacks;
static atomic_int producer_failures;
static atomic_int worker_gate_open;
static atomic_int worker_gate_started;

static void count_task(void *user) {
  (void)user;
  atomic_fetch_add(&executor_counter, 1);
}

static void gated_count_task(void *user) {
  (void)user;
  atomic_store(&worker_gate_started, 1);
  while (!atomic_load(&worker_gate_open)) turbo_sleep_ms(1);
  atomic_fetch_add(&executor_counter, 1);
}

static void serial_probe(void *user) {
  int active;
  int seen;
  (void)user;

  active = atomic_fetch_add(&active_callbacks, 1) + 1;
  seen = atomic_load(&max_active_callbacks);
  while (active > seen &&
         !atomic_compare_exchange_weak(&max_active_callbacks, &seen, active)) {
  }
  turbo_sleep_ms(1);
  atomic_fetch_add(&executor_counter, 1);
  atomic_fetch_sub(&active_callbacks, 1);
}

typedef struct executor_producer_ctx {
  cflow_executor *executor;
  int count;
} executor_producer_ctx;

static void executor_producer(void *user) {
  executor_producer_ctx *ctx = (executor_producer_ctx *)user;
  for (int i = 0; i < ctx->count; ++i) {
    if (!cflow_executor_post(ctx->executor, serial_probe, NULL))
      atomic_fetch_add(&producer_failures, 1);
  }
}

spec("CFlow execution foundation") {
  it("saturates deadline arithmetic") {
    cflow_instant now = {UINT64_MAX - 5u};
    cflow_duration delay = cflow_duration_from_ns(10u);
    cflow_deadline deadline = cflow_deadline_after(now, delay);
    check_equal(deadline.ns, UINT64_MAX);
  }

  it("saturates duration unit conversion") {
    check_equal(cflow_duration_from_s(UINT64_MAX).ns, UINT64_MAX);
  }

  it("advances virtual time exactly") {
    cflow_clock clock = {0};
    check_true(cflow_clock_virtual_init(&clock, (cflow_instant){100u}));
    check_equal(cflow_clock_now(&clock).ns, 100u);
    check_true(cflow_clock_advance(&clock, cflow_duration_from_ns(25u)));
    check_equal(cflow_clock_now(&clock).ns, 125u);
    cflow_clock_destroy(&clock);
  }

  it("does not manually advance system time") {
    cflow_clock clock = {0};
    check_true(cflow_clock_system_init(&clock));
    check(cflow_clock_now(&clock).ns > 0u);
    check_false(cflow_clock_advance(&clock, cflow_duration_from_ns(1u)));
    cflow_clock_destroy(&clock);
  }

  it("keeps ManualExecutor explicitly driven") {
    cflow_executor executor = {0};
    atomic_store(&executor_counter, 0);

    check_true(cflow_executor_manual_init(&executor));
    check_equal(cflow_executor_try_post(&executor, NULL, NULL),
                CFLOW_ADMISSION_INVALID_ARGUMENT);
    check_equal(cflow_executor_try_post(&executor, count_task, NULL),
                CFLOW_ADMISSION_ACCEPTED);
    check_equal(cflow_executor_pending(&executor), (size_t)1u);
    check_false(cflow_executor_wait_idle(&executor));
    check_equal(atomic_load(&executor_counter), 0);

    check_true(cflow_executor_run_one(&executor));
    check_equal(atomic_load(&executor_counter), 1);
    check_equal(cflow_executor_pending(&executor), (size_t)0u);
    check_true(cflow_executor_wait_idle(&executor));
    cflow_executor_destroy(&executor);
  }

  it("bounds ManualExecutor storage and reuses released capacity") {
    cflow_executor executor = {0};
    const void *task_storage;

    check_false(cflow_executor_manual_init_with_capacity(&executor, 0u));
    check_false(cflow_executor_valid(&executor));
    check_false(cflow_executor_manual_init_with_capacity(&executor, SIZE_MAX));
    check_false(cflow_executor_valid(&executor));

    check_true(cflow_executor_manual_init_with_capacity(&executor, 1u));
    task_storage = *(void *const *)executor.self;
    check(task_storage != NULL);
    check_equal(cflow_executor_try_post(&executor, count_task, NULL),
                CFLOW_ADMISSION_ACCEPTED);
    check_equal(cflow_executor_try_post(&executor, count_task, NULL),
                CFLOW_ADMISSION_FULL);
    check_false(cflow_executor_post(&executor, count_task, NULL));
    check_true(cflow_executor_run_one(&executor));
    check_equal(cflow_executor_try_post(&executor, count_task, NULL),
                CFLOW_ADMISSION_ACCEPTED);
    check(*(void *const *)executor.self == task_storage);
    check_true(cflow_executor_run_one(&executor));
    cflow_executor_destroy(&executor);
  }

  it("serializes callbacks from concurrent producers") {
    enum { PRODUCERS = 4, TASKS_PER_PRODUCER = 8 };
    cflow_executor executor = {0};
    turbo_thread_t producers[PRODUCERS];
    executor_producer_ctx contexts[PRODUCERS];

    atomic_store(&executor_counter, 0);
    atomic_store(&active_callbacks, 0);
    atomic_store(&max_active_callbacks, 0);
    atomic_store(&producer_failures, 0);

    check_true(cflow_executor_serial_init(&executor));
    for (int i = 0; i < PRODUCERS; ++i) {
      contexts[i].executor = &executor;
      contexts[i].count = TASKS_PER_PRODUCER;
      check_equal(turbo_thread_create(&producers[i], executor_producer,
                                      &contexts[i]), 0);
    }
    for (int i = 0; i < PRODUCERS; ++i)
      check_equal(turbo_thread_join(&producers[i]), 0);

    check_true(cflow_executor_wait_idle(&executor));
    check_equal(atomic_load(&producer_failures), 0);
    check_equal(atomic_load(&executor_counter), PRODUCERS * TASKS_PER_PRODUCER);
    check_equal(atomic_load(&max_active_callbacks), 1);
    cflow_executor_destroy(&executor);
  }

  it("runs work through WorkerExecutor") {
    cflow_executor executor = {0};
    atomic_store(&executor_counter, 0);

    check_true(cflow_executor_worker_init(&executor, 2u));
    check_true(cflow_executor_post(&executor, count_task, NULL));
    check_true(cflow_executor_post(&executor, count_task, NULL));
    check_true(cflow_executor_wait_idle(&executor));
    check_equal(atomic_load(&executor_counter), 2);
    cflow_executor_destroy(&executor);
  }

  it("reports WorkerExecutor saturation without growing its queue") {
    cflow_executor executor = {0};
    int attempts = 0;

    check_false(cflow_executor_serial_init_with_capacity(&executor, 0u));
    check_false(cflow_executor_worker_init_with_capacity(&executor, 0u, 1u));
    check_false(cflow_executor_worker_init_with_capacity(&executor, 1u, 0u));
    atomic_store(&executor_counter, 0);
    atomic_store(&worker_gate_open, 0);
    atomic_store(&worker_gate_started, 0);
    check_true(cflow_executor_worker_init_with_capacity(&executor, 1u, 1u));
    check_true(cflow_executor_post(&executor, gated_count_task, NULL));
    while (!atomic_load(&worker_gate_started) && attempts++ < 200)
      turbo_sleep_ms(1);
    check_equal(atomic_load(&worker_gate_started), 1);
    check_equal(cflow_executor_try_post(&executor, count_task, NULL),
                CFLOW_ADMISSION_ACCEPTED);
    check_equal(cflow_executor_try_post(&executor, count_task, NULL),
                CFLOW_ADMISSION_FULL);

    atomic_store(&worker_gate_open, 1);
    check_true(cflow_executor_wait_idle(&executor));
    check_equal(atomic_load(&executor_counter), 2);
    cflow_executor_destroy(&executor);
  }

  it("orders TimerQueue by deadline then insertion order") {
    cflow_timer_queue queue;
    cflow_timer_task task;
    int a = 1;
    int b = 2;
    int c = 3;

    check_true(cflow_timer_queue_init(&queue));
    check(cflow_timer_queue_schedule(&queue, (cflow_deadline){20u}, count_task, &a) != 0u);
    check(cflow_timer_queue_schedule(&queue, (cflow_deadline){10u}, count_task, &b) != 0u);
    check(cflow_timer_queue_schedule(&queue, (cflow_deadline){10u}, count_task, &c) != 0u);

    check_true(cflow_timer_queue_take_ready(&queue, (cflow_instant){10u}, &task));
    check(task.user == &b);
    check_true(cflow_timer_queue_take_ready(&queue, (cflow_instant){10u}, &task));
    check(task.user == &c);
    check_false(cflow_timer_queue_take_ready(&queue, (cflow_instant){10u}, &task));
    check_true(cflow_timer_queue_take_ready(&queue, (cflow_instant){20u}, &task));
    check(task.user == &a);
    check_equal(cflow_timer_queue_pending(&queue), (size_t)0u);
    cflow_timer_queue_destroy(&queue);
  }

  it("cancels only pending TimerQueue entries") {
    cflow_timer_queue queue;
    cflow_timer_task task;
    int a = 1;
    int b = 2;
    cflow_timer_id a_id;
    cflow_timer_id b_id;

    check_true(cflow_timer_queue_init(&queue));
    a_id = cflow_timer_queue_schedule(&queue, (cflow_deadline){10u}, count_task, &a);
    b_id = cflow_timer_queue_schedule(&queue, (cflow_deadline){20u}, count_task, &b);
    check(a_id != 0u);
    check(b_id != 0u);
    check_true(cflow_timer_queue_cancel(&queue, a_id));
    check_false(cflow_timer_queue_cancel(&queue, a_id));
    check_true(cflow_timer_queue_take_ready(&queue, (cflow_instant){20u}, &task));
    check(task.user == &b);
    check_false(cflow_timer_queue_cancel(&queue, b_id));
    cflow_timer_queue_destroy(&queue);
  }

  it("bounds TimerQueue storage and reuses released capacity") {
    cflow_timer_queue queue;
    cflow_timer_task task;
    cflow_schedule_result result;
    const void *item_storage;

    check_false(cflow_timer_queue_init_with_capacity(&queue, 0u));
    check_false(cflow_timer_queue_init_with_capacity(&queue, SIZE_MAX));
    check_true(cflow_timer_queue_init_with_capacity(&queue, 1u));
    item_storage = queue.items;
    check(item_storage != NULL);

    result = cflow_timer_queue_try_schedule(
        &queue, (cflow_deadline){10u}, count_task, NULL);
    check_equal(result.status, CFLOW_ADMISSION_ACCEPTED);
    check(result.task_id != 0u);
    result = cflow_timer_queue_try_schedule(
        &queue, (cflow_deadline){20u}, count_task, NULL);
    check_equal(result.status, CFLOW_ADMISSION_FULL);
    check_equal(result.task_id, (cflow_task_id)0u);

    check_true(cflow_timer_queue_take_ready(
        &queue, (cflow_instant){10u}, &task));
    result = cflow_timer_queue_try_schedule(
        &queue, (cflow_deadline){20u}, count_task, NULL);
    check_equal(result.status, CFLOW_ADMISSION_ACCEPTED);
    check(queue.items == item_storage);
    cflow_timer_queue_destroy(&queue);
  }
}
