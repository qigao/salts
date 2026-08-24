/**
 * @file test_threadpool.c
 * @brief Thread pool unit tests
 */

#include "turbo_thread.h"
#include "tinytest.h"
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>

#define UNUSED(x) (void)(x)
#define NUM_TASKS 100

static turbo_mutex_t counter_mutex;
static volatile int counter = 0;
static atomic_int gate_open;
static atomic_int mpmc_counter;

typedef struct callback_protocol_ctx {
    turbo_threadpool_t *pool;
    atomic_int entered;
    int first_post_status;
    int second_post_status;
    int wait_status;
} callback_protocol_ctx;

typedef struct task_lifecycle_probe {
    atomic_int run_count;
    atomic_int cancel_count;
    atomic_int finalize_count;
    atomic_int finalize_entered;
    atomic_int finalize_gate;
    atomic_int sequence;
    atomic_int run_sequence;
    atomic_int cancel_sequence;
    atomic_int finalize_sequence;
} task_lifecycle_probe;

typedef struct wait_probe {
    turbo_threadpool_t *pool;
    atomic_int entered;
    atomic_int done;
    int status;
} wait_probe;

typedef struct {
    turbo_threadpool_t *pool;
    int tasks;
    atomic_int *submit_failures;
} submitter_ctx_t;

static void increment_task(void *arg) {
    UNUSED(arg);
    turbo_mutex_lock(&counter_mutex);
    counter++;
    turbo_mutex_unlock(&counter_mutex);
}

static void slow_task(void *arg) {
    int *result = (int *)arg;
    turbo_sleep_ms(10);
    *result = 42;
}

static void sum_task(void *arg) {
    int *val = (int *)arg;
    turbo_mutex_lock(&counter_mutex);
    counter += *val;
    turbo_mutex_unlock(&counter_mutex);
}

static void gated_task(void *arg) {
    UNUSED(arg);
    while (!atomic_load(&gate_open)) {
        turbo_sleep_ms(1);
    }
}

static void mpmc_count_task(void *arg) {
    UNUSED(arg);
    atomic_fetch_add(&mpmc_counter, 1);
}

static void gated_count_task(void *arg) {
    UNUSED(arg);
    while (!atomic_load(&gate_open)) turbo_sleep_ms(1);
    atomic_fetch_add(&mpmc_counter, 1);
}

static void lifecycle_run(void *arg) {
    task_lifecycle_probe *probe = (task_lifecycle_probe *)arg;
    atomic_fetch_add(&probe->run_count, 1);
    atomic_store(&probe->run_sequence,
                 atomic_fetch_add(&probe->sequence, 1) + 1);
}

static void lifecycle_cancel(void *arg) {
    task_lifecycle_probe *probe = (task_lifecycle_probe *)arg;
    atomic_fetch_add(&probe->cancel_count, 1);
    atomic_store(&probe->cancel_sequence,
                 atomic_fetch_add(&probe->sequence, 1) + 1);
}

static void lifecycle_finalize(void *arg) {
    task_lifecycle_probe *probe = (task_lifecycle_probe *)arg;
    atomic_fetch_add(&probe->finalize_count, 1);
    atomic_store(&probe->finalize_sequence,
                 atomic_fetch_add(&probe->sequence, 1) + 1);
}

static void blocking_lifecycle_finalize(void *arg) {
    task_lifecycle_probe *probe = (task_lifecycle_probe *)arg;
    lifecycle_finalize(arg);
    atomic_store(&probe->finalize_entered, 1);
    while (!atomic_load(&probe->finalize_gate)) turbo_sleep_ms(1);
}

static void wait_for_pool(void *arg) {
    wait_probe *probe = (wait_probe *)arg;
    atomic_store(&probe->entered, 1);
    probe->status = turbo_threadpool_wait_status(probe->pool);
    atomic_store(&probe->done, 1);
}

static void callback_protocol_probe(void *arg) {
    callback_protocol_ctx *ctx = (callback_protocol_ctx *)arg;
    atomic_store(&ctx->entered, 1);
    ctx->first_post_status = turbo_threadpool_submit(
        ctx->pool, mpmc_count_task, NULL);
    ctx->second_post_status = turbo_threadpool_submit(
        ctx->pool, mpmc_count_task, NULL);
    ctx->wait_status = turbo_threadpool_wait_status(ctx->pool);
}

static void submitter_thread(void *arg) {
    submitter_ctx_t *ctx = (submitter_ctx_t *)arg;
    for (int i = 0; i < ctx->tasks; ++i) {
        if (turbo_threadpool_submit(ctx->pool, mpmc_count_task, NULL) != 0) {
            atomic_fetch_add(ctx->submit_failures, 1);
        }
    }
}

spec("Thread Pool Tests") {
    before_each() {
        turbo_mutex_init(&counter_mutex);
        counter = 0;
        atomic_store(&gate_open, 0);
        atomic_store(&mpmc_counter, 0);
    }

    after_each() {
        turbo_mutex_destroy(&counter_mutex);
    }

    it("should create and destroy pool") {
        turbo_threadpool_t *pool = turbo_threadpool_create(2);
        check(pool != NULL);
        check_equal(turbo_threadpool_size(pool), 2);
        turbo_threadpool_destroy(pool);
    }

    it("should auto-detect CPU cores when 0") {
        turbo_threadpool_t *pool = turbo_threadpool_create(0);
        check(pool != NULL);
        check(turbo_threadpool_size(pool) >= 1);
        printf("  (detected %d cores)\n", turbo_threadpool_size(pool));
        turbo_threadpool_destroy(pool);
    }

    it("should execute single task") {
        turbo_threadpool_t *pool = turbo_threadpool_create(2);

        int result = 0;
        turbo_threadpool_submit(pool, slow_task, &result);
        turbo_threadpool_wait(pool);

        check_equal(result, 42);
        turbo_threadpool_destroy(pool);
    }

    it("should execute many tasks") {
        turbo_threadpool_t *pool = turbo_threadpool_create(4);

        for (int i = 0; i < NUM_TASKS; i++) {
            turbo_threadpool_submit(pool, increment_task, NULL);
        }

        turbo_threadpool_wait(pool);
        check_equal(counter, NUM_TASKS);

        turbo_threadpool_destroy(pool);
    }

    it("should accept tasks from multiple producers") {
        enum { PRODUCERS = 4, TASKS_PER_PRODUCER = 250 };
        turbo_threadpool_t *pool = turbo_threadpool_create(4);
        turbo_thread_t producers[PRODUCERS];
        submitter_ctx_t contexts[PRODUCERS];
        atomic_int submit_failures;

        atomic_store(&submit_failures, 0);
        check(pool != NULL);

        for (int i = 0; i < PRODUCERS; ++i) {
            contexts[i].pool = pool;
            contexts[i].tasks = TASKS_PER_PRODUCER;
            contexts[i].submit_failures = &submit_failures;
            check_equal(turbo_thread_create(&producers[i], submitter_thread, &contexts[i]), 0);
        }

        for (int i = 0; i < PRODUCERS; ++i) {
            turbo_thread_join(&producers[i]);
        }

        turbo_threadpool_wait(pool);
        check_equal(atomic_load(&submit_failures), 0);
        check_equal(atomic_load(&mpmc_counter), PRODUCERS * TASKS_PER_PRODUCER);

        turbo_threadpool_destroy(pool);
    }

    it("should handle more tasks than threads") {
        turbo_threadpool_t *pool = turbo_threadpool_create(2);

        for (int i = 0; i < 50; i++) {
            turbo_threadpool_submit(pool, increment_task, NULL);
        }

        turbo_threadpool_wait(pool);
        check_equal(counter, 50);

        turbo_threadpool_destroy(pool);
    }

    it("should pass arguments correctly") {
        turbo_threadpool_t *pool = turbo_threadpool_create(4);

        int values[10];
        for (int i = 0; i < 10; i++) {
            values[i] = i + 1;  // 1..10
            turbo_threadpool_submit(pool, sum_task, &values[i]);
        }

        turbo_threadpool_wait(pool);
        // Sum of 1..10 = 55
        check_equal(counter, 55);

        turbo_threadpool_destroy(pool);
    }

    it("should report pending count") {
        turbo_threadpool_t *pool = turbo_threadpool_create(1);

        // Submit slow tasks
        int results[5] = {0};
        for (int i = 0; i < 5; i++) {
            turbo_threadpool_submit(pool, slow_task, &results[i]);
        }

        // Should have pending tasks
        check(turbo_threadpool_pending(pool) > 0);

        turbo_threadpool_wait(pool);
        check_equal(turbo_threadpool_pending(pool), 0);

        turbo_threadpool_destroy(pool);
    }

    it("should handle empty wait") {
        turbo_threadpool_t *pool = turbo_threadpool_create(2);

        // Wait with no tasks should return immediately
        turbo_threadpool_wait(pool);
        check_equal(turbo_threadpool_pending(pool), 0);

        turbo_threadpool_destroy(pool);
    }

    it("should reject tasks after shutdown") {
        turbo_threadpool_t *pool = turbo_threadpool_create(2);
        turbo_threadpool_shutdown(pool);
        check_equal(turbo_threadpool_is_accepting(pool), 0);
        check_equal(turbo_threadpool_submit(pool, increment_task, NULL),
                    TURBO_ESHUTDOWN);
        turbo_threadpool_destroy(pool);

        check(1);
    }

    it("should reject invalid submissions with an exact status") {
        turbo_threadpool_t *pool = turbo_threadpool_create(1);

        check(pool != NULL);
        check_equal(turbo_threadpool_try_submit(NULL, gated_task, NULL),
                    TURBO_EINVAL);
        check_equal(turbo_threadpool_try_submit(pool, NULL, NULL),
                    TURBO_EINVAL);

        turbo_threadpool_destroy(pool);
    }

    it("should honor configured queue capacity for try_submit") {
        turbo_threadpool_config_t config = {
            .num_threads = 1,
            .queue_capacity = 2,
        };
        turbo_threadpool_t *pool = turbo_threadpool_create_with_config(&config);
        turbo_threadpool_stats_t stats = {0};
        int accepted = 0;
        int rejected = 0;
        int rejected_status = TURBO_OK;
        int attempts = 0;

        check(pool != NULL);
        check_equal((int)turbo_threadpool_capacity(pool), 2);

        while (accepted < 1 && attempts < 200) {
            if (turbo_threadpool_try_submit(pool, gated_task, NULL) == 0) {
                accepted++;
                break;
            }
            turbo_sleep_ms(1);
            attempts++;
        }
        check_equal(accepted, 1);

        attempts = 0;
        do {
            turbo_threadpool_get_stats(pool, &stats);
            if (stats.active_tasks >= 1) {
                break;
            }
            turbo_sleep_ms(1);
            attempts++;
        } while (attempts < 200);
        check_equal((int)stats.active_tasks, 1);

        attempts = 0;
        while ((accepted < 3 || rejected < 1) && attempts < 400) {
            int status = turbo_threadpool_try_submit(pool, gated_task, NULL);
            if (status == TURBO_OK) {
                accepted++;
            } else {
                rejected++;
                rejected_status = status;
            }
            turbo_sleep_ms(1);
            attempts++;
        }

        turbo_threadpool_get_stats(pool, &stats);
        check_equal(accepted, 3);
        check(rejected >= 1);
        check_equal(rejected_status, TURBO_ENOBUFS);
        check_equal((int)stats.rejected_tasks, 1);
        check_equal((int)stats.active_tasks, 1);
        check_equal((int)stats.queued_tasks, 2);
        check_equal((int)stats.pending_tasks, 3);
        check_equal((int)stats.peak_pending_tasks, 3);

        atomic_store(&gate_open, 1);
        turbo_threadpool_wait(pool);
        turbo_threadpool_get_stats(pool, &stats);
        check_equal((int)stats.pending_tasks, 0);
        turbo_threadpool_destroy(pool);
    }

    it("fails callback self-blocking operations without deadlock") {
        turbo_threadpool_config_t config = {
            .num_threads = 1,
            .queue_capacity = 1,
        };
        turbo_threadpool_t *pool = turbo_threadpool_create_with_config(&config);
        callback_protocol_ctx ctx = {
            .pool = pool,
            .first_post_status = TURBO_UNKNOWN,
            .second_post_status = TURBO_UNKNOWN,
            .wait_status = TURBO_UNKNOWN,
        };

        check_not_null(pool);
        atomic_init(&ctx.entered, 0);
        check_equal(turbo_threadpool_submit(
                        pool, callback_protocol_probe, &ctx), TURBO_OK);
        check_equal(turbo_threadpool_wait_status(pool), TURBO_OK);
        check_equal(atomic_load(&ctx.entered), 1);
        check_equal(ctx.first_post_status, TURBO_OK);
        check_equal(ctx.second_post_status, TURBO_EBUSY);
        check_equal(ctx.wait_status, TURBO_EBUSY);
        check_equal(atomic_load(&mpmc_counter), 1);
        turbo_threadpool_destroy(pool);
    }

    it("cancels queued callbacks while allowing running work to finish") {
        turbo_threadpool_config_t config = {
            .num_threads = 1,
            .queue_capacity = 2,
        };
        turbo_threadpool_t *pool = turbo_threadpool_create_with_config(&config);
        turbo_threadpool_stats_t stats = {0};
        int attempts = 0;

        check_not_null(pool);
        check_equal(turbo_threadpool_submit(pool, gated_count_task, NULL),
                    TURBO_OK);
        do {
            turbo_threadpool_get_stats(pool, &stats);
            if (stats.active_tasks == 1) break;
            turbo_sleep_ms(1);
        } while (++attempts < 200);
        check_equal((int)stats.active_tasks, 1);
        check_equal(turbo_threadpool_submit(pool, mpmc_count_task, NULL),
                    TURBO_OK);
        check_equal(turbo_threadpool_submit(pool, mpmc_count_task, NULL),
                    TURBO_OK);

        check_equal(turbo_threadpool_shutdown_with_policy(
                        pool, TURBO_THREADPOOL_SHUTDOWN_CANCEL_PENDING),
                    TURBO_OK);
        atomic_store(&gate_open, 1);
        check_equal(turbo_threadpool_wait_status(pool), TURBO_OK);
        check_equal(atomic_load(&mpmc_counter), 1);
        check_equal(turbo_threadpool_cancelled(pool), (int64_t)2);
        check_equal(turbo_threadpool_pending(pool), 0);
        turbo_threadpool_destroy(pool);
    }

    it("runs and finalizes an accepted task exactly once") {
        turbo_threadpool_t *pool = turbo_threadpool_create(1);
        task_lifecycle_probe probe = {0};
        turbo_threadpool_task_t task = {
            .run = lifecycle_run,
            .cancel = lifecycle_cancel,
            .finalize = lifecycle_finalize,
            .arg = &probe,
        };

        check_not_null(pool);
        check_equal(turbo_threadpool_submit_task(pool, &task), TURBO_OK);
        check_equal(turbo_threadpool_wait_status(pool), TURBO_OK);
        check_equal(atomic_load(&probe.run_count), 1);
        check_equal(atomic_load(&probe.cancel_count), 0);
        check_equal(atomic_load(&probe.finalize_count), 1);
        check_equal(atomic_load(&probe.run_sequence), 1);
        check_equal(atomic_load(&probe.finalize_sequence), 2);
        turbo_threadpool_destroy(pool);
    }

    it("keeps a running descriptor pending until finalize returns") {
        turbo_threadpool_t *pool = turbo_threadpool_create(1);
        task_lifecycle_probe lifecycle = {0};
        wait_probe waiter = {
            .pool = pool,
            .status = TURBO_UNKNOWN,
        };
        turbo_threadpool_task_t task = {
            .run = lifecycle_run,
            .cancel = lifecycle_cancel,
            .finalize = blocking_lifecycle_finalize,
            .arg = &lifecycle,
        };
        turbo_threadpool_stats_t stats = {0};
        turbo_thread_t wait_thread;
        int attempts = 0;

        check_not_null(pool);
        check_equal(turbo_threadpool_submit_task(pool, &task), TURBO_OK);
        while (!atomic_load(&lifecycle.finalize_entered) && ++attempts < 200)
            turbo_sleep_ms(1);
        check_equal(atomic_load(&lifecycle.finalize_entered), 1);

        turbo_threadpool_get_stats(pool, &stats);
        check_equal((int)stats.queued_tasks, 0);
        check_equal((int)stats.active_tasks, 1);
        check_equal((int)stats.pending_tasks, 1);

        check_equal(turbo_thread_create(&wait_thread, wait_for_pool, &waiter),
                    TURBO_OK);
        attempts = 0;
        while (!atomic_load(&waiter.entered) && ++attempts < 200)
            turbo_sleep_ms(1);
        check_equal(atomic_load(&waiter.entered), 1);
        turbo_sleep_ms(10);
        check_equal(atomic_load(&waiter.done), 0);

        atomic_store(&lifecycle.finalize_gate, 1);
        turbo_thread_join(&wait_thread);
        turbo_threadpool_get_stats(pool, &stats);
        check_equal(waiter.status, TURBO_OK);
        check_equal(atomic_load(&waiter.done), 1);
        check_equal((int)stats.active_tasks, 0);
        check_equal((int)stats.pending_tasks, 0);
        check_equal(atomic_load(&lifecycle.run_count), 1);
        check_equal(atomic_load(&lifecycle.finalize_count), 1);
        turbo_threadpool_destroy(pool);
    }

    it("cancels and finalizes queued task exactly once") {
        turbo_threadpool_config_t config = {
            .num_threads = 1,
            .queue_capacity = 1,
        };
        turbo_threadpool_t *pool = turbo_threadpool_create_with_config(&config);
        task_lifecycle_probe probe = {0};
        turbo_threadpool_task_t task = {
            .run = lifecycle_run,
            .cancel = lifecycle_cancel,
            .finalize = lifecycle_finalize,
            .arg = &probe,
        };
        turbo_threadpool_stats_t stats = {0};
        int attempts = 0;

        check_not_null(pool);
        check_equal(turbo_threadpool_submit(pool, gated_task, NULL), TURBO_OK);
        do {
            turbo_threadpool_get_stats(pool, &stats);
            if (stats.active_tasks == 1) break;
            turbo_sleep_ms(1);
        } while (++attempts < 200);
        check_equal((int)stats.active_tasks, 1);
        check_equal(turbo_threadpool_submit_task(pool, &task), TURBO_OK);
        check_equal(turbo_threadpool_shutdown_with_policy(
                        pool, TURBO_THREADPOOL_SHUTDOWN_CANCEL_PENDING),
                    TURBO_OK);
        atomic_store(&gate_open, 1);
        check_equal(turbo_threadpool_wait_status(pool), TURBO_OK);
        check_equal(atomic_load(&probe.run_count), 0);
        check_equal(atomic_load(&probe.cancel_count), 1);
        check_equal(atomic_load(&probe.finalize_count), 1);
        check_equal(atomic_load(&probe.cancel_sequence), 1);
        check_equal(atomic_load(&probe.finalize_sequence), 2);
        turbo_threadpool_destroy(pool);
    }

    it("keeps a cancelling descriptor pending until finalize returns") {
        turbo_threadpool_config_t config = {
            .num_threads = 1,
            .queue_capacity = 1,
        };
        turbo_threadpool_t *pool = turbo_threadpool_create_with_config(&config);
        task_lifecycle_probe lifecycle = {0};
        wait_probe waiter = {
            .pool = pool,
            .status = TURBO_UNKNOWN,
        };
        turbo_threadpool_task_t task = {
            .run = lifecycle_run,
            .cancel = lifecycle_cancel,
            .finalize = blocking_lifecycle_finalize,
            .arg = &lifecycle,
        };
        turbo_threadpool_stats_t stats = {0};
        turbo_thread_t wait_thread;
        int attempts = 0;

        check_not_null(pool);
        check_equal(turbo_threadpool_submit(pool, gated_task, NULL), TURBO_OK);
        do {
            turbo_threadpool_get_stats(pool, &stats);
            if (stats.active_tasks == 1) break;
            turbo_sleep_ms(1);
        } while (++attempts < 200);
        check_equal((int)stats.active_tasks, 1);
        check_equal(turbo_threadpool_submit_task(pool, &task), TURBO_OK);
        check_equal(turbo_threadpool_shutdown_with_policy(
                        pool, TURBO_THREADPOOL_SHUTDOWN_CANCEL_PENDING),
                    TURBO_OK);

        atomic_store(&gate_open, 1);
        attempts = 0;
        while (!atomic_load(&lifecycle.finalize_entered) && ++attempts < 200)
            turbo_sleep_ms(1);
        check_equal(atomic_load(&lifecycle.finalize_entered), 1);

        turbo_threadpool_get_stats(pool, &stats);
        check_equal((int)stats.queued_tasks, 1);
        check_equal((int)stats.active_tasks, 0);
        check_equal((int)stats.pending_tasks, 1);
        check_equal(turbo_threadpool_cancelled(pool), (int64_t)0);

        check_equal(turbo_thread_create(&wait_thread, wait_for_pool, &waiter),
                    TURBO_OK);
        attempts = 0;
        while (!atomic_load(&waiter.entered) && ++attempts < 200)
            turbo_sleep_ms(1);
        check_equal(atomic_load(&waiter.entered), 1);
        turbo_sleep_ms(10);
        check_equal(atomic_load(&waiter.done), 0);

        atomic_store(&lifecycle.finalize_gate, 1);
        turbo_thread_join(&wait_thread);
        turbo_threadpool_get_stats(pool, &stats);
        check_equal(waiter.status, TURBO_OK);
        check_equal(atomic_load(&waiter.done), 1);
        check_equal((int)stats.queued_tasks, 0);
        check_equal((int)stats.pending_tasks, 0);
        check_equal(turbo_threadpool_cancelled(pool), (int64_t)1);
        turbo_threadpool_destroy(pool);
    }

    it("does not invoke descriptor callbacks after full rejection") {
        turbo_threadpool_config_t config = {
            .num_threads = 1,
            .queue_capacity = 1,
        };
        turbo_threadpool_t *pool = turbo_threadpool_create_with_config(&config);
        task_lifecycle_probe accepted_probe = {0};
        task_lifecycle_probe rejected_probe = {0};
        turbo_threadpool_task_t accepted = {
            .run = lifecycle_run,
            .cancel = lifecycle_cancel,
            .finalize = lifecycle_finalize,
            .arg = &accepted_probe,
        };
        turbo_threadpool_task_t rejected = {
            .run = lifecycle_run,
            .cancel = lifecycle_cancel,
            .finalize = lifecycle_finalize,
            .arg = &rejected_probe,
        };
        turbo_threadpool_stats_t stats = {0};
        int attempts = 0;

        check_not_null(pool);
        check_equal(turbo_threadpool_submit(pool, gated_task, NULL), TURBO_OK);
        do {
            turbo_threadpool_get_stats(pool, &stats);
            if (stats.active_tasks == 1) break;
            turbo_sleep_ms(1);
        } while (++attempts < 200);
        check_equal((int)stats.active_tasks, 1);
        check_equal(turbo_threadpool_submit_task(pool, &accepted), TURBO_OK);
        check_equal(turbo_threadpool_try_submit_task(pool, &rejected),
                    TURBO_ENOBUFS);
        check_equal(atomic_load(&rejected_probe.run_count), 0);
        check_equal(atomic_load(&rejected_probe.cancel_count), 0);
        check_equal(atomic_load(&rejected_probe.finalize_count), 0);

        check_equal(turbo_threadpool_shutdown_with_policy(
                        pool, TURBO_THREADPOOL_SHUTDOWN_CANCEL_PENDING),
                    TURBO_OK);
        atomic_store(&gate_open, 1);
        check_equal(turbo_threadpool_wait_status(pool), TURBO_OK);
        check_equal(atomic_load(&accepted_probe.cancel_count), 1);
        check_equal(atomic_load(&accepted_probe.finalize_count), 1);
        check_equal(atomic_load(&rejected_probe.sequence), 0);
        turbo_threadpool_destroy(pool);
    }
}
