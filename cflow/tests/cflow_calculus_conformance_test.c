#include <cflow/cflow.h>
#include "tinytest.h"

#include "cflow_test_ops.h"

#include <string.h>

typedef struct cflow_conformance_trace {
    size_t values[2];
    char events[3];
    size_t value_count;
    size_t event_count;
    bool done;
    const char *error;
} cflow_conformance_trace;

typedef struct cflow_conformance_fixture {
    cflow_stream stream;
    cflow_stream left;
    cflow_stream right;
    cflow_graph graph;
    cflow_graph normalized;
    cflow_graph optimized;
    cflow_plan plan;
    cflow_result plan_result;
    cflow_result kernel_result;
    cflow_scheduler scheduler;
    cflow_publisher source;
    cflow_subscription run;
    cflow_conformance_trace trace;
    cflow_subscriber_callbacks callbacks;
} cflow_conformance_fixture;

static cflow_conformance_fixture cflow_conformance_state;

typed(map, value, double, cflow_conformance_as_double, (int value)) {
    return (double)value + 0.25;
}

typed(zip, value, double, cflow_conformance_merge,
      (long left, double right)) {
    return (double)left + right;
}

static bool cflow_conformance_build_linear(cflow_stream *stream) {
    return cflow_stream_init(stream, &cmeta_type_int) &&
           stream->filter(stream, cflow_test_even) &&
           stream->map(stream, cflow_test_square) &&
           stream->map(stream, cflow_test_half);
}

static bool cflow_conformance_on_size(void *user,
                                      const cmeta_type_desc *type,
                                      const void *value) {
    cflow_conformance_trace *trace = (cflow_conformance_trace *)user;
    if (!trace || !cmeta_type_equal(type, &cmeta_type_size) ||
        trace->value_count >= 2u || trace->event_count >= 3u)
        return false;
    trace->values[trace->value_count++] = *(const size_t *)value;
    trace->events[trace->event_count++] = 'V';
    return true;
}

static void cflow_conformance_on_error(void *user, const char *message) {
    cflow_conformance_trace *trace = (cflow_conformance_trace *)user;
    if (!trace) return;
    trace->error = message;
    if (trace->event_count < 3u) trace->events[trace->event_count++] = 'E';
}

static void cflow_conformance_on_done(void *user) {
    cflow_conformance_trace *trace = (cflow_conformance_trace *)user;
    if (!trace) return;
    trace->done = true;
    if (trace->event_count < 3u) trace->events[trace->event_count++] = 'D';
}

suite("CFlow calculus conformance") {
    before_each() {
        memset(&cflow_conformance_state, 0, sizeof(cflow_conformance_state));
        cflow_conformance_state.normalized.root = CMETA_INVALID_ID;
        cflow_conformance_state.optimized.root = CMETA_INVALID_ID;
    }

    after_each() {
        cflow_conformance_fixture *state = &cflow_conformance_state;

        cflow_subscription_close(&state->run);
        if (cflow_publisher_valid(&state->source))
            cflow_publisher_destroy(&state->source);
        if (cflow_scheduler_valid(&state->scheduler))
            cflow_scheduler_destroy(&state->scheduler);
        cflow_result_destroy(&state->kernel_result);
        cflow_result_destroy(&state->plan_result);
        cflow_plan_destroy(&state->plan);
        cflow_graph_destroy(&state->optimized);
        cflow_graph_destroy(&state->normalized);
        cflow_graph_destroy(&state->graph);
        cflow_stream_destroy(&state->right);
        cflow_stream_destroy(&state->left);
        cflow_stream_destroy(&state->stream);
    }

    group("Plan and Kernel") {
        it("preserves observations for a supported linear pure flow") {
            const int input[] = {1, 2, 3, 4, 5, 6};
            const double expected[] = {2.0, 8.0, 18.0};
            cflow_plan_compile_stats stats = {0};
            cflow_conformance_fixture *state = &cflow_conformance_state;

            check_true(cflow_conformance_build_linear(&state->stream));
            check_true(cflow_plan_compile_surface(
                &state->plan, &state->stream.graph, &stats));
            check_null(state->plan.error);
            check_equal(stats.instructions, (size_t)2u);
            check_equal(stats.map_callbacks, (size_t)2u);

            /* This is the formal Plan path: it still dispatches pre-decoded
             * step handlers, so it is not the calculus' Direct path. */
            check_true(cflow_plan_eval_array(
                &state->plan, input, 6u, &state->plan_result));
            check_true(cflow_eval_array(
                &state->stream.graph, input, 6u, &state->kernel_result));
            check_true(cflow_result_equal(
                &state->plan_result, &state->kernel_result));
            check_equal(state->plan_result.count, (size_t)3u);
            check_true(cmeta_type_equal(
                state->plan_result.type, &cmeta_type_double));
            check_equal(state->plan_result.data, expected, sizeof(expected));
        }
    }

    group("Kernel WAIT") {
        it("preserves WAIT wake value and done observations") {
            cflow_conformance_fixture *state = &cflow_conformance_state;
            state->callbacks = (cflow_subscriber_callbacks){
                cflow_conformance_on_size,
                cflow_conformance_on_error,
                cflow_conformance_on_done,
                &state->trace
            };
            cflow_subscriber sink = cflow_subscriber_from_callbacks(&state->callbacks);

            cflow_graph_init(&state->graph, &cmeta_type_size);
            check_true(cflow_scheduler_test_init(&state->scheduler));
            check_true(cflow_publisher_from_timer(&state->source, 1u, 5u));
            check_true(cflow_subscribe(&state->run,
                                      &state->graph,
                                      &state->source,
                                      &state->scheduler,
                                      &sink));
            check_null(state->source.self);
            check_true(cflow_subscription_request(&state->run, 1u));

            (void)cflow_scheduler_run_ready(&state->scheduler);
            check_equal(cflow_scheduler_now(&state->scheduler), (uint64_t)0u);
            check_equal(state->trace.value_count, (size_t)0u);
            check_false(state->trace.done);
            check_null(state->trace.error);
            check_equal(cflow_subscription_outstanding_demand(&state->run), (size_t)1u);

            (void)cflow_scheduler_advance(&state->scheduler, 4u);
            check_equal(cflow_scheduler_now(&state->scheduler), (uint64_t)4u);
            check_equal(state->trace.value_count, (size_t)0u);
            check_false(state->trace.done);

            (void)cflow_scheduler_advance(&state->scheduler, 1u);
            check_equal(cflow_scheduler_now(&state->scheduler), (uint64_t)5u);
            check_equal(state->trace.value_count, (size_t)1u);
            check_equal(state->trace.values[0], (size_t)0u);
            check_true(state->trace.done);
            check_null(state->trace.error);
            check_equal(state->trace.event_count, (size_t)2u);
            check_equal(state->trace.events[0], 'V');
            check_equal(state->trace.events[1], 'D');
            check_equal(cflow_subscription_outstanding_demand(&state->run), (size_t)0u);
        }
    }

    group("unsupported Plan semantics") {
        it("rejects relation compilation without hiding the Kernel path") {
            const int input[] = {1, 2, 3};
            const double expected[] = {2.25, 6.25, 12.25};
            cflow_conformance_fixture *state = &cflow_conformance_state;

            check_not_null(cflow_stream_init(&state->left, &cmeta_type_int));
            check_not_null(cflow_stream_init(&state->right, &cmeta_type_int));
            check_not_null(state->left.map(&state->left, cflow_test_square));
            check_not_null(state->right.map(
                &state->right, cflow_conformance_as_double));
            check_not_null(state->left.zip(
                &state->left, &state->right, cflow_conformance_merge));
            check_true(cflow_graph_normalize(
                &state->normalized, &state->left.graph));
            check_true(cflow_graph_optimize(
                &state->optimized,
                &state->normalized,
                (cflow_opt_options){CMETA_OPT_DEFAULT},
                NULL));

            check_false(cflow_plan_graph_supported(&state->optimized));
            check_false(cflow_plan_compile(
                &state->plan, &state->optimized, NULL));
            check_null(state->plan.impl);
            check_not_null(state->plan.error);

            check_true(cflow_eval_array(
                &state->optimized, input, 3u, &state->kernel_result));
            check_equal(state->kernel_result.count, (size_t)3u);
            check_true(cmeta_type_equal(
                state->kernel_result.type, &cmeta_type_double));
            check_equal(state->kernel_result.data, expected, sizeof(expected));
        }
    }
}
