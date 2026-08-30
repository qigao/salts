#include <cflow/publishers.h>
#include <cflow/temporal.h>
#include <cflow/lower.h>

#include "tinytest.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define TEMPORAL_WORKER_DESTROY_ITERATIONS 128u

typedef struct wake_probe {
    size_t wakes;
} wake_probe;

static void record_wake(void *user) {
    wake_probe *probe = (wake_probe *)user;
    if (probe != NULL) ++probe->wakes;
}

static void record_atomic_wake(void *user) {
    atomic_size_t *wakes = (atomic_size_t *)user;
    if (wakes != NULL)
        (void)atomic_fetch_add_explicit(wakes, 1u, memory_order_relaxed);
}

static bool arm_step(cflow_step step, wake_probe *probe) {
    const cflow_waker waker = {record_wake, probe};
    return step.kind == CFLOW_STEP_WAIT &&
           cflow_waitable_valid(&step.waitable) &&
           cflow_waitable_arm(&step.waitable, waker);
}

typedef struct temporal_owned_value {
    int *resource;
} temporal_owned_value;

static size_t temporal_owned_copies;
static size_t temporal_owned_moves;
static size_t temporal_owned_destroys;

static bool temporal_owned_copy(void *destination_, const void *source_) {
    temporal_owned_value *destination =
        (temporal_owned_value *)destination_;
    const temporal_owned_value *source =
        (const temporal_owned_value *)source_;
    ++temporal_owned_copies;
    destination->resource = (int *)malloc(sizeof(*destination->resource));
    if (destination->resource == NULL) return false;
    *destination->resource = *source->resource;
    return true;
}

static void temporal_owned_move(void *destination_, void *source_) {
    temporal_owned_value *destination =
        (temporal_owned_value *)destination_;
    temporal_owned_value *source = (temporal_owned_value *)source_;
    ++temporal_owned_moves;
    destination->resource = source->resource;
    source->resource = NULL;
}

static void temporal_owned_destroy(void *value_) {
    temporal_owned_value *value = (temporal_owned_value *)value_;
    ++temporal_owned_destroys;
    free(value->resource);
    value->resource = NULL;
}

static const cmeta_type_traits temporal_owned_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = temporal_owned_copy,
    .move_construct = temporal_owned_move,
    .destroy = temporal_owned_destroy
};

static const cmeta_type_desc temporal_owned_type = {
    .name = "temporal_owned_value",
    .size = sizeof(temporal_owned_value),
    .align = _Alignof(temporal_owned_value),
    .kind = CMETA_T_OBJECT,
    .traits = &temporal_owned_traits
};

typedef struct temporal_sink_probe {
    size_t values;
    size_t dones;
    size_t errors;
    int last;
} temporal_sink_probe;

static bool temporal_sink_value(void *user,
                                const cmeta_type_desc *type,
                                const void *value) {
    temporal_sink_probe *probe = (temporal_sink_probe *)user;
    if (probe == NULL || !cmeta_type_equal(type, &cmeta_type_int) ||
        value == NULL)
        return false;
    ++probe->values;
    probe->last = *(const int *)value;
    return true;
}

static void temporal_sink_error(void *user, const char *message) {
    temporal_sink_probe *probe = (temporal_sink_probe *)user;
    if (probe != NULL && message != NULL) ++probe->errors;
}

static void temporal_sink_done(void *user) {
    temporal_sink_probe *probe = (temporal_sink_probe *)user;
    if (probe != NULL) ++probe->dones;
}

static void close_temporal_channel(void *user) {
    cflow_channel_close((cflow_channel *)user);
}

suite("CFlow bounded temporal Sources") {
    it("delays one value until the exact monotonic scheduler boundary") {
        const int values[] = {7};
        cflow_publisher inner = {0};
        cflow_publisher delayed = {0};
        cflow_scheduler scheduler = {0};
        cflow_publish_context context;
        cflow_step step;
        wake_probe probe = {0};
        int output = 0;

        check_true(cflow_publisher_from_array(
            &inner, &cmeta_type_int, values, 1u));
        check_true(cflow_publisher_delay(
            &delayed, &inner, cflow_duration_from_ms(10u)));
        check_false(cflow_publisher_valid(&inner));
        check_true(cflow_scheduler_test_init(&scheduler));
        context = (cflow_publish_context){&scheduler};
        step = cflow_publisher_resume(&delayed, &context, &output);
        check_true(arm_step(step, &probe));
        check_equal(cflow_scheduler_advance(&scheduler, 9u), (size_t)0u);
        check_equal(probe.wakes, (size_t)0u);
        check_equal(cflow_scheduler_advance(&scheduler, 1u), (size_t)1u);
        check_equal(probe.wakes, (size_t)1u);
        step = cflow_publisher_resume(&delayed, &context, &output);
        check_equal(step.kind, CFLOW_STEP_VALUE_AND_DONE);
        check_equal(output, 7);

        cflow_publisher_destroy(&delayed);
        cflow_scheduler_destroy(&scheduler);
    }

    it("debounces to the latest value and flushes final input immediately") {
        cflow_channel channel = {0};
        cflow_publisher inner = {0};
        cflow_publisher debounced = {0};
        cflow_scheduler scheduler = {0};
        cflow_publish_context context;
        cflow_step step;
        wake_probe probe = {0};
        int value = 1;
        int output = 0;

        check_true(cflow_channel_init(&channel, &cmeta_type_int, 2u));
        check_true(cflow_publisher_from_channel(&inner, &channel));
        check_true(cflow_publisher_debounce(
            &debounced, &inner, cflow_duration_from_ms(10u)));
        check_true(cflow_scheduler_test_init(&scheduler));
        context = (cflow_publish_context){&scheduler};
        check_true(cflow_channel_push(&channel, &value));
        step = cflow_publisher_resume(&debounced, &context, &output);
        check_true(arm_step(step, &probe));

        value = 2;
        check_true(cflow_channel_push(&channel, &value));
        check_equal(probe.wakes, (size_t)1u);
        step = cflow_publisher_resume(&debounced, &context, &output);
        check_true(arm_step(step, &probe));
        check_equal(cflow_scheduler_advance(&scheduler, 9u), (size_t)0u);
        check_equal(cflow_scheduler_advance(&scheduler, 1u), (size_t)1u);
        check_equal(probe.wakes, (size_t)2u);
        step = cflow_publisher_resume(&debounced, &context, &output);
        check_equal(step.kind, CFLOW_STEP_VALUE);
        check_equal(output, 2);

        value = 3;
        check_true(cflow_channel_push(&channel, &value));
        cflow_channel_close(&channel);
        step = cflow_publisher_resume(&debounced, &context, &output);
        check_equal(step.kind, CFLOW_STEP_VALUE_AND_DONE);
        check_equal(output, 3);

        cflow_publisher_destroy(&debounced);
        cflow_channel_destroy(&channel);
        cflow_scheduler_destroy(&scheduler);
    }

    it("times out only an upstream WAIT and reports a stable error") {
        cflow_channel channel = {0};
        cflow_publisher inner = {0};
        cflow_publisher timed = {0};
        cflow_scheduler scheduler = {0};
        cflow_publish_context context;
        cflow_step step;
        wake_probe probe = {0};
        int output = 0;

        check_true(cflow_channel_init(&channel, &cmeta_type_int, 1u));
        check_true(cflow_publisher_from_channel(&inner, &channel));
        check_true(cflow_publisher_timeout(
            &timed, &inner, cflow_duration_from_ms(5u)));
        check_true(cflow_scheduler_test_init(&scheduler));
        context = (cflow_publish_context){&scheduler};
        step = cflow_publisher_resume(&timed, &context, &output);
        check_true(arm_step(step, &probe));
        check_equal(cflow_scheduler_advance(&scheduler, 5u), (size_t)1u);
        check_equal(probe.wakes, (size_t)1u);
        step = cflow_publisher_resume(&timed, &context, &output);
        check_equal(step.kind, CFLOW_STEP_ERROR);
        check_not_null(step.error);
        check_equal(strcmp(step.error, "temporal source timed out"), 0);

        cflow_publisher_destroy(&timed);
        cflow_channel_destroy(&channel);
        cflow_scheduler_destroy(&scheduler);
    }

    it("observes upstream completion before an accumulated timeout") {
        cflow_channel channel = {0};
        cflow_publisher inner = {0};
        cflow_publisher timed = {0};
        cflow_scheduler scheduler = {0};
        cflow_publish_context context;
        cflow_step step;
        wake_probe probe = {0};
        const char *terminal_error = NULL;
        int output = 0;

        check_true(cflow_channel_init(&channel, &cmeta_type_int, 1u));
        check_true(cflow_publisher_from_channel(&inner, &channel));
        check_true(cflow_publisher_timeout(
            &timed, &inner, cflow_duration_from_ms(5u)));
        check_true(cflow_scheduler_test_init(&scheduler));
        context = (cflow_publish_context){&scheduler};
        step = cflow_publisher_resume(&timed, &context, &output);
        check_true(arm_step(step, &probe));
        check(cflow_scheduler_post_after(
                  &scheduler, 5u, close_temporal_channel, &channel) != 0u);
        check_equal(cflow_scheduler_advance(&scheduler, 5u), (size_t)2u);
        check_equal(cflow_publisher_poll_terminal(
                        &timed, &terminal_error),
                    CFLOW_PUBLISHER_OPEN);
        step = cflow_publisher_resume(&timed, &context, &output);
        check_equal(step.kind, CFLOW_STEP_DONE);

        cflow_publisher_destroy(&timed);
        cflow_channel_destroy(&channel);
        cflow_scheduler_destroy(&scheduler);
    }

    it("keeps upstream-first observation when completion is queued first") {
        cflow_channel channel = {0};
        cflow_publisher inner = {0};
        cflow_publisher timed = {0};
        cflow_scheduler scheduler = {0};
        cflow_publish_context context;
        cflow_step step;
        wake_probe probe = {0};
        int output = 0;

        check_true(cflow_channel_init(&channel, &cmeta_type_int, 1u));
        check_true(cflow_publisher_from_channel(&inner, &channel));
        check_true(cflow_publisher_timeout(
            &timed, &inner, cflow_duration_from_ms(5u)));
        check_true(cflow_scheduler_test_init(&scheduler));
        context = (cflow_publish_context){&scheduler};
        check(cflow_scheduler_post_after(
                  &scheduler, 5u, close_temporal_channel, &channel) != 0u);
        step = cflow_publisher_resume(&timed, &context, &output);
        check_true(arm_step(step, &probe));
        check_equal(cflow_scheduler_advance(&scheduler, 5u), (size_t)2u);
        check(probe.wakes >= (size_t)1u);
        step = cflow_publisher_resume(&timed, &context, &output);
        check_equal(step.kind, CFLOW_STEP_DONE);

        cflow_publisher_destroy(&timed);
        cflow_channel_destroy(&channel);
        cflow_scheduler_destroy(&scheduler);
    }

    it("accepts zero duration and leaves ownership unchanged on rejection") {
        const int value = 9;
        cflow_publisher inner = {0};
        cflow_publisher delayed = {0};
        cflow_publisher occupied = {0};
        cflow_scheduler scheduler = {0};
        cflow_publish_context context;
        cflow_step step;
        wake_probe probe = {0};
        int output = 0;

        check_true(cflow_publisher_from_array(
            &inner, &cmeta_type_int, &value, 1u));
        check_true(cflow_publisher_from_array(
            &occupied, &cmeta_type_int, &value, 1u));
        check_false(cflow_publisher_delay(
            &occupied, &inner, cflow_duration_from_ns(0u)));
        check_true(cflow_publisher_valid(&inner));
        cflow_publisher_destroy(&occupied);
        check_true(cflow_publisher_delay(
            &delayed, &inner, cflow_duration_from_ns(0u)));
        check_true(cflow_scheduler_test_init(&scheduler));
        context = (cflow_publish_context){&scheduler};
        step = cflow_publisher_resume(&delayed, &context, &output);
        check_true(arm_step(step, &probe));
        check_equal(cflow_scheduler_run_ready(&scheduler), (size_t)1u);
        step = cflow_publisher_resume(&delayed, &context, &output);
        check_equal(step.kind, CFLOW_STEP_VALUE_AND_DONE);
        check_equal(output, 9);

        cflow_publisher_destroy(&delayed);
        cflow_scheduler_destroy(&scheduler);
    }

    it("cancels an armed managed delay and destroys retained ownership once") {
        temporal_owned_value input = {0};
        cflow_publisher inner = {0};
        cflow_publisher delayed = {0};
        cflow_scheduler scheduler = {0};
        cflow_publish_context context;
        cflow_step step;
        wake_probe probe = {0};
        temporal_owned_value output = {0};

        temporal_owned_copies = 0u;
        temporal_owned_moves = 0u;
        temporal_owned_destroys = 0u;
        input.resource = (int *)malloc(sizeof(*input.resource));
        check_not_null(input.resource);
        *input.resource = 41;
        check_true(cflow_publisher_from_array(
            &inner, &temporal_owned_type, &input, 1u));
        check_true(cflow_publisher_delay(
            &delayed, &inner, cflow_duration_from_ms(10u)));
        check_true(cflow_scheduler_test_init(&scheduler));
        context = (cflow_publish_context){&scheduler};
        step = cflow_publisher_resume(&delayed, &context, &output);
        check_true(arm_step(step, &probe));
        check_equal(cflow_scheduler_pending(&scheduler), (size_t)1u);
        cflow_publisher_cancel(&delayed);
        check_equal(cflow_scheduler_pending(&scheduler), (size_t)0u);
        cflow_publisher_destroy(&delayed);
        temporal_owned_destroy(&input);
        check_equal(temporal_owned_copies, (size_t)1u);
        check_equal(temporal_owned_moves, (size_t)0u);
        check_equal(temporal_owned_destroys, (size_t)2u);
        cflow_scheduler_destroy(&scheduler);
    }

    it("quiesces admitted worker timers before adapter destruction") {
        cflow_scheduler scheduler = {0};
        atomic_size_t wakes;
        size_t iteration;

        atomic_init(&wakes, 0u);
        check_true(cflow_scheduler_worker_init_with_capacity(
            &scheduler, 1u, 32u, 32u));
        for (iteration = 0u;
             iteration < TEMPORAL_WORKER_DESTROY_ITERATIONS;
             ++iteration) {
            const int input = (int)iteration;
            cflow_publisher inner = {0};
            cflow_publisher delayed = {0};
            cflow_publish_context context = {&scheduler};
            cflow_step step;
            int output = 0;

            check_true(cflow_publisher_from_array(
                &inner, &cmeta_type_int, &input, 1u));
            check_true(cflow_publisher_delay(
                &delayed, &inner, cflow_duration_from_ns(0u)));
            step = cflow_publisher_resume(&delayed, &context, &output);
            check_equal(step.kind, CFLOW_STEP_WAIT);
            check_true(cflow_waitable_arm(
                &step.waitable,
                (cflow_waker){record_atomic_wake, &wakes}));
            cflow_publisher_destroy(&delayed);
        }
        check_true(cflow_scheduler_wait_idle(&scheduler));
        cflow_scheduler_destroy(&scheduler);
    }

    it("does not pull or emit through Run without downstream demand") {
        const int input = 17;
        cflow_graph surface = {0};
        cflow_graph graph = {0};
        cflow_publisher inner = {0};
        cflow_publisher delayed = {0};
        cflow_scheduler scheduler = {0};
        cflow_subscription run = {0};
        temporal_sink_probe probe = {0};
        cflow_subscriber_callbacks callbacks = {
            temporal_sink_value,
            temporal_sink_error,
            temporal_sink_done,
            &probe
        };
        cflow_subscriber sink = cflow_subscriber_from_callbacks(&callbacks);

        cflow_graph_init(&surface, &cmeta_type_int);
        check_true(cflow_graph_normalize(&graph, &surface));
        check_true(cflow_publisher_from_array(
            &inner, &cmeta_type_int, &input, 1u));
        check_true(cflow_publisher_delay(
            &delayed, &inner, cflow_duration_from_ms(5u)));
        check_true(cflow_scheduler_test_init(&scheduler));
        check_true(cflow_subscribe(
            &run, &graph, &delayed, &scheduler, &sink));
        check_equal(cflow_scheduler_run_ready(&scheduler), (size_t)0u);
        check_equal(probe.values, (size_t)0u);
        check_true(cflow_subscription_request(&run, 1u));
        check_equal(cflow_scheduler_run_ready(&scheduler), (size_t)1u);
        check_equal(probe.values, (size_t)0u);
        check_equal(cflow_scheduler_advance(&scheduler, 4u), (size_t)0u);
        check_equal(probe.values, (size_t)0u);
        check_greater(cflow_scheduler_advance(&scheduler, 1u), (size_t)0u);
        check_equal(probe.values, (size_t)1u);
        check_equal(probe.last, 17);
        check_equal(probe.errors, (size_t)0u);
        check_equal(probe.dones, (size_t)1u);

        cflow_subscription_close(&run);
        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&graph);
        cflow_graph_destroy(&surface);
    }
}
