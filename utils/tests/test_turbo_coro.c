/**
 * @file test_turbo_coro.c
 * @brief Tests for the TurboNet coroutine primitive.
 */

#include "tinytest.h"
#include "turbo_coro.h"
#include "turbo_coro_pool.h"

#include <stdint.h>
#include <string.h>

enum {
    CORO_STACK_WORK_BYTES = 2048,
    CORO_STACK_WORK_ROUNDS = 4
};

typedef struct coro_stack_work_state_s {
    uint32_t checksum;
    int rounds;
    int mismatch;
} coro_stack_work_state_t;

typedef struct coro_nested_stack_state_s {
    coro_stack_work_state_t child;
    int parent_resumes;
    int allocation_failed;
    int resume_failed;
    int parent_stack_mismatch;
} coro_nested_stack_state_t;

static int g_counter = 0;
static int g_discard_count = 0;

static void counter_coro(coro_t *co, void *arg) {
    UNUSED(co);
    int times = *(int *)arg;
    for (int i = 0; i < times; i++) {
        g_counter++;
        coro_yield();
    }
}

static void echo_coro(coro_t *co, void *arg) {
    UNUSED(arg);
    int value = 0;
    while (coro_bytes_stored(co) >= sizeof(int)) {
        coro_pop(co, &value, sizeof(value));
        value *= 2;
        coro_push(co, &value, sizeof(value));
        coro_yield();
    }
}

static void sched_worker(coro_t *co, void *arg) {
    UNUSED(co);
    int *counter = (int *)arg;
    for (int i = 0; i < 3; i++) {
        (*counter)++;
        coro_yield();
    }
}

static void sched_fast(coro_t *co, void *arg) {
    UNUSED(co);
    int *counter = (int *)arg;
    (*counter) += 10;
}

static void stack_work_coro(coro_t *co, void *arg) {
    coro_stack_work_state_t *state = (coro_stack_work_state_t *)arg;
    uint8_t source[CORO_STACK_WORK_BYTES];
    uint8_t copy[CORO_STACK_WORK_BYTES];
    UNUSED(co);

    for (int round = 0; round < CORO_STACK_WORK_ROUNDS; ++round) {
        uint8_t value = (uint8_t)(0x31 + round);
        memset(source, value, sizeof(source));
        memcpy(copy, source, sizeof(copy));
        if (memcmp(copy, source, sizeof(copy)) != 0) {
            state->mismatch = 1;
        }
        for (size_t i = 0; i < sizeof(copy); ++i) {
            state->checksum += copy[i];
        }
        state->rounds++;
        coro_yield();
    }
}

static void nested_stack_work_coro(coro_t *co, void *arg) {
    coro_nested_stack_state_t *state = (coro_nested_stack_state_t *)arg;
    uint8_t parent_stack[CORO_STACK_WORK_BYTES];
    coro_t *child = NULL;
    UNUSED(co);

    memset(parent_stack, 0x5a, sizeof(parent_stack));
    child = coro_create(stack_work_coro, &state->child, NULL);
    if (!child) {
        state->allocation_failed = 1;
        return;
    }

    while (coro_alive(child)) {
        if (coro_resume(child) != 0) {
            state->resume_failed = 1;
            break;
        }
        for (size_t i = 0; i < sizeof(parent_stack); ++i) {
            if (parent_stack[i] != 0x5a) {
                state->parent_stack_mismatch = 1;
                break;
            }
        }
        state->parent_resumes++;
        coro_yield();
    }

    coro_destroy(child);
}

static void discard_callback(coro_t *co, void *arg) {
    UNUSED(co);
    int *counter = (int *)arg;
    (*counter)++;
}

spec("Turbo Coro Primitive") {
    before_each() {
        g_counter = 0;
        g_discard_count = 0;
    }

    it("creates, resumes, yields, and destroys a coroutine") {
        int times = 3;
        coro_t *co = coro_create(counter_coro, &times, NULL);
        check_not_null(co);
        check_int_eq(coro_state(co), coro_SUSPENDED);

        for (int i = 0; i < times; i++) {
            check_int_eq(coro_alive(co), 1);
            check_int_eq(coro_resume(co), 0);
            check_int_eq(g_counter, i + 1);
        }

        check_int_eq(coro_resume(co), 0);
        check_int_eq(coro_alive(co), 0);
        coro_destroy(co);
    }

    it("passes data through coroutine storage") {
        coro_t *co = coro_create(echo_coro, NULL, NULL);
        int input = 21;
        int output = 0;

        check_not_null(co);
        check_int_eq(coro_push(co, &input, sizeof(input)), 0);
        check_int_eq(coro_resume(co), 0);
        check_int_eq(coro_pop(co, &output, sizeof(output)), 0);
        check_int_eq(output, 42);

        coro_destroy(co);
    }

    it("keeps stack memory valid across repeated context switches") {
        coro_stack_work_state_t state = {0};
        uint32_t expected = 0;
        coro_t *co = coro_create(stack_work_coro, &state, NULL);

        check_not_null(co);
        while (coro_alive(co)) {
            check_int_eq(coro_resume(co), 0);
        }
        for (int round = 0; round < CORO_STACK_WORK_ROUNDS; ++round) {
            expected += (uint32_t)(0x31 + round) * CORO_STACK_WORK_BYTES;
        }
        check_int_eq(state.rounds, CORO_STACK_WORK_ROUNDS);
        check_int_eq(state.mismatch, 0);
        check_uint_eq(state.checksum, expected);

        coro_destroy(co);
    }

    it("keeps parent and child stacks valid across nested context switches") {
        coro_nested_stack_state_t state = {0};
        coro_t *parent = coro_create(nested_stack_work_coro, &state, NULL);

        check_not_null(parent);
        while (coro_alive(parent)) {
            check_int_eq(coro_resume(parent), 0);
        }
        check_int_eq(state.allocation_failed, 0);
        check_int_eq(state.resume_failed, 0);
        check_int_eq(state.parent_stack_mismatch, 0);
        check_int_eq(state.parent_resumes, CORO_STACK_WORK_ROUNDS + 1);
        check_int_eq(state.child.rounds, CORO_STACK_WORK_ROUNDS);
        check_int_eq(state.child.mismatch, 0);

        coro_destroy(parent);
    }

    it("stores user data") {
        coro_opts_t opts = coro_OPTS_DEFAULT;
        int data = 123;
        int new_data = 456;
        int times = 1;
        opts.user_data = &data;

        coro_t *co = coro_create(counter_coro, &times, &opts);
        check_not_null(co);
        check(coro_get_data(co) == &data);

        coro_set_data(co, &new_data);
        check(coro_get_data(co) == &new_data);

        coro_destroy(co);
    }

    it("rejects coroutine allocation size overflow") {
        coro_opts_t opts = coro_OPTS_DEFAULT;
        int times = 1;
        opts.stack_size = SIZE_MAX;
        check_null(coro_create(counter_coro, &times, &opts));
    }

    it("runs scheduled coroutines to completion") {
        coro_scheduler_t *sched = coro_scheduler_create();
        int c1 = 0;
        int c2 = 0;

        check_not_null(sched);
        check_not_null(coro_spawn(sched, sched_worker, &c1, NULL));
        check_not_null(coro_spawn(sched, sched_worker, &c2, NULL));
        check_int_eq(coro_scheduler_count(sched), 2);

        coro_scheduler_run(sched);
        check_int_eq(c1, 3);
        check_int_eq(c2, 3);
        check_int_eq(coro_scheduler_count(sched), 0);

        coro_scheduler_destroy(sched);
    }

    it("handles fast-completing scheduled coroutines") {
        coro_scheduler_t *sched = coro_scheduler_create();
        int counter = 0;

        check_not_null(sched);
        check_not_null(coro_spawn(sched, sched_fast, &counter, NULL));
        check_not_null(coro_spawn(sched, sched_fast, &counter, NULL));

        coro_scheduler_run(sched);
        check_int_eq(counter, 20);
        check_int_eq(coro_scheduler_count(sched), 0);

        coro_scheduler_destroy(sched);
    }

    it("calls discard callback during forced scheduler teardown") {
        coro_scheduler_t *sched = coro_scheduler_create();
        int times = 2;
        coro_t *co = NULL;

        check_not_null(sched);
        co = coro_spawn(sched, counter_coro, &times, NULL);
        check_not_null(co);
        coro_set_discard(co, discard_callback, &g_discard_count);

        coro_scheduler_destroy(sched);
        check_int_eq(g_discard_count, 1);
    }
}

spec("Turbo Coro Pool") {
    before_each() {
        g_counter = 0;
    }

    it("creates and destroys a generic coroutine pool") {
        turbo_coro_pool_config_t config = TURBO_CORO_POOL_CONFIG_DEFAULT;
        turbo_coro_pool_t *pool = turbo_coro_pool_create(&config);

        check_not_null(pool);
        check_int_eq(turbo_coro_pool_free_count(pool), 16);
        check_int_eq(turbo_coro_pool_active_count(pool), 0);
        check_int_eq(turbo_coro_pool_capacity(pool), 16);

        turbo_coro_pool_destroy(pool);
    }

    it("acquires and releases pooled coroutines") {
        turbo_coro_pool_config_t config = {.initial_capacity = 2, .max_capacity = 4, .stack_size = 0, .storage_size = 0};
        turbo_coro_pool_t *pool = turbo_coro_pool_create(&config);
        int counter = 0;
        coro_t *co1 = NULL;
        coro_t *co2 = NULL;

        check_not_null(pool);
        co1 = turbo_coro_pool_acquire(pool, sched_worker, &counter);
        co2 = turbo_coro_pool_acquire(pool, sched_worker, &counter);
        check_not_null(co1);
        check_not_null(co2);
        check_int_eq(turbo_coro_pool_active_count(pool), 2);
        check_int_eq(turbo_coro_pool_free_count(pool), 0);

        while (coro_alive(co1)) check_int_eq(coro_resume(co1), 0);
        while (coro_alive(co2)) check_int_eq(coro_resume(co2), 0);

        turbo_coro_pool_release(pool, co1);
        turbo_coro_pool_release(pool, co2);
        check_int_eq(turbo_coro_pool_active_count(pool), 0);
        check_int_eq(turbo_coro_pool_free_count(pool), 2);

        turbo_coro_pool_destroy(pool);
    }

    it("respects max capacity") {
        turbo_coro_pool_config_t config = {.initial_capacity = 1, .max_capacity = 2, .stack_size = 0, .storage_size = 0};
        turbo_coro_pool_t *pool = turbo_coro_pool_create(&config);
        int counter = 0;
        coro_t *co1 = NULL;
        coro_t *co2 = NULL;

        check_not_null(pool);
        co1 = turbo_coro_pool_acquire(pool, sched_fast, &counter);
        co2 = turbo_coro_pool_acquire(pool, sched_fast, &counter);
        check_not_null(co1);
        check_not_null(co2);
        check(turbo_coro_pool_acquire(pool, sched_fast, &counter) == NULL);

        while (coro_alive(co1)) check_int_eq(coro_resume(co1), 0);
        while (coro_alive(co2)) check_int_eq(coro_resume(co2), 0);
        turbo_coro_pool_release(pool, co1);
        turbo_coro_pool_release(pool, co2);

        turbo_coro_pool_destroy(pool);
    }

    it("reuses coroutine shells without overwriting user data") {
        turbo_coro_pool_config_t config = {.initial_capacity = 1, .max_capacity = 1, .stack_size = 0, .storage_size = 0};
        turbo_coro_pool_t *pool = turbo_coro_pool_create(&config);
        int c1 = 0;
        int c2 = 0;
        int user_data = 42;
        coro_t *co1 = NULL;
        coro_t *co2 = NULL;

        check_not_null(pool);
        co1 = turbo_coro_pool_acquire(pool, sched_worker, &c1);
        check_not_null(co1);
        coro_set_data(co1, &user_data);
        while (coro_alive(co1)) check_int_eq(coro_resume(co1), 0);
        turbo_coro_pool_release(pool, co1);

        co2 = turbo_coro_pool_acquire(pool, sched_worker, &c2);
        check_not_null(co2);
        check(co1 == co2);
        check(coro_get_data(co2) == &user_data);
        while (coro_alive(co2)) check_int_eq(coro_resume(co2), 0);
        turbo_coro_pool_release(pool, co2);

        check_int_eq(c1, 3);
        check_int_eq(c2, 3);
        turbo_coro_pool_destroy(pool);
    }

    it("spawns pooled coroutines into a scheduler") {
        turbo_coro_pool_config_t config = {.initial_capacity = 2, .max_capacity = 2, .stack_size = 0, .storage_size = 0};
        turbo_coro_pool_t *pool = turbo_coro_pool_create(&config);
        coro_scheduler_t *sched = coro_scheduler_create();
        int c1 = 0;
        int c2 = 0;

        check_not_null(pool);
        check_not_null(sched);
        check_not_null(turbo_coro_spawn_pooled(sched, pool, sched_worker, &c1));
        check_not_null(turbo_coro_spawn_pooled(sched, pool, sched_worker, &c2));
        check_int_eq(turbo_coro_pool_active_count(pool), 2);

        coro_scheduler_run(sched);
        check_int_eq(c1, 3);
        check_int_eq(c2, 3);
        check_int_eq(turbo_coro_pool_active_count(pool), 0);
        check_int_eq(turbo_coro_pool_free_count(pool), 2);

        coro_scheduler_destroy(sched);
        turbo_coro_pool_destroy(pool);
    }
}
