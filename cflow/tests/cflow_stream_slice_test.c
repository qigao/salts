#include <cflow/cflow.h>
#include "tinytest.h"

#include <string.h>

static size_t slice_expand_calls;

typed(flatMap, value, cmeta_gen_status, slice_expand_four,
      (int value, long *out, size_t *cursor)) {
    if (*cursor >= 4u) return CMETA_GEN_DONE;
    *out = (long)value * 10L + (long)*cursor;
    ++*cursor;
    ++slice_expand_calls;
    return *cursor == 4u ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE;
}

typedef struct slice_probe_source {
    const int *values;
    size_t count;
    size_t index;
    size_t resumes;
    size_t cancels;
    size_t fail_on_resume;
} slice_probe_source;

typedef struct slice_probe_sink {
    int values[4];
    size_t count;
    size_t done;
    const char *error;
} slice_probe_sink;

static const char *slice_probe_name(void *self) {
    (void)self;
    return "slice-probe";
}

static const cmeta_type_desc *slice_probe_type(void *self) {
    return self ? &cmeta_type_int : NULL;
}

static cflow_step slice_probe_resume(void *self,
                                     cflow_publish_context *ctx,
                                     void *out_value) {
    slice_probe_source *source = (slice_probe_source *)self;
    (void)ctx;
    if (!source || !out_value)
        return (cflow_step){CFLOW_STEP_ERROR, {0}, "probe source is invalid"};
    ++source->resumes;
    if (source->fail_on_resume == source->resumes)
        return (cflow_step){CFLOW_STEP_ERROR, {0}, "probe source failed"};
    if (source->index >= source->count)
        return (cflow_step){CFLOW_STEP_DONE, {0}, NULL};
    memcpy(out_value, &source->values[source->index], sizeof(int));
    ++source->index;
    return (cflow_step){
        source->index == source->count
            ? CFLOW_STEP_VALUE_AND_DONE : CFLOW_STEP_VALUE,
        {0}, NULL};
}

static void slice_probe_cancel(void *self) {
    slice_probe_source *source = (slice_probe_source *)self;
    if (source) ++source->cancels;
}

static void slice_probe_destroy(void *self) { (void)self; }
static void slice_probe_bind(void *self, cflow_waker waker) {
    (void)self;
    (void)waker;
}
static cflow_publisher_terminal slice_probe_poll(void *self,
                                              const char **error) {
    (void)self;
    if (error) *error = NULL;
    return CFLOW_PUBLISHER_OPEN;
}

CMETA_IMPLEMENTS(cflow_publisher, slice_probe_source_interface,
    CFLOW_PUBLISHER_CAP_CONSTRUCTS_VALUES,
    .name = slice_probe_name,
    .output_type = slice_probe_type,
    .resume = slice_probe_resume,
    .cancel = slice_probe_cancel,
    .destroy = slice_probe_destroy,
    .bind_terminal_waker = slice_probe_bind,
    .poll_terminal = slice_probe_poll
);

static bool slice_probe_on_value(void *user,
                                 const cmeta_type_desc *type,
                                 const void *value) {
    slice_probe_sink *sink = (slice_probe_sink *)user;
    if (!sink || !cmeta_type_equal(type, &cmeta_type_int) || !value ||
        sink->count >= sizeof(sink->values) / sizeof(sink->values[0]))
        return false;
    sink->values[sink->count++] = *(const int *)value;
    return true;
}

static void slice_probe_on_error(void *user, const char *message) {
    slice_probe_sink *sink = (slice_probe_sink *)user;
    if (sink) sink->error = message;
}

static void slice_probe_on_done(void *user) {
    slice_probe_sink *sink = (slice_probe_sink *)user;
    if (sink) ++sink->done;
}

static void run_take_probe(size_t limit,
                           slice_probe_source *source_state,
                           slice_probe_sink *sink_state) {
    cflow_graph graph = {0};
    cflow_publisher source = {0};
    cflow_scheduler scheduler = {0};
    cflow_subscription run = {0};
    cflow_subscriber_callbacks callbacks = {
        slice_probe_on_value,
        slice_probe_on_error,
        slice_probe_on_done,
        sink_state
    };
    cflow_subscriber sink = cflow_subscriber_from_callbacks(&callbacks);

    cflow_graph_init(&graph, &cmeta_type_int);
    check_true(cflow_graph_take(&graph, limit));
    source = slice_probe_source_interface_as_cflow_publisher(source_state);
    check_true(cflow_scheduler_test_init(&scheduler));
    check_true(cflow_subscribe(&run, &graph, &source, &scheduler, &sink));
    check_true(cflow_subscription_request(&run, SIZE_MAX));
    check_greater(cflow_scheduler_run_until_idle(&scheduler, 0u), (size_t)0u);
    check_true(cflow_subscription_is_done(&run));
    check_null(cflow_subscription_error(&run));

    cflow_subscription_close(&run);
    cflow_scheduler_destroy(&scheduler);
    cflow_graph_destroy(&graph);
}

static void check_slice_result(const cflow_result *result,
                               const int *expected,
                               size_t expected_count) {
    check_not_null(result);
    check_true(cmeta_type_equal(result->type, &cmeta_type_int));
    check_equal(result->count, expected_count);
    if (expected_count == 0u) {
        check_null(result->data);
    } else {
        check_equal(result->data, expected, expected_count * sizeof(*expected));
    }
}

spec("CFlow Stream slicing") {
    it("applies skip and take in encounter order") {
        const int input[] = {1, 2, 3, 4, 5, 6};
        const int expected[] = {4, 5};
        cflow_stream stream = {0};
        cflow_result result = {0};
        cflow_status_result status;

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.skip(&stream, 3u)->take(&stream, 2u));
        status = cflow_eval_array_result(
            &stream.graph, input, 6u, &result);
        check_true(cflow_status_result_is_ok(status));
        check_equal(status.status, CFLOW_STATUS_OK);
        check_slice_result(&result, expected, 2u);

        cflow_result_destroy(&result);
        cflow_stream_destroy(&stream);
    }

    it("handles zero and out of range bounds") {
        const int input[] = {7, 8};
        cflow_stream take_zero = {0};
        cflow_stream skip_all = {0};
        cflow_result result = {0};

        check_not_null(cflow_stream_init(&take_zero, &cmeta_type_int));
        check_not_null(take_zero.take(&take_zero, 0u));
        check_true(cflow_eval_array(&take_zero.graph, input, 2u, &result));
        check_slice_result(&result, NULL, 0u);
        cflow_result_destroy(&result);

        check_not_null(cflow_stream_init(&skip_all, &cmeta_type_int));
        check_not_null(skip_all.skip(&skip_all, 3u));
        check_true(cflow_eval_array(&skip_all.graph, input, 2u, &result));
        check_slice_result(&result, NULL, 0u);

        cflow_result_destroy(&result);
        cflow_stream_destroy(&skip_all);
        cflow_stream_destroy(&take_zero);
    }

    it("keeps interpreted and compiled executions equivalent") {
        const int input[] = {10, 20, 30, 40, 50};
        const int expected[] = {20, 30, 40};
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_result interpreted = {0};
        cflow_result compiled = {0};
        cflow_verify_report report = {0};

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.skip(&stream, 1u)->take(&stream, 3u));
        check_true(cflow_eval_array(&stream.graph, input, 5u, &interpreted));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        check_true(cflow_plan_eval_array(&plan, input, 5u, &compiled));
        check_slice_result(&interpreted, expected, 3u);
        check_true(cflow_result_equal(&interpreted, &compiled));
        check_true(cflow_verify_pipeline(&stream.graph, input, 5u, &report));
        check_true(report.compiled_plan_checked);
        check_equal(report.compiled_instructions, (size_t)2u);

        cflow_result_destroy(&compiled);
        cflow_result_destroy(&interpreted);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("resets positional state for every evaluation") {
        const int input[] = {1, 2, 3};
        const int expected[] = {2};
        cflow_stream stream = {0};
        cflow_result first = {0};
        cflow_result second = {0};

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.skip(&stream, 1u)->take(&stream, 1u));
        check_true(cflow_eval_array(&stream.graph, input, 3u, &first));
        check_true(cflow_eval_array(&stream.graph, input, 3u, &second));
        check_slice_result(&first, expected, 1u);
        check_true(cflow_result_equal(&first, &second));

        cflow_result_destroy(&second);
        cflow_result_destroy(&first);
        cflow_stream_destroy(&stream);
    }

    it("declares deterministic stateful slice semantics") {
        cflow_stream stream = {0};
        const cmeta_properties expected_properties =
            CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL |
            CMETA_PROP_NO_ALIAS;

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.skip(&stream, 1u)->take(&stream, 2u));
        check_true((cflow_graph_effects(&stream.graph) &
                    CMETA_EFFECT_STATEFUL) != 0u);
        check_true(cmeta_properties_include(
            cflow_graph_properties(&stream.graph), expected_properties));

        cflow_stream_destroy(&stream);
    }

    it("does not pull the source for take zero") {
        const int input[] = {1, 2, 3};
        slice_probe_source source = {input, 3u, 0u, 0u, 0u, 0u};
        slice_probe_sink sink = {0};

        run_take_probe(0u, &source, &sink);

        check_equal(source.resumes, (size_t)0u);
        check_equal(source.cancels, (size_t)1u);
        check_equal(sink.count, (size_t)0u);
        check_equal(sink.done, (size_t)1u);
        check_null(sink.error);
    }

    it("cancels upstream immediately after the take bound") {
        const int input[] = {1, 2, 3, 4};
        const int expected[] = {1, 2};
        slice_probe_source source = {input, 4u, 0u, 0u, 0u, 0u};
        slice_probe_sink sink = {0};

        run_take_probe(2u, &source, &sink);

        check_equal(source.resumes, (size_t)2u);
        check_equal(source.cancels, (size_t)1u);
        check_equal(sink.count, (size_t)2u);
        check_equal(sink.values, expected, sizeof(expected));
        check_equal(sink.done, (size_t)1u);
        check_null(sink.error);
    }

    it("does not observe a source error after the take bound") {
        const int input[] = {5, 6, 7};
        slice_probe_source source = {input, 3u, 0u, 0u, 0u, 2u};
        slice_probe_sink sink = {0};

        run_take_probe(1u, &source, &sink);

        check_equal(source.resumes, (size_t)1u);
        check_equal(source.cancels, (size_t)1u);
        check_equal(sink.count, (size_t)1u);
        check_equal(sink.values[0], 5);
        check_equal(sink.done, (size_t)1u);
        check_null(sink.error);
    }

    it("stops an active upstream generator at the take bound") {
        const int input[] = {1, 2};
        const long expected[] = {10L, 11L};
        cflow_stream stream = {0};
        cflow_plan unsupported = {0};
        cflow_result result = {0};

        slice_expand_calls = 0u;
        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.flatMap(&stream, slice_expand_four)
                                   ->take(&stream, 2u));
        check_true(cflow_eval_array(&stream.graph, input, 2u, &result));
        check_true(cmeta_type_equal(result.type, &cmeta_type_long));
        check_equal(result.count, (size_t)2u);
        check_equal(result.data, expected, sizeof(expected));
        check_equal(slice_expand_calls, (size_t)2u);
        check_false(cflow_plan_compile_surface(
            &unsupported, &stream.graph, NULL));
        check_equal(unsupported.error,
                    "Graph mixes slice and callable plan semantics");

        cflow_result_destroy(&result);
        cflow_plan_destroy(&unsupported);
        cflow_stream_destroy(&stream);
    }

    it("allows a downstream generator to finish after take") {
        const int input[] = {2, 3};
        const long expected[] = {20L, 21L, 22L, 23L};
        cflow_stream stream = {0};
        cflow_result result = {0};

        slice_expand_calls = 0u;
        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.take(&stream, 1u)
                                   ->flatMap(&stream, slice_expand_four));
        check_true(cflow_eval_array(&stream.graph, input, 2u, &result));
        check_true(cmeta_type_equal(result.type, &cmeta_type_long));
        check_equal(result.count, (size_t)4u);
        check_equal(result.data, expected, sizeof(expected));
        check_equal(slice_expand_calls, (size_t)4u);

        cflow_result_destroy(&result);
        cflow_stream_destroy(&stream);
    }
}
