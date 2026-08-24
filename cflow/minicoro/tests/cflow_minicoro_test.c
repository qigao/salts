#include <cflow/minicoro.h>

#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

typedef struct native_script_state {
    size_t index;
} native_script_state;

typedef struct step_trace {
    cflow_step_kind kinds[3];
    int values[2];
    size_t step_count;
    size_t value_count;
} step_trace;

static cflow_step native_script_resume(void *state,
                                       cflow_resume_ctx *ctx,
                                       void *out_value) {
    native_script_state *script = (native_script_state *)state;
    (void)ctx;

    if (script == NULL || out_value == NULL)
        return (cflow_step){CFLOW_STEP_ERROR, {0}, "native script invalid"};
    if (script->index == 0u) {
        *(int *)out_value = 3;
        ++script->index;
        return (cflow_step){CFLOW_STEP_VALUE, {0}, NULL};
    }
    if (script->index == 1u) {
        *(int *)out_value = 5;
        ++script->index;
        return (cflow_step){CFLOW_STEP_VALUE_AND_DONE, {0}, NULL};
    }
    return (cflow_step){CFLOW_STEP_DONE, {0}, NULL};
}

static const cflow_resumable_ops native_script_ops = {
    native_script_resume,
    NULL,
    NULL
};

static void minicoro_script(cflow_minicoro *coroutine, void *user) {
    int first = 3;
    int last = 5;
    (void)user;

    if (!cflow_minicoro_yield_value(coroutine, &first))
        return;
    first = 99;
    (void)cflow_minicoro_return_value(coroutine, &last);
}

static void minicoro_complete(cflow_minicoro *coroutine, void *user) {
    (void)coroutine;
    (void)user;
}

static void minicoro_error(cflow_minicoro *coroutine, void *user) {
    (void)user;
    (void)cflow_minicoro_fail(coroutine, "script failed");
}

typedef struct test_waitable_state {
    cflow_waker current;
    cflow_waker stale;
    size_t arms;
    size_t cancels;
} test_waitable_state;

static bool test_waitable_arm(void *state, cflow_waker waker) {
    test_waitable_state *waitable = (test_waitable_state *)state;

    if (waitable == NULL || waker.wake == NULL)
        return false;
    waitable->current = waker;
    waitable->stale = waker;
    ++waitable->arms;
    return true;
}

static void test_waitable_cancel(void *state) {
    test_waitable_state *waitable = (test_waitable_state *)state;

    if (waitable == NULL)
        return;
    waitable->current = (cflow_waker){0};
    ++waitable->cancels;
}

CMETA_IMPLEMENTS(cflow_waitable, test_waitable, 0,
    .arm = test_waitable_arm,
    .cancel = test_waitable_cancel
);

static void test_waitable_wake(test_waitable_state *waitable) {
    cflow_waker waker;

    if (waitable == NULL)
        return;
    waker = waitable->current;
    waitable->current = (cflow_waker){0};
    if (waker.wake != NULL)
        waker.wake(waker.user);
}

typedef struct wait_script_state {
    test_waitable_state *waitable;
    cflow_resume_ctx *seen_context;
    size_t after_wait;
} wait_script_state;

static void minicoro_wait_then_value(cflow_minicoro *coroutine, void *user) {
    wait_script_state *script = (wait_script_state *)user;
    const int value = 41;

    script->seen_context = cflow_minicoro_resume_context(coroutine);
    if (!cflow_minicoro_wait(
            coroutine, test_waitable_as_cflow_waitable(script->waitable)))
        return;
    ++script->after_wait;
    (void)cflow_minicoro_return_value(coroutine, &value);
}

typedef struct wake_probe {
    size_t wakes;
} wake_probe;

static void count_wake(void *user) {
    wake_probe *probe = (wake_probe *)user;
    if (probe != NULL)
        ++probe->wakes;
}

typedef struct destroy_from_wake_state {
    cflow_resumable *resumable;
    size_t calls;
} destroy_from_wake_state;

static void destroy_resumable(cflow_resumable *resumable);

static void destroy_from_wake(void *user) {
    destroy_from_wake_state *state = (destroy_from_wake_state *)user;

    if (state == NULL)
        return;
    ++state->calls;
    if (state->resumable != NULL && state->resumable->ops != NULL) {
        if (state->resumable->ops->cancel != NULL)
            state->resumable->ops->cancel(state->resumable->state);
        destroy_resumable(state->resumable);
    }
}

typedef struct counting_allocator {
    size_t allocations;
    size_t deallocations;
    size_t fail_at;
} counting_allocator;

static void *counting_alloc(size_t size, void *allocator_data) {
    counting_allocator *allocator = (counting_allocator *)allocator_data;
    size_t allocation_index;

    if (allocator == NULL)
        return NULL;
    allocation_index = allocator->allocations++;
    if (allocation_index == allocator->fail_at)
        return NULL;
    return malloc(size);
}

static void counting_dealloc(void *pointer,
                             size_t size,
                             void *allocator_data) {
    counting_allocator *allocator = (counting_allocator *)allocator_data;
    (void)size;

    if (allocator != NULL)
        ++allocator->deallocations;
    free(pointer);
}

static step_trace collect_trace(cflow_resumable *resumable) {
    step_trace trace = {0};
    cflow_resume_ctx context = {0};

    while (trace.step_count < 3u) {
        int value = 0;
        cflow_step step = resumable->ops->resume(
            resumable->state, &context, &value);

        trace.kinds[trace.step_count++] = step.kind;
        if (step.kind == CFLOW_STEP_VALUE ||
            step.kind == CFLOW_STEP_VALUE_AND_DONE)
            trace.values[trace.value_count++] = value;
        if (step.kind == CFLOW_STEP_VALUE_AND_DONE ||
            step.kind == CFLOW_STEP_DONE ||
            step.kind == CFLOW_STEP_ERROR)
            break;
    }
    return trace;
}

static void destroy_resumable(cflow_resumable *resumable) {
    if (resumable != NULL && resumable->ops != NULL &&
        resumable->ops->destroy != NULL)
        resumable->ops->destroy(resumable->state);
    if (resumable != NULL)
        memset(resumable, 0, sizeof(*resumable));
}

spec("CFlow minicoro Resumable adapter") {
    it("matches a native Resumable value trace") {
        native_script_state native_state = {0};
        cflow_resumable native = {
            "native-script", &cmeta_type_int, &native_script_ops, &native_state
        };
        cflow_resumable coroutine = {0};
        cflow_minicoro_config config = {
            "minicoro-script",
            &cmeta_type_int,
            minicoro_script,
            NULL,
            0u,
            NULL,
            NULL,
            NULL
        };
        step_trace native_trace;
        step_trace coroutine_trace;

        check_true(cflow_resumable_from_minicoro(&coroutine, &config));
        native_trace = collect_trace(&native);
        coroutine_trace = collect_trace(&coroutine);

        check_equal(native_trace.step_count, (size_t)2u);
        check_equal(coroutine_trace.step_count, native_trace.step_count);
        check_equal(coroutine_trace.value_count, (size_t)2u);
        check_equal(coroutine_trace.kinds[0], CFLOW_STEP_VALUE);
        check_equal(coroutine_trace.kinds[1], CFLOW_STEP_VALUE_AND_DONE);
        check_equal(coroutine_trace.kinds[0], native_trace.kinds[0]);
        check_equal(coroutine_trace.kinds[1], native_trace.kinds[1]);
        check_equal(coroutine_trace.values[0], 3);
        check_equal(coroutine_trace.values[1], 5);
        check_equal(coroutine_trace.values[0], native_trace.values[0]);
        check_equal(coroutine_trace.values[1], native_trace.values[1]);

        destroy_resumable(&coroutine);
    }

    it("maps an immediate callback return to DONE") {
        cflow_resumable coroutine = {0};
        cflow_minicoro_config config = {
            "complete", &cmeta_type_int, minicoro_complete, NULL,
            0u, NULL, NULL, NULL
        };
        cflow_resume_ctx context = {0};
        int output = 17;
        cflow_step step;

        check_true(cflow_resumable_from_minicoro(&coroutine, &config));
        step = coroutine.ops->resume(coroutine.state, &context, &output);

        check_equal(step.kind, CFLOW_STEP_DONE);
        check_equal(output, 17);
        destroy_resumable(&coroutine);
    }

    it("maps callback failure to ERROR") {
        cflow_resumable coroutine = {0};
        cflow_minicoro_config config = {
            "failure", &cmeta_type_int, minicoro_error, NULL,
            0u, NULL, NULL, NULL
        };
        cflow_resume_ctx context = {0};
        int output = 23;
        cflow_step step;

        check_true(cflow_resumable_from_minicoro(&coroutine, &config));
        step = coroutine.ops->resume(coroutine.state, &context, &output);

        check_equal(step.kind, CFLOW_STEP_ERROR);
        check_equal(step.error, "script failed");
        check_equal(output, 23);
        destroy_resumable(&coroutine);
    }

    it("rejects invalid and managed output descriptors") {
        static const cmeta_type_traits managed_traits = {0};
        static const cmeta_type_desc managed_type = {
            "managed",
            sizeof(int),
            _Alignof(int),
            CMETA_T_OBJECT,
            NULL,
            &managed_traits,
            NULL
        };
        cflow_resumable coroutine = {0};
        cflow_minicoro_config config = {
            "invalid", &managed_type, minicoro_complete, NULL,
            0u, NULL, NULL, NULL
        };
        cmeta_type_desc malformed_type = cmeta_type_int;

        check_false(cflow_resumable_from_minicoro(&coroutine, &config));
        check_null(coroutine.ops);
        config.output_type = NULL;
        check_false(cflow_resumable_from_minicoro(&coroutine, &config));
        config.output_type = &cmeta_type_int;
        config.entry = NULL;
        check_false(cflow_resumable_from_minicoro(&coroutine, &config));
        check_false(cflow_resumable_from_minicoro(NULL, &config));
        config.entry = minicoro_complete;
        malformed_type.name = NULL;
        config.output_type = &malformed_type;
        check_false(cflow_resumable_from_minicoro(&coroutine, &config));
        malformed_type = cmeta_type_int;
        malformed_type.align = 3u;
        check_false(cflow_resumable_from_minicoro(&coroutine, &config));
    }

    it("preserves output and rejects invalid construction resources") {
        native_script_state native_state = {0};
        cflow_resumable sentinel = {
            "sentinel", &cmeta_type_int, &native_script_ops, &native_state
        };
        cflow_resumable coroutine = sentinel;
        counting_allocator allocator = {0u, 0u, SIZE_MAX};
        cflow_minicoro_config config = {
            "invalid-resources", &cmeta_type_int, minicoro_complete, NULL,
            0u, counting_alloc, NULL, &allocator
        };

        check_false(cflow_resumable_from_minicoro(&coroutine, &config));
        check(coroutine.name == sentinel.name);
        check(coroutine.output_type == sentinel.output_type);
        check(coroutine.ops == sentinel.ops);
        check(coroutine.state == sentinel.state);
        check_equal(allocator.allocations, (size_t)0u);

        config.alloc = NULL;
        config.dealloc = counting_dealloc;
        check_false(cflow_resumable_from_minicoro(&coroutine, &config));
        check(coroutine.state == sentinel.state);
        check_equal(allocator.allocations, (size_t)0u);

        config.alloc = counting_alloc;
        config.stack_size = SIZE_MAX - 15u;
        check_false(cflow_resumable_from_minicoro(&coroutine, &config));
        check(coroutine.state == sentinel.state);
        check_equal(allocator.allocations, (size_t)0u);
    }

    it("reuses the supplied CFlow context across WAIT and wake") {
        test_waitable_state waitable = {0};
        wait_script_state script = {&waitable, NULL, 0u};
        wake_probe probe = {0};
        cflow_resumable coroutine = {0};
        cflow_minicoro_config config = {
            "wait", &cmeta_type_int, minicoro_wait_then_value, &script,
            0u, NULL, NULL, NULL
        };
        cflow_scheduler scheduler = {0};
        cflow_resume_ctx context = {&scheduler};
        int output = 0;
        cflow_step step;

        check_true(cflow_resumable_from_minicoro(&coroutine, &config));
        step = coroutine.ops->resume(coroutine.state, &context, &output);
        check_equal(step.kind, CFLOW_STEP_WAIT);
        check_true(cflow_waitable_arm(
            &step.waitable, (cflow_waker){count_wake, &probe}));
        check_equal(waitable.arms, (size_t)1u);
        check(script.seen_context == &context);
        check_equal(script.after_wait, (size_t)0u);

        test_waitable_wake(&waitable);
        check_equal(probe.wakes, (size_t)1u);
        step = coroutine.ops->resume(coroutine.state, &context, &output);
        check_equal(step.kind, CFLOW_STEP_VALUE_AND_DONE);
        check_equal(output, 41);
        check_equal(script.after_wait, (size_t)1u);

        destroy_resumable(&coroutine);
    }

    it("cancels an armed WAIT before terminating the frame") {
        test_waitable_state waitable = {0};
        wait_script_state script = {&waitable, NULL, 0u};
        wake_probe probe = {0};
        cflow_resumable coroutine = {0};
        cflow_minicoro_config config = {
            "cancel", &cmeta_type_int, minicoro_wait_then_value, &script,
            0u, NULL, NULL, NULL
        };
        cflow_resume_ctx context = {0};
        int output = 0;
        cflow_step step;

        check_true(cflow_resumable_from_minicoro(&coroutine, &config));
        step = coroutine.ops->resume(coroutine.state, &context, &output);
        check_equal(step.kind, CFLOW_STEP_WAIT);
        check_true(cflow_waitable_arm(
            &step.waitable, (cflow_waker){count_wake, &probe}));

        coroutine.ops->cancel(coroutine.state);

        check_equal(waitable.cancels, (size_t)1u);
        check_equal(script.after_wait, (size_t)0u);
        step = coroutine.ops->resume(coroutine.state, &context, &output);
        check_equal(step.kind, CFLOW_STEP_DONE);
        check_equal(script.after_wait, (size_t)0u);
        destroy_resumable(&coroutine);
    }

    it("does not enter a cancelled frame through a stale waker") {
        test_waitable_state waitable = {0};
        wait_script_state script = {&waitable, NULL, 0u};
        wake_probe probe = {0};
        cflow_resumable coroutine = {0};
        cflow_minicoro_config config = {
            "stale", &cmeta_type_int, minicoro_wait_then_value, &script,
            0u, NULL, NULL, NULL
        };
        cflow_resume_ctx context = {0};
        int output = 0;
        cflow_step step;

        check_true(cflow_resumable_from_minicoro(&coroutine, &config));
        step = coroutine.ops->resume(coroutine.state, &context, &output);
        check_equal(step.kind, CFLOW_STEP_WAIT);
        check_true(cflow_waitable_arm(
            &step.waitable, (cflow_waker){count_wake, &probe}));
        coroutine.ops->cancel(coroutine.state);

        waitable.stale.wake(waitable.stale.user);

        check_equal(probe.wakes, (size_t)1u);
        check_equal(script.after_wait, (size_t)0u);
        step = coroutine.ops->resume(coroutine.state, &context, &output);
        check_equal(step.kind, CFLOW_STEP_DONE);
        check_equal(script.after_wait, (size_t)0u);
        destroy_resumable(&coroutine);
    }

    it("does not retain a frame through a stale waker after destroy") {
        test_waitable_state waitable = {0};
        wait_script_state script = {&waitable, NULL, 0u};
        wake_probe probe = {0};
        cflow_resumable coroutine = {0};
        cflow_minicoro_config config = {
            "destroy-stale", &cmeta_type_int, minicoro_wait_then_value, &script,
            0u, NULL, NULL, NULL
        };
        cflow_resume_ctx context = {0};
        int output = 0;
        cflow_step step;

        check_true(cflow_resumable_from_minicoro(&coroutine, &config));
        step = coroutine.ops->resume(coroutine.state, &context, &output);
        check_equal(step.kind, CFLOW_STEP_WAIT);
        check_true(cflow_waitable_arm(
            &step.waitable, (cflow_waker){count_wake, &probe}));

        destroy_resumable(&coroutine);
        waitable.stale.wake(waitable.stale.user);

        check_equal(waitable.cancels, (size_t)1u);
        check_equal(probe.wakes, (size_t)1u);
        check_equal(script.after_wait, (size_t)0u);
        check_null(coroutine.ops);
    }

    it("allows a wake callback to close a suspended frame") {
        test_waitable_state waitable = {0};
        wait_script_state script = {&waitable, NULL, 0u};
        cflow_resumable coroutine = {0};
        destroy_from_wake_state close_state = {&coroutine, 0u};
        cflow_minicoro_config config = {
            "close", &cmeta_type_int, minicoro_wait_then_value, &script,
            0u, NULL, NULL, NULL
        };
        cflow_resume_ctx context = {0};
        int output = 0;
        cflow_step step;

        check_true(cflow_resumable_from_minicoro(&coroutine, &config));
        step = coroutine.ops->resume(coroutine.state, &context, &output);
        check_equal(step.kind, CFLOW_STEP_WAIT);
        check_true(cflow_waitable_arm(
            &step.waitable, (cflow_waker){destroy_from_wake, &close_state}));

        test_waitable_wake(&waitable);

        check_equal(close_state.calls, (size_t)1u);
        check_equal(script.after_wait, (size_t)0u);
        check_null(coroutine.ops);
    }

    it("releases partial state when frame allocation fails") {
        counting_allocator allocator = {0u, 0u, 1u};
        cflow_resumable coroutine = {0};
        cflow_minicoro_config config = {
            "oom", &cmeta_type_int, minicoro_complete, NULL, 0u,
            counting_alloc, counting_dealloc, &allocator
        };

        check_false(cflow_resumable_from_minicoro(&coroutine, &config));
        check_equal(allocator.allocations, (size_t)2u);
        check_equal(allocator.deallocations, (size_t)1u);
        check_null(coroutine.ops);
    }

    it("leaves output empty when adapter allocation fails") {
        counting_allocator allocator = {0u, 0u, 0u};
        cflow_resumable coroutine = {0};
        cflow_minicoro_config config = {
            "state-oom", &cmeta_type_int, minicoro_complete, NULL, 0u,
            counting_alloc, counting_dealloc, &allocator
        };

        check_false(cflow_resumable_from_minicoro(&coroutine, &config));
        check_equal(allocator.allocations, (size_t)1u);
        check_equal(allocator.deallocations, (size_t)0u);
        check_null(coroutine.ops);
    }

    it("balances adapter and frame allocations across repeated lifecycles") {
        enum { LIFECYCLE_COUNT = 128 };
        counting_allocator allocator = {0u, 0u, SIZE_MAX};
        cflow_minicoro_config config = {
            "repeat", &cmeta_type_int, minicoro_complete, NULL, 0u,
            counting_alloc, counting_dealloc, &allocator
        };

        for (size_t index = 0u; index < LIFECYCLE_COUNT; ++index) {
            cflow_resumable coroutine = {0};

            check_true(cflow_resumable_from_minicoro(&coroutine, &config));
            destroy_resumable(&coroutine);
        }

        check_equal(allocator.allocations,
                    (size_t)LIFECYCLE_COUNT * 2u);
        check_equal(allocator.deallocations, allocator.allocations);
    }
}
