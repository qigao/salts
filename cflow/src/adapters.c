#include <cflow/lower.h>
#include <cflow/sources.h>

#include "adapters_internal.h"
#include "sources_internal.h"
#include "value_storage.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct eval_state {
    bool done;
    const char *error;
    const cmeta_type_desc *output_type;
} eval_state;

typedef struct result_collector {
    eval_state eval;
    unsigned char *data;
    size_t count;
    size_t capacity;
    size_t limit;
    const cmeta_type_desc *type;
} result_collector;

static bool collect_value(void *user, const cmeta_type_desc *type, const void *value) {
    result_collector *c = (result_collector *)user;
    if (!c->type) c->type = type;
    if (!cmeta_type_equal(c->type, type)) return false;
    if (c->count >= c->limit) return false;
    if (c->count == c->capacity) {
        size_t next = c->capacity ? c->capacity * 2 : 8;
        if (next < c->capacity) return false;
        if (next > c->limit) next = c->limit;
        if (next <= c->capacity || type->size > SIZE_MAX / next) return false;
        unsigned char *p = realloc(c->data, next * type->size);
        if (!p) return false;
        c->data = p; c->capacity = next;
    }
    memcpy(c->data + c->count * type->size, value, type->size);
    ++c->count;
    return true;
}
static void collect_error(void *user, const char *message) {
    result_collector *c = (result_collector *)user; c->eval.error = message;
}
static void collect_done(void *user) {
    result_collector *c = (result_collector *)user; c->eval.done = true;
}

static bool cflow_eval_source(const cflow_graph *graph,
                              cflow_source *source,
                              const cflow_sink *sink,
                              eval_state *state,
                              size_t initial_demand) {
    if (!source) return false;
    if (!graph || !sink || !state || initial_demand == 0u) {
        cflow_source_destroy(source);
        return false;
    }

    cflow_graph normalized = {0};
    normalized.root = CMETA_INVALID_ID;
    const cflow_graph *exec_graph = graph;
    if (!cflow_graph_is_normalized(graph)) {
        if (!cflow_graph_normalize(&normalized, graph)) {
            state->error = normalized.error
                ? normalized.error : "graph normalization failed";
            cflow_graph_destroy(&normalized);
            cflow_source_destroy(source);
            return false;
        }
        exec_graph = &normalized;
    }

    cflow_scheduler scheduler = {0};
    cflow_run run = {0};
    if (!cflow_scheduler_test_init(&scheduler)) {
        cflow_graph_destroy(&normalized);
        cflow_source_destroy(source);
        return false;
    }
    if (!cflow_run_open(&run, exec_graph, source, &scheduler, sink)) {
        cflow_source_destroy(source);
        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        return false;
    }
    if (!cflow_run_request(&run, initial_demand)) {
        cflow_run_close(&run);
        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        return false;
    }
    (void)cflow_scheduler_run_until_idle(&scheduler, 0);
    if (!state->done && !state->error &&
        cflow_run_outstanding_demand(&run) == 0u) {
        if (cflow_run_request(&run, 1u))
            (void)cflow_scheduler_run_until_idle(&scheduler, 0);
    }
    state->output_type = cflow_graph_output_type(exec_graph);
    if (!state->error) state->error = cflow_run_error(&run);
    bool ok = state->done && !state->error;
    cflow_run_close(&run);
    cflow_scheduler_destroy(&scheduler);
    cflow_graph_destroy(&normalized);
    return ok;
}

static bool cflow_eval_result_source(const cflow_graph *graph,
                                     cflow_source *source,
                                     size_t max_items,
                                     cflow_result *out) {
    result_collector c = {{false, NULL, NULL}, NULL, 0u, 0u, max_items, NULL};
    cflow_sink_callbacks sink_cb = { collect_value, collect_error, collect_done, &c };
    cflow_sink sink = cflow_sink_from_callbacks(&sink_cb);
    bool ok;

    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!graph || !source || !cflow_source_valid(source) ||
        !cflow_value_storage_type_supported(
            cflow_source_output_type(source)) ||
        !cflow_value_storage_graph_supported(graph)) {
        if (source)
            cflow_source_destroy(source);
        return false;
    }
    ok = cflow_eval_source(graph, source, &sink, &c.eval, SIZE_MAX);
    if (ok) {
        out->data = c.data;
        out->count = c.count;
        out->type = c.type ? c.type : c.eval.output_type;
        c.data = NULL;
    }
    free(c.data);
    return ok;
}

bool cflow_eval_array(const cflow_graph *graph,
                      const void *inputs,
                      size_t input_count,
                      cflow_result *out) {
    cflow_source source = {0};
    if (!graph || !out) return false;
    if (!cflow_source_from_array(&source, cflow_graph_source_type(graph), inputs, input_count)) {
        return false;
    }
    return cflow_eval_result_source(graph, &source, SIZE_MAX, out);
}

bool cflow_eval_stream(const cflow_stream *stream, cflow_result *out) {
    cflow_source source = {0};
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!stream || !stream->has_source_range) return false;
    if (!cflow_source_from_range(&source, stream->source_range)) return false;
    return cflow_eval_result_source(&stream->graph, &source, SIZE_MAX, out);
}

bool cflow_eval_stream_limit(const cflow_stream *stream,
                             size_t max_items,
                             cflow_result *out) {
    cflow_source source = {0};
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!stream || !stream->has_source_range) return false;
    if (!cflow_source_from_range(&source, stream->source_range)) return false;
    return cflow_eval_result_source(&stream->graph, &source, max_items, out);
}

typedef struct cmeta_collector_sink {
    eval_state eval;
    cmeta_collector *collector;
} cmeta_collector_sink;

static bool cmeta_collect_value(void *user,
                                const cmeta_type_desc *type,
                                const void *value) {
    cmeta_collector_sink *sink = (cmeta_collector_sink *)user;
    return sink && cmeta_collector_accept(sink->collector, type, value) == CMETA_OK;
}

static void cmeta_collect_error(void *user, const char *message) {
    cmeta_collector_sink *sink = (cmeta_collector_sink *)user;
    if (!sink) return;
    sink->eval.error = message ? message : "stream evaluation failed";
    if (sink->collector && sink->collector->status == CMETA_OK)
        cmeta_collector_fail(sink->collector, CMETA_CALLBACK_ERROR);
}

static void cmeta_collect_done(void *user) {
    cmeta_collector_sink *sink = (cmeta_collector_sink *)user;
    if (!sink) return;
    if (cmeta_collector_finish(sink->collector) != CMETA_OK) {
        sink->eval.error = "collector finish failed";
        return;
    }
    sink->eval.done = true;
}

bool cflow_eval_collect(const cflow_stream *stream,
                        cmeta_collector *collector,
                        const char **out_error) {
    cmeta_collector_sink state = {{false, NULL, NULL}, collector};
    cflow_sink_callbacks sink_cb = {
        cmeta_collect_value, cmeta_collect_error, cmeta_collect_done, &state
    };
    cflow_sink sink = cflow_sink_from_callbacks(&sink_cb);
    cflow_source source = {0};
    const char *source_error = NULL;
    cmeta_status source_status;
    bool ok;

    if (out_error) *out_error = NULL;
    if (!stream || !stream->has_source_range || !collector) {
        if (out_error) *out_error = "invalid stream collection arguments";
        return false;
    }
    if (collector->state != CMETA_COLLECTOR_ZERO) {
        if (out_error) *out_error = "collector begin failed";
        return false;
    }
    source_status = cflow_source_from_range_checked(
        &source, stream->source_range, &source_error);
    if (source_status != CMETA_OK) {
        cmeta_collector_terminate_pre_begin(collector, source_status);
        if (out_error) *out_error = source_error;
        return false;
    }
    if (cmeta_collector_begin(collector) != CMETA_OK) {
        cflow_source_destroy(&source);
        if (out_error) *out_error = "collector begin failed";
        return false;
    }
    ok = cflow_eval_source(
        &stream->graph, &source, &sink, &state.eval, SIZE_MAX);
    if (!ok && !state.eval.error)
        state.eval.error = "stream evaluation failed";
    if (!ok && (collector->state == CMETA_COLLECTOR_BEGUN ||
                collector->state == CMETA_COLLECTOR_ACCEPTING)) {
        cmeta_collector_fail(collector, CMETA_CALLBACK_ERROR);
    }
    if (out_error) *out_error = state.eval.error;
    return ok && collector->state == CMETA_COLLECTOR_COMMITTED;
}

typedef struct count_sink {
    eval_state eval;
    size_t count;
    size_t limit;
} count_sink;

static bool count_value(void *user,
                        const cmeta_type_desc *type,
                        const void *value) {
    count_sink *sink = (count_sink *)user;

    (void)type;
    (void)value;
    if (!sink) return false;
    if (sink->count == sink->limit) {
        sink->eval.error = "stream count overflow";
        return false;
    }
    ++sink->count;
    return true;
}

static void count_error(void *user, const char *message) {
    count_sink *sink = (count_sink *)user;

    if (sink && !sink->eval.error)
        sink->eval.error = message ? message : "stream evaluation failed";
}

static void count_done(void *user) {
    count_sink *sink = (count_sink *)user;

    if (sink) sink->eval.done = true;
}

bool cflow_eval_count_bounded(const cflow_stream *stream,
                              size_t max_count,
                              size_t *out_count,
                              const char **out_error) {
    count_sink state = {{false, NULL, NULL}, 0u, max_count};
    cflow_sink_callbacks sink_cb = {
        count_value, count_error, count_done, &state
    };
    cflow_sink sink = cflow_sink_from_callbacks(&sink_cb);
    cflow_source source = {0};
    const char *source_error = NULL;
    cmeta_status source_status;
    bool ok;

    if (out_error) *out_error = NULL;
    if (!out_count) {
        if (out_error) *out_error = "invalid stream count arguments";
        return false;
    }
    *out_count = 0u;
    if (!stream || !stream->has_source_range) {
        if (out_error) *out_error = "invalid stream count arguments";
        return false;
    }
    if (!cflow_stream_ok(stream)) {
        if (out_error) {
            const char *stream_error = cflow_stream_error(stream);
            *out_error = stream_error
                ? stream_error : "stream graph is invalid";
        }
        return false;
    }
    source_status = cflow_source_from_range_checked(
        &source, stream->source_range, &source_error);
    if (source_status != CMETA_OK) {
        if (out_error) *out_error = source_error;
        return false;
    }
    ok = cflow_eval_source(
        &stream->graph, &source, &sink, &state.eval,
        max_count ? max_count : 1u);
    if (!ok && !state.eval.error)
        state.eval.error = "stream evaluation failed";
    if (ok) *out_count = state.count;
    if (out_error) *out_error = state.eval.error;
    return ok;
}

bool cflow_eval_count(const cflow_stream *stream,
                      size_t *out_count,
                      const char **out_error) {
    return cflow_eval_count_bounded(
        stream, SIZE_MAX, out_count, out_error);
}

void cflow_result_destroy(cflow_result *result) {
    if (!result) return;
    free(result->data); memset(result, 0, sizeof(*result));
}
