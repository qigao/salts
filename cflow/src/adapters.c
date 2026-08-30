#include <cflow/adapters.h>
#include <cflow/lower.h>
#include <cflow/publishers.h>

#include "adapters_internal.h"
#include "result_storage.h"
#include "publishers_internal.h"
#include "value_storage.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct eval_state {
    bool done;
    const char *error;
    const cmeta_type_desc *output_type;
    cflow_status status;
} eval_state;

typedef struct eval_control {
    cflow_subscription *run;
    bool stop_requested;
} eval_control;

typedef struct result_collector {
    eval_state eval;
    cflow_status status;
    unsigned char *data;
    size_t count;
    size_t capacity;
    size_t limit;
    const cmeta_type_desc *type;
} result_collector;

static cflow_status stream_status_from_cmeta(cmeta_status status);

static cflow_status_result stream_status_result(cflow_status status) {
    cflow_status_result result = {status};
    return result;
}

static bool collect_value(void *user, const cmeta_type_desc *type, const void *value) {
    result_collector *c = (result_collector *)user;
    if (!c || !type || !value) {
        if (c) c->status = CFLOW_STATUS_INVALID_ARGUMENT;
        return false;
    }
    if (!c->type) c->type = type;
    if (!cmeta_type_equal(c->type, type)) {
        c->status = CFLOW_STATUS_TYPE_MISMATCH;
        return false;
    }
    if (c->count >= c->limit) {
        c->status = CFLOW_STATUS_CAPACITY_EXCEEDED;
        return false;
    }
    if (c->count == c->capacity) {
        size_t next = c->capacity ? c->capacity * 2 : 8;
        if (next < c->capacity) {
            c->status = CFLOW_STATUS_CAPACITY_EXCEEDED;
            return false;
        }
        if (next > c->limit) next = c->limit;
        if (next <= c->capacity || type->size > SIZE_MAX / next) {
            c->status = CFLOW_STATUS_CAPACITY_EXCEEDED;
            return false;
        }
        unsigned char *p = realloc(c->data, next * type->size);
        if (!p) {
            c->status = CFLOW_STATUS_ALLOCATION_FAILED;
            return false;
        }
        c->data = p; c->capacity = next;
    }
    memcpy(c->data + c->count * type->size, value, type->size);
    ++c->count;
    return true;
}
static void collect_error(void *user, const char *message) {
    result_collector *c = (result_collector *)user;
    if (c && !c->eval.error) c->eval.error = message;
}
static void collect_done(void *user) {
    result_collector *c = (result_collector *)user; c->eval.done = true;
}

static void adapter_destroy_owned_source(cflow_publisher *source) {
    if (!source || !cflow_publisher_valid(source)) return;
    cflow_publisher_destroy(source);
    *source = (cflow_publisher){0};
}

bool cflow_adapter_prepare_owned_source_graph(
    cflow_graph *normalized,
    const cflow_graph *graph,
    cflow_publisher *source,
    const cflow_graph **out_graph) {
    if (out_graph) *out_graph = NULL;
    if (!normalized || !graph || !source || !cflow_publisher_valid(source) ||
        !out_graph || normalized->subgraphs || normalized->subgraph_count) {
        adapter_destroy_owned_source(source);
        return false;
    }
    if (cflow_graph_is_normalized(graph)) {
        *out_graph = graph;
        return true;
    }
    if (!cflow_graph_normalize(normalized, graph)) {
        adapter_destroy_owned_source(source);
        return false;
    }
    *out_graph = normalized;
    return true;
}

static bool cflow_eval_source(const cflow_graph *graph,
                              cflow_publisher *source,
                              const cflow_subscriber *sink,
                              eval_state *state,
                              eval_control *control,
                              const cflow_eval_options *options) {
    if (!graph || !source || !sink || !state) return false;

    cflow_graph normalized = {0};
    normalized.root = CMETA_INVALID_ID;
    const cflow_graph *exec_graph = NULL;
    if (!cflow_adapter_prepare_owned_source_graph(
            &normalized, graph, source, &exec_graph))
        return false;

    cflow_scheduler scheduler = {0};
    cflow_subscription run = {0};
    if (!cflow_scheduler_test_init(&scheduler)) {
        cflow_graph_destroy(&normalized);
        cflow_publisher_destroy(source);
        return false;
    }
    cflow_status_result open_result = cflow_subscribe_with_options(
        &run, exec_graph, source, &scheduler, sink, options);
    if (!cflow_status_result_is_ok(open_result)) {
        state->status = open_result.status;
        state->error = cflow_status_string(open_result.status);
        cflow_publisher_destroy(source);
        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        return false;
    }
    if (control) control->run = &run;
    if (!cflow_subscription_request(&run, SIZE_MAX)) {
        if (control) control->run = NULL;
        cflow_subscription_close(&run);
        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        return false;
    }
    (void)cflow_scheduler_run_until_idle(&scheduler, 0);
    state->output_type = cflow_graph_output_type(exec_graph);
    if (!state->error) state->error = cflow_subscription_error(&run);
    state->status = cflow_subscription_status(&run);
    bool ok = !state->error &&
        (state->done ||
         (control && control->stop_requested &&
          cflow_subscription_is_cancelled(&run)));
    if (control) control->run = NULL;
    cflow_subscription_close(&run);
    cflow_scheduler_destroy(&scheduler);
    cflow_graph_destroy(&normalized);
    return ok;
}

static cflow_status cflow_eval_result_source(
    const cflow_graph *graph,
    cflow_publisher *source,
    size_t max_items,
    const cflow_eval_options *options,
    cflow_result *out) {
    result_collector c = {
        {false, NULL, NULL, CFLOW_STATUS_OK},
        CFLOW_STATUS_OK, NULL, 0u, 0u, max_items, NULL};
    cflow_subscriber_callbacks sink_cb = { collect_value, collect_error, collect_done, &c };
    cflow_subscriber sink = cflow_subscriber_from_callbacks(&sink_cb);
    bool ok;

    cflow_status status;

    if (!out) {
        if (source) cflow_publisher_destroy(source);
        return CFLOW_STATUS_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (!graph || !source || !cflow_publisher_valid(source)) {
        if (source)
            cflow_publisher_destroy(source);
        return CFLOW_STATUS_INVALID_ARGUMENT;
    }
    if (!cflow_value_storage_type_supported(
            cflow_publisher_output_type(source)) ||
        !cflow_value_storage_graph_supported(graph)) {
        cflow_publisher_destroy(source);
        return CFLOW_STATUS_UNSUPPORTED;
    }
    ok = cflow_eval_source(graph, source, &sink, &c.eval, NULL, options);
    if (ok) {
        out->data = c.data;
        out->count = c.count;
        out->type = c.type ? c.type : c.eval.output_type;
        c.data = NULL;
    }
    free(c.data);
    if (ok) return CFLOW_STATUS_OK;
    if (c.status != CFLOW_STATUS_OK) return c.status;
    status = c.eval.status;
    return status == CFLOW_STATUS_OK
        ? CFLOW_STATUS_EXECUTION_ERROR : status;
}

cflow_status_result cflow_eval_array_result(
    const cflow_graph *graph,
    const void *inputs,
    size_t input_count,
    cflow_result *out) {
    cflow_publisher source = {0};
    cmeta_status source_status;
    cflow_status status;

    if (out) memset(out, 0, sizeof(*out));
    if (!graph || !out)
        return stream_status_result(CFLOW_STATUS_INVALID_ARGUMENT);
    source_status = cflow_publisher_from_array_checked(
        &source, cflow_graph_input_type(graph), inputs, input_count);
    if (source_status != CMETA_OK)
        return stream_status_result(stream_status_from_cmeta(source_status));
    status = cflow_eval_result_source(
        graph, &source, SIZE_MAX, NULL, out);
    return stream_status_result(status);
}

static cflow_status_result cflow_eval_bound_stream_result(
    const cflow_stream *stream,
    size_t max_items,
    cflow_result *out) {
    cflow_publisher source = {0};
    cmeta_status source_status;
    cflow_status status;

    if (out) memset(out, 0, sizeof(*out));
    if (!stream || !stream->has_input_range || !out)
        return stream_status_result(CFLOW_STATUS_INVALID_ARGUMENT);
    source_status = cflow_publisher_from_range_checked(
        &source, stream->input_range, NULL);
    if (source_status != CMETA_OK)
        return stream_status_result(stream_status_from_cmeta(source_status));
    status = cflow_eval_result_source(
        &stream->graph, &source, max_items, &stream->eval_options, out);
    return stream_status_result(status);
}

cflow_status_result cflow_eval_stream_result(
    const cflow_stream *stream, cflow_result *out) {
    return cflow_eval_bound_stream_result(stream, SIZE_MAX, out);
}

cflow_status_result cflow_eval_stream_limit_result(
    const cflow_stream *stream,
    size_t max_items,
    cflow_result *out) {
    return cflow_eval_bound_stream_result(stream, max_items, out);
}

bool cflow_eval_array(const cflow_graph *graph,
                      const void *inputs,
                      size_t input_count,
                      cflow_result *out) {
    return cflow_status_result_is_ok(
        cflow_eval_array_result(graph, inputs, input_count, out));
}

bool cflow_eval_stream(const cflow_stream *stream, cflow_result *out) {
    return cflow_status_result_is_ok(
        cflow_eval_stream_result(stream, out));
}

bool cflow_eval_stream_limit(const cflow_stream *stream,
                             size_t max_items,
                             cflow_result *out) {
    return cflow_status_result_is_ok(
        cflow_eval_stream_limit_result(stream, max_items, out));
}

typedef struct cmeta_collector_sink {
    eval_state eval;
    cmeta_collector *collector;
} cmeta_collector_sink;

static cflow_collect_result collect_result_snapshot(
    cflow_status status, const cmeta_collector *collector) {
    cflow_collect_result result = {
        status, CMETA_INVALID_ARGUMENT, CMETA_COLLECTOR_ZERO, 0u};

    if (collector) {
        result.collector_status = collector->status;
        result.collector_state = collector->state;
        result.count = collector->count;
    }
    return result;
}

bool cflow_collect_result_is_ok(cflow_collect_result result) {
    return result.status == CFLOW_STATUS_OK &&
           result.collector_status == CMETA_OK &&
           result.collector_state == CMETA_COLLECTOR_COMMITTED;
}

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

cflow_collect_result cflow_eval_collect_result(
    const cflow_stream *stream,
    cmeta_collector *collector,
    const char **out_error) {
    cmeta_collector_sink state = {
        {false, NULL, NULL, CFLOW_STATUS_OK}, collector};
    cflow_subscriber_callbacks sink_cb = {
        cmeta_collect_value, cmeta_collect_error, cmeta_collect_done, &state
    };
    cflow_subscriber sink = cflow_subscriber_from_callbacks(&sink_cb);
    cflow_publisher source = {0};
    const char *source_error = NULL;
    cmeta_status source_status;
    bool ok;
    cflow_status status;

    if (out_error) *out_error = NULL;
    if (!stream || !stream->has_input_range || !collector) {
        if (out_error) *out_error = "invalid stream collection arguments";
        return collect_result_snapshot(CFLOW_STATUS_INVALID_ARGUMENT,
                                       collector);
    }
    if (collector->state != CMETA_COLLECTOR_ZERO) {
        if (out_error) *out_error = "collector begin failed";
        return collect_result_snapshot(CFLOW_STATUS_INVALID_ARGUMENT,
                                       collector);
    }
    source_status = cflow_publisher_from_range_checked(
        &source, stream->input_range, &source_error);
    if (source_status != CMETA_OK) {
        cmeta_collector_terminate_pre_begin(collector, source_status);
        if (out_error) *out_error = source_error;
        return collect_result_snapshot(
            stream_status_from_cmeta(source_status), collector);
    }
    if (cmeta_collector_begin(collector) != CMETA_OK) {
        cflow_publisher_destroy(&source);
        if (out_error) *out_error = "collector begin failed";
        return collect_result_snapshot(
            stream_status_from_cmeta(collector->status), collector);
    }
    ok = cflow_eval_source(
        &stream->graph, &source, &sink, &state.eval, NULL,
        &stream->eval_options);
    if (!ok && !state.eval.error)
        state.eval.error = "stream evaluation failed";
    if (!ok && (collector->state == CMETA_COLLECTOR_BEGUN ||
                collector->state == CMETA_COLLECTOR_ACCEPTING)) {
        cmeta_collector_fail(collector, CMETA_CALLBACK_ERROR);
    }
    if (out_error) *out_error = state.eval.error;

    if (collector->status != CMETA_OK &&
        (state.eval.status == CFLOW_STATUS_OK ||
         collector->status != CMETA_CALLBACK_ERROR)) {
        status = stream_status_from_cmeta(collector->status);
    } else if (state.eval.status != CFLOW_STATUS_OK) {
        status = state.eval.status;
    } else if (ok && collector->state == CMETA_COLLECTOR_COMMITTED) {
        status = CFLOW_STATUS_OK;
    } else {
        status = CFLOW_STATUS_EXECUTION_ERROR;
    }
    return collect_result_snapshot(status, collector);
}

bool cflow_eval_collect(const cflow_stream *stream,
                        cmeta_collector *collector,
                        const char **out_error) {
    return cflow_collect_result_is_ok(
        cflow_eval_collect_result(stream, collector, out_error));
}

typedef enum stream_terminal_kind {
    STREAM_TERMINAL_COUNT,
    STREAM_TERMINAL_ANY_MATCH,
    STREAM_TERMINAL_ALL_MATCH,
    STREAM_TERMINAL_FIND_FIRST,
    STREAM_TERMINAL_FOR_EACH
} stream_terminal_kind;

typedef struct cflow_find_result_impl {
    cflow_value_slot value;
} cflow_find_result_impl;

typedef struct stream_terminal_state {
    eval_state eval;
    eval_control control;
    cflow_status status;
    stream_terminal_kind kind;
    size_t count;
    bool matched;
    cmeta_callable predicate;
    cflow_value_fn action;
    void *action_user;
    cflow_find_result pending_find;
} stream_terminal_state;

static void stream_terminal_stop(stream_terminal_state *state) {
    if (!state || !state->control.run) return;
    state->control.stop_requested = true;
    cflow_subscription_cancel(state->control.run);
}

static cflow_status stream_terminal_retain_first(
    stream_terminal_state *state,
    const cmeta_type_desc *type,
    const void *value) {
    cflow_find_result_impl *result;

    if (!state || state->pending_find.impl || !value)
        return CFLOW_STATUS_INVALID_ARGUMENT;
    result = (cflow_find_result_impl *)calloc(1u, sizeof(*result));
    if (!result) return CFLOW_STATUS_ALLOCATION_FAILED;
    if (!cflow_value_slot_init(&result->value, type)) {
        free(result);
        return CFLOW_STATUS_ALLOCATION_FAILED;
    }
    if (!cflow_value_slot_copy(&result->value, value)) {
        cflow_value_slot_destroy(&result->value);
        free(result);
        return CFLOW_STATUS_EXECUTION_ERROR;
    }
    state->pending_find.impl = result;
    return CFLOW_STATUS_OK;
}

static bool stream_terminal_value(void *user,
                                  const cmeta_type_desc *type,
                                  const void *value) {
    stream_terminal_state *state = (stream_terminal_state *)user;
    const void *args[1] = {value};
    bool predicate_result = false;

    if (!state || !type || !value) return false;
    switch (state->kind) {
        case STREAM_TERMINAL_COUNT:
            if (state->count == SIZE_MAX) {
                state->status = CFLOW_STATUS_CAPACITY_EXCEEDED;
                state->eval.error = "stream count overflow";
                return false;
            }
            ++state->count;
            return true;
        case STREAM_TERMINAL_ANY_MATCH:
        case STREAM_TERMINAL_ALL_MATCH:
            if (!cmeta_callable_invoke(
                    &state->predicate, &predicate_result, args)) {
                state->status = CFLOW_STATUS_EXECUTION_ERROR;
                state->eval.error = "stream predicate invocation failed";
                return false;
            }
            if ((state->kind == STREAM_TERMINAL_ANY_MATCH &&
                 predicate_result) ||
                (state->kind == STREAM_TERMINAL_ALL_MATCH &&
                 !predicate_result)) {
                state->matched = predicate_result;
                stream_terminal_stop(state);
            }
            return true;
        case STREAM_TERMINAL_FIND_FIRST:
            state->status = stream_terminal_retain_first(state, type, value);
            if (state->status != CFLOW_STATUS_OK) {
                state->eval.error = "stream could not retain first value";
                return false;
            }
            stream_terminal_stop(state);
            return true;
        case STREAM_TERMINAL_FOR_EACH:
            if (!state->action(state->action_user, type, value)) {
                state->status = CFLOW_STATUS_EXECUTION_ERROR;
                state->eval.error = "stream for_each callback failed";
                return false;
            }
            return true;
    }
    state->status = CFLOW_STATUS_EXECUTION_ERROR;
    state->eval.error = "stream terminal kind is invalid";
    return false;
}

static void stream_terminal_error(void *user, const char *message) {
    stream_terminal_state *state = (stream_terminal_state *)user;
    if (state && !state->eval.error) {
        state->eval.error = message ? message : "stream evaluation failed";
    }
}

static void stream_terminal_done(void *user) {
    stream_terminal_state *state = (stream_terminal_state *)user;
    if (state) state->eval.done = true;
}

static cflow_status stream_status_from_cmeta(cmeta_status status) {
    switch (status) {
        case CMETA_OK: return CFLOW_STATUS_OK;
        case CMETA_INVALID_ARGUMENT: return CFLOW_STATUS_INVALID_ARGUMENT;
        case CMETA_TYPE_MISMATCH: return CFLOW_STATUS_TYPE_MISMATCH;
        case CMETA_TRAIT_MISSING: return CFLOW_STATUS_UNSUPPORTED;
        case CMETA_CAPACITY_EXCEEDED: return CFLOW_STATUS_CAPACITY_EXCEEDED;
        case CMETA_OUT_OF_MEMORY: return CFLOW_STATUS_ALLOCATION_FAILED;
        case CMETA_CALLBACK_ERROR: return CFLOW_STATUS_EXECUTION_ERROR;
    }
    return CFLOW_STATUS_EXECUTION_ERROR;
}

static cflow_status stream_terminal_eval(const cflow_stream *stream,
                                         stream_terminal_state *state,
                                         const char **out_error) {
    cflow_subscriber_callbacks callbacks = {
        stream_terminal_value,
        stream_terminal_error,
        stream_terminal_done,
        state
    };
    cflow_subscriber sink = cflow_subscriber_from_callbacks(&callbacks);
    cflow_publisher source = {0};
    const char *source_error = NULL;
    cmeta_status source_status;
    bool ok;

    if (out_error) *out_error = NULL;
    if (!stream || !state || !stream->has_input_range ||
        !cflow_stream_ok(stream)) {
        if (out_error) *out_error = "invalid bound stream";
        return CFLOW_STATUS_INVALID_ARGUMENT;
    }
    source_status = cflow_publisher_from_range_checked(
        &source, stream->input_range, &source_error);
    if (source_status != CMETA_OK) {
        if (out_error) *out_error = source_error;
        return stream_status_from_cmeta(source_status);
    }
    ok = cflow_eval_source(
        &stream->graph, &source, &sink, &state->eval, &state->control,
        &stream->eval_options);
    if (!ok && !state->eval.error)
        state->eval.error = "stream evaluation failed";
    if (out_error) *out_error = state->eval.error;
    if (ok) return CFLOW_STATUS_OK;
    if (state->status != CFLOW_STATUS_OK) return state->status;
    return state->eval.status == CFLOW_STATUS_OK
        ? CFLOW_STATUS_EXECUTION_ERROR : state->eval.status;
}

static cflow_status stream_terminal_bind_predicate(
    const cflow_stream *stream,
    cflow_filter_callable predicate,
    cmeta_callable *out,
    const char **out_error) {
    const cmeta_sig_desc *signature;
    const cmeta_type_desc *output_type;

    if (!stream || !out || !cflow_stream_ok(stream)) {
        if (out_error) *out_error = "invalid bound stream";
        return CFLOW_STATUS_INVALID_ARGUMENT;
    }
    output_type = cflow_stream_output_type(stream);
    if (!cmeta_callable_bind(predicate.fn, out)) {
        if (out_error) *out_error = "invalid stream predicate";
        return CFLOW_STATUS_INVALID_ARGUMENT;
    }
    signature = cmeta_callable_signature(*out);
    if (!signature || signature->protocol != CMETA_FN_PROTOCOL_VALUE ||
        signature->param_count != 1u ||
        !cmeta_type_equal(signature->params[0], output_type) ||
        !cmeta_type_equal(signature->return_type, &cmeta_type_bool)) {
        if (out_error) *out_error = "stream predicate type mismatch";
        return CFLOW_STATUS_TYPE_MISMATCH;
    }
    return CFLOW_STATUS_OK;
}

static cflow_status cflow_stream_count_impl(const cflow_stream *stream,
                                            size_t *out_count,
                                            const char **out_error) {
    stream_terminal_state state = {0};
    cflow_status status;

    if (out_error) *out_error = NULL;
    if (!out_count) {
        if (out_error) *out_error = "count output is null";
        return CFLOW_STATUS_INVALID_ARGUMENT;
    }
    *out_count = 0u;
    state.kind = STREAM_TERMINAL_COUNT;
    status = stream_terminal_eval(stream, &state, out_error);
    if (status == CFLOW_STATUS_OK) *out_count = state.count;
    return status;
}

static cflow_status cflow_stream_match_impl(
    const cflow_stream *stream,
    cflow_filter_callable predicate,
    stream_terminal_kind kind,
    bool *out_matches,
    const char **out_error) {
    stream_terminal_state state = {0};
    cflow_status status;

    if (out_error) *out_error = NULL;
    if (!out_matches) {
        if (out_error) *out_error = "match output is null";
        return CFLOW_STATUS_INVALID_ARGUMENT;
    }
    *out_matches = false;
    status = stream_terminal_bind_predicate(
        stream, predicate, &state.predicate, out_error);
    if (status != CFLOW_STATUS_OK) return status;
    state.kind = kind;
    state.matched = kind == STREAM_TERMINAL_ALL_MATCH;
    status = stream_terminal_eval(stream, &state, out_error);
    if (status == CFLOW_STATUS_OK) *out_matches = state.matched;
    return status;
}

bool cflow_stream_any_match(const cflow_stream *stream,
                            cflow_filter_callable predicate,
                            bool *out_matches,
                            const char **out_error) {
    return cflow_stream_match_impl(
        stream, predicate, STREAM_TERMINAL_ANY_MATCH,
        out_matches, out_error) == CFLOW_STATUS_OK;
}

bool cflow_stream_all_match(const cflow_stream *stream,
                            cflow_filter_callable predicate,
                            bool *out_matches,
                            const char **out_error) {
    return cflow_stream_match_impl(
        stream, predicate, STREAM_TERMINAL_ALL_MATCH,
        out_matches, out_error) == CFLOW_STATUS_OK;
}

static cflow_status cflow_stream_find_first_impl(
    const cflow_stream *stream,
    cflow_find_result *out,
    const char **out_error) {
    stream_terminal_state state = {0};
    cflow_status status;

    if (out_error) *out_error = NULL;
    if (!out) {
        if (out_error) *out_error = "find result is null";
        return CFLOW_STATUS_INVALID_ARGUMENT;
    }
    if (out->impl) {
        if (out_error) *out_error = "find result is not empty";
        return CFLOW_STATUS_INVALID_ARGUMENT;
    }
    state.kind = STREAM_TERMINAL_FIND_FIRST;
    status = stream_terminal_eval(stream, &state, out_error);
    if (status == CFLOW_STATUS_OK) {
        *out = state.pending_find;
        state.pending_find.impl = NULL;
    }
    cflow_find_result_destroy(&state.pending_find);
    return status;
}

static cflow_status cflow_stream_for_each_impl(const cflow_stream *stream,
                                               cflow_value_fn action,
                                               void *user,
                                               const char **out_error) {
    stream_terminal_state state = {0};

    if (out_error) *out_error = NULL;
    if (!action) {
        if (out_error) *out_error = "for_each callback is null";
        return CFLOW_STATUS_INVALID_ARGUMENT;
    }
    state.kind = STREAM_TERMINAL_FOR_EACH;
    state.action = action;
    state.action_user = user;
    return stream_terminal_eval(stream, &state, out_error);
}

bool cflow_stream_count(const cflow_stream *stream,
                        size_t *out_count,
                        const char **out_error) {
    return cflow_stream_count_impl(stream, out_count, out_error) ==
        CFLOW_STATUS_OK;
}

bool cflow_stream_find_first(const cflow_stream *stream,
                             cflow_find_result *out,
                             const char **out_error) {
    return cflow_stream_find_first_impl(stream, out, out_error) ==
        CFLOW_STATUS_OK;
}

bool cflow_stream_for_each(const cflow_stream *stream,
                           cflow_value_fn action,
                           void *user,
                           const char **out_error) {
    return cflow_stream_for_each_impl(stream, action, user, out_error) ==
        CFLOW_STATUS_OK;
}

cflow_status_result cflow_stream_count_result(
    const cflow_stream *stream, size_t *out_count) {
    return stream_status_result(
        cflow_stream_count_impl(stream, out_count, NULL));
}

cflow_status_result cflow_stream_any_match_result(
    const cflow_stream *stream, cflow_filter_callable predicate,
    bool *out_matches) {
    return stream_status_result(cflow_stream_match_impl(
        stream, predicate, STREAM_TERMINAL_ANY_MATCH, out_matches, NULL));
}

cflow_status_result cflow_stream_all_match_result(
    const cflow_stream *stream, cflow_filter_callable predicate,
    bool *out_matches) {
    return stream_status_result(cflow_stream_match_impl(
        stream, predicate, STREAM_TERMINAL_ALL_MATCH, out_matches, NULL));
}

cflow_status_result cflow_stream_find_first_result(
    const cflow_stream *stream, cflow_find_result *out) {
    return stream_status_result(
        cflow_stream_find_first_impl(stream, out, NULL));
}

cflow_status_result cflow_stream_for_each_result(
    const cflow_stream *stream, cflow_value_fn action, void *user) {
    return stream_status_result(
        cflow_stream_for_each_impl(stream, action, user, NULL));
}

bool cflow_find_result_has_value(const cflow_find_result *result) {
    return result && result->impl;
}

const cmeta_type_desc *
cflow_find_result_type(const cflow_find_result *result) {
    const cflow_find_result_impl *impl = result
        ? (const cflow_find_result_impl *)result->impl : NULL;
    return impl ? impl->value.type : NULL;
}

const void *cflow_find_result_value(const cflow_find_result *result) {
    const cflow_find_result_impl *impl = result
        ? (const cflow_find_result_impl *)result->impl : NULL;
    return impl && impl->value.live ? impl->value.storage : NULL;
}

void cflow_find_result_destroy(cflow_find_result *result) {
    cflow_find_result_impl *impl;

    if (!result) return;
    impl = (cflow_find_result_impl *)result->impl;
    if (impl) {
        cflow_value_slot_destroy(&impl->value);
        free(impl);
    }
    result->impl = NULL;
}

void cflow_result_destroy(cflow_result *result) {
    cflow_result_storage_destroy(result);
}
