#include <cflow/cflow.h>

#include "tinytest.h"

#include <string.h>

typed(filter, value, bool, cflow_slice_even, (int value)) {
    return value % 2 == 0;
}

typed(flatMap, value, cmeta_gen_status, cflow_slice_expand,
      (int value, long *out, size_t *cursor)) {
    if (*cursor >= 3u) return CMETA_GEN_DONE;
    if (*cursor == 0u) *out = (long)value;
    else if (*cursor == 1u) *out = (long)value * 10L;
    else *out = (long)value * 100L;
    ++*cursor;
    return *cursor == 3u ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE;
}

typedef struct slice_source_probe {
    size_t resumes;
    size_t cancels;
    size_t fail_on_resume;
    int next;
} slice_source_probe;

static const char *slice_probe_source_name(void *state) {
    (void)state;
    return "slice-probe";
}

static const cmeta_type_desc *slice_probe_source_type(void *state) {
    (void)state;
    return &cmeta_type_int;
}

static cflow_step slice_probe_source_resume(
    void *state, cflow_resume_ctx *ctx, void *out_value) {
    slice_source_probe *probe = (slice_source_probe *)state;

    (void)ctx;
    if (!probe || !out_value)
        return (cflow_step){
            CFLOW_STEP_ERROR, {0}, "slice probe is invalid"};
    ++probe->resumes;
    if (probe->fail_on_resume != 0u &&
        probe->resumes == probe->fail_on_resume)
        return (cflow_step){
            CFLOW_STEP_ERROR, {0}, "slice probe failure"};
    *(int *)out_value = probe->next++;
    return (cflow_step){CFLOW_STEP_VALUE, {0}, NULL};
}

static void slice_probe_source_cancel(void *state) {
    slice_source_probe *probe = (slice_source_probe *)state;
    if (probe) ++probe->cancels;
}

static void slice_probe_source_noop(void *state) {
    (void)state;
}

static void slice_probe_source_bind(void *state, cflow_waker waker) {
    (void)state;
    (void)waker;
}

static cflow_source_terminal slice_probe_source_poll(
    void *state, const char **error) {
    (void)state;
    if (error) *error = NULL;
    return CFLOW_SOURCE_OPEN;
}

CMETA_IMPLEMENTS(cflow_source, slice_probe_source, 0,
    .name = slice_probe_source_name,
    .output_type = slice_probe_source_type,
    .resume = slice_probe_source_resume,
    .cancel = slice_probe_source_cancel,
    .destroy = slice_probe_source_noop,
    .bind_terminal_waker = slice_probe_source_bind,
    .poll_terminal = slice_probe_source_poll
);

typedef struct slice_sink_probe {
    int values[4];
    size_t value_count;
    size_t done_count;
    const char *error;
} slice_sink_probe;

static bool slice_probe_sink_value(void *user,
                                   const cmeta_type_desc *type,
                                   const void *value) {
    slice_sink_probe *probe = (slice_sink_probe *)user;
    if (!probe || !value ||
        !cmeta_type_equal(type, &cmeta_type_int) ||
        probe->value_count >= 4u)
        return false;
    probe->values[probe->value_count++] = *(const int *)value;
    return true;
}

static void slice_probe_sink_error(void *user, const char *message) {
    slice_sink_probe *probe = (slice_sink_probe *)user;
    if (probe) probe->error = message;
}

static void slice_probe_sink_done(void *user) {
    slice_sink_probe *probe = (slice_sink_probe *)user;
    if (probe) ++probe->done_count;
}

typedef struct slice_run_fixture {
    cflow_stream stream;
    cflow_graph normalized;
    cflow_scheduler scheduler;
    cflow_run run;
    slice_sink_probe sink_probe;
    cflow_sink_callbacks callbacks;
    cflow_sink sink;
} slice_run_fixture;

static bool slice_run_fixture_init(slice_run_fixture *fixture,
                                   slice_source_probe *source_probe,
                                   size_t limit) {
    cflow_source source;

    if (!fixture || !source_probe) return false;
    memset(fixture, 0, sizeof(*fixture));
    fixture->normalized.root = CMETA_INVALID_ID;
    if (!cflow_stream_init(&fixture->stream, &cmeta_type_int) ||
        !fixture->stream.take(&fixture->stream, limit) ||
        !cflow_graph_normalize(
            &fixture->normalized, &fixture->stream.graph) ||
        !cflow_scheduler_test_init(&fixture->scheduler))
        return false;
    fixture->callbacks = (cflow_sink_callbacks){
        slice_probe_sink_value,
        slice_probe_sink_error,
        slice_probe_sink_done,
        &fixture->sink_probe
    };
    fixture->sink = cflow_sink_from_callbacks(&fixture->callbacks);
    source = slice_probe_source_as_cflow_source(source_probe);
    return cflow_run_open(
        &fixture->run, &fixture->normalized, &source,
        &fixture->scheduler, &fixture->sink);
}

static void slice_run_fixture_destroy(slice_run_fixture *fixture) {
    if (!fixture) return;
    cflow_run_close(&fixture->run);
    cflow_scheduler_destroy(&fixture->scheduler);
    cflow_graph_destroy(&fixture->normalized);
    cflow_stream_destroy(&fixture->stream);
}

static void check_slice_eval(const cflow_stream *stream,
                             const int *input,
                             size_t input_count,
                             const int *expected,
                             size_t expected_count) {
    cflow_result result = {0};

    check_true(cflow_eval_array(
        cflow_stream_graph(stream), input, input_count, &result));
    check_equal(result.count, expected_count);
    check_true(cmeta_type_equal(result.type, &cmeta_type_int));
    if (expected_count == 0u) {
        check_null(result.data);
    } else {
        check_not_null(result.data);
        check_equal(result.data, expected, expected_count * sizeof(*expected));
    }
    cflow_result_destroy(&result);
}

spec("CFlow Stream slicing") {
    it("composes skip and take in encounter order") {
        const int input[] = {1, 2, 3, 4, 5};
        const int expected[] = {3, 4};
        cflow_stream stream = {0};
        cflow_result result = {0};

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.skip(&stream, 2u)->take(&stream, 2u));
        check_true(cflow_eval_array(
            cflow_stream_graph(&stream), input, 5u, &result));
        check_equal(result.count, (size_t)2u);
        check_equal(result.data, expected, sizeof(expected));

        cflow_result_destroy(&result);
        cflow_stream_destroy(&stream);
    }

    it("handles zero exact and oversized bounds") {
        const int input[] = {1, 2, 3};
        const int all[] = {1, 2, 3};
        cflow_stream stream = {0};

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.take(&stream, 2u));
        check_slice_eval(&stream, input, 0u, NULL, 0u);
        cflow_stream_destroy(&stream);

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.skip(&stream, 0u));
        check_slice_eval(&stream, input, 3u, all, 3u);
        cflow_stream_destroy(&stream);

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.skip(&stream, 3u));
        check_slice_eval(&stream, input, 3u, NULL, 0u);
        cflow_stream_destroy(&stream);

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.skip(&stream, 4u));
        check_slice_eval(&stream, input, 3u, NULL, 0u);
        cflow_stream_destroy(&stream);

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.take(&stream, 0u));
        check_slice_eval(&stream, input, 3u, NULL, 0u);
        cflow_stream_destroy(&stream);

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.take(&stream, 3u));
        check_slice_eval(&stream, input, 3u, all, 3u);
        cflow_stream_destroy(&stream);
    }

    it("counts values at each slice position") {
        const int input[] = {1, 2, 3, 4, 5, 6, 7};
        const int filtered[] = {4, 6};
        const int take_then_skip[] = {2, 3};
        cflow_stream stream = {0};

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.filter(&stream, cflow_slice_even)
                                 ->skip(&stream, 1u)
                                 ->take(&stream, 2u));
        check_slice_eval(&stream, input, 7u, filtered, 2u);
        cflow_stream_destroy(&stream);

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.take(&stream, 3u)->skip(&stream, 1u));
        check_slice_eval(&stream, input, 7u, take_then_skip, 2u);
        cflow_stream_destroy(&stream);
    }

    it("short circuits only work upstream of the take position") {
        const int input[] = {2, 3};
        const long expanded_taken_input[] = {2L, 20L, 200L};
        const long taken_expansion[] = {2L, 20L};
        cflow_stream stream = {0};
        cflow_result result = {0};

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.take(&stream, 1u)
                                 ->flatMap(&stream, cflow_slice_expand));
        check_true(cflow_eval_array(
            cflow_stream_graph(&stream), input, 2u, &result));
        check_equal(result.count, (size_t)3u);
        check_true(cmeta_type_equal(result.type, &cmeta_type_long));
        check_equal(result.data, expanded_taken_input,
                    sizeof(expanded_taken_input));
        cflow_result_destroy(&result);
        cflow_stream_destroy(&stream);

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.flatMap(&stream, cflow_slice_expand)
                                 ->take(&stream, 2u));
        check_true(cflow_eval_array(
            cflow_stream_graph(&stream), input, 2u, &result));
        check_equal(result.count, (size_t)2u);
        check_true(cmeta_type_equal(result.type, &cmeta_type_long));
        check_equal(result.data, taken_expansion, sizeof(taken_expansion));
        cflow_result_destroy(&result);
        cflow_stream_destroy(&stream);
    }

    it("resets slice positions for every evaluation") {
        const int input[] = {1, 2, 3, 4};
        const int expected[] = {2, 3};
        cflow_stream stream = {0};

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.skip(&stream, 1u)->take(&stream, 2u));
        check_slice_eval(&stream, input, 4u, expected, 2u);
        check_slice_eval(&stream, input, 4u, expected, 2u);
        cflow_stream_destroy(&stream);
    }

    it("preserves interpreted parity and rejects direct plans") {
        const int input[] = {1, 2, 3, 4, 5};
        cflow_stream stream = {0};
        cflow_graph normalized = {0};
        cflow_verify_report report = {0};
        cflow_plan plan = {0};

        normalized.root = CMETA_INVALID_ID;
        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.skip(&stream, 1u)->take(&stream, 2u));
        check_true(cflow_graph_normalize(&normalized, &stream.graph));
        check_false(cflow_plan_graph_supported(&normalized));
        check_false(cflow_plan_compile(&plan, &normalized, NULL));
        check_true(cflow_verify_pipeline(&stream.graph, input, 5u, &report));
        check_null(report.error);
        check_equal(report.output_count, (size_t)2u);
        check_false(report.compiled_plan_checked);

        cflow_plan_destroy(&plan);
        cflow_graph_destroy(&normalized);
        cflow_stream_destroy(&stream);
    }

    it("includes immutable bounds in structural equality") {
        cflow_stream stream = {0};
        cflow_graph clone = {0};
        cflow_subgraph *root;

        clone.root = CMETA_INVALID_ID;
        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.take(&stream, 2u));
        check_true(cflow_graph_clone(&clone, &stream.graph));
        check_true(cflow_graph_structural_equal(&stream.graph, &clone));
        root = &clone.subgraphs[clone.root];
        root->nodes[root->tail].slice.count = 3u;
        check_false(cflow_graph_structural_equal(&stream.graph, &clone));

        cflow_graph_destroy(&clone);
        cflow_stream_destroy(&stream);
    }

    it("rejects inconsistent slice metadata") {
        cflow_stream stream = {0};
        cflow_graph plain = {0};
        cflow_subgraph *root;
        const char *error = NULL;

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.take(&stream, 2u));
        root = &stream.graph.subgraphs[stream.graph.root];
        root->nodes[root->tail].slice.present = false;
        check_false(cflow_graph_validate(&stream.graph, &error));
        check_not_null(error);
        cflow_stream_destroy(&stream);

        cflow_graph_init(&plain, &cmeta_type_int);
        plain.subgraphs[plain.root].nodes[0].slice.count = 1u;
        error = NULL;
        check_false(cflow_graph_validate(&plain, &error));
        check_not_null(error);
        cflow_graph_destroy(&plain);
    }

    it("creates detached slice nodes for advanced Graph wiring") {
        cflow_graph graph = {0};
        cflow_subgraph_id subgraph;
        cflow_node_id source;
        cflow_node_id slice = CMETA_INVALID_ID;
        const char *error = NULL;

        cflow_graph_init(&graph, &cmeta_type_int);
        subgraph = graph.root;
        source = graph.subgraphs[subgraph].tail;
        check_true(cflow_graph_create_slice_node(
            &graph, subgraph, CFLOW_OP_SKIP, &cmeta_type_int, 2u, &slice));
        check_true(cflow_graph_connect(
            &graph, subgraph, source, 0u, slice, 0u));
        check_true(cflow_graph_set_subgraph_exit(&graph, subgraph, slice));
        check_true(cflow_graph_validate(&graph, &error));
        check_null(error);
        check_true(graph.subgraphs[subgraph].nodes[slice].slice.present);
        check_equal(graph.subgraphs[subgraph].nodes[slice].slice.count,
                    (size_t)2u);
        cflow_graph_destroy(&graph);
    }

    it("completes take zero without resuming its source") {
        slice_source_probe source = {0};
        slice_run_fixture fixture;

        check_true(slice_run_fixture_init(&fixture, &source, 0u));
        check_true(cflow_run_request(&fixture.run, SIZE_MAX));
        (void)cflow_scheduler_run_until_idle(&fixture.scheduler, 0u);

        check_equal(source.resumes, (size_t)0u);
        check_equal(source.cancels, (size_t)1u);
        check_equal(fixture.sink_probe.value_count, (size_t)0u);
        check_equal(fixture.sink_probe.done_count, (size_t)1u);
        check_null(fixture.sink_probe.error);
        check_true(cflow_run_is_done(&fixture.run));
        check_false(cflow_run_is_cancelled(&fixture.run));
        slice_run_fixture_destroy(&fixture);
    }

    it("stops an unbounded source exactly at the take limit") {
        const int expected[] = {0, 1};
        slice_source_probe source = {0};
        slice_run_fixture fixture;

        check_true(slice_run_fixture_init(&fixture, &source, 2u));
        check_true(cflow_run_request(&fixture.run, SIZE_MAX));
        (void)cflow_scheduler_run_until_idle(&fixture.scheduler, 0u);

        check_equal(source.resumes, (size_t)2u);
        check_equal(source.cancels, (size_t)1u);
        check_equal(fixture.sink_probe.value_count, (size_t)2u);
        check_equal(fixture.sink_probe.values, expected, sizeof(expected));
        check_equal(fixture.sink_probe.done_count, (size_t)1u);
        check_null(fixture.sink_probe.error);
        check_true(cflow_run_is_done(&fixture.run));
        check_false(cflow_run_is_cancelled(&fixture.run));
        slice_run_fixture_destroy(&fixture);
    }

    it("preserves a source error reached before the take limit") {
        slice_source_probe source = {.fail_on_resume = 2u};
        slice_run_fixture fixture;

        check_true(slice_run_fixture_init(&fixture, &source, 3u));
        check_true(cflow_run_request(&fixture.run, SIZE_MAX));
        (void)cflow_scheduler_run_until_idle(&fixture.scheduler, 0u);

        check_equal(source.resumes, (size_t)2u);
        check_equal(source.cancels, (size_t)0u);
        check_equal(fixture.sink_probe.value_count, (size_t)1u);
        check_equal(fixture.sink_probe.done_count, (size_t)0u);
        check_equal(fixture.sink_probe.error, "slice probe failure");
        check_equal(cflow_run_error(&fixture.run), "slice probe failure");
        check_false(cflow_run_is_done(&fixture.run));
        check_false(cflow_run_is_cancelled(&fixture.run));
        slice_run_fixture_destroy(&fixture);
    }
}
