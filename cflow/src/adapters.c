#include <cflow/adapters.h>
#include <cflow/lower.h>
#include <cflow/sources.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct collector {
    unsigned char *data;
    size_t count;
    size_t capacity;
    const cmeta_type_desc *type;
    bool done;
    const char *error;
} collector;

static bool collect_value(void *user, const cmeta_type_desc *type, const void *value) {
    collector *c = (collector *)user;
    if (!c->type) c->type = type;
    if (!cmeta_type_equal(c->type, type)) return false;
    if (c->count == c->capacity) {
        size_t next = c->capacity ? c->capacity * 2 : 8;
        if (next < c->capacity || type->size > SIZE_MAX / next) return false;
        unsigned char *p = realloc(c->data, next * type->size);
        if (!p) return false;
        c->data = p; c->capacity = next;
    }
    memcpy(c->data + c->count * type->size, value, type->size);
    ++c->count;
    return true;
}
static void collect_error(void *user, const char *message) {
    collector *c = (collector *)user; c->error = message;
}
static void collect_done(void *user) {
    collector *c = (collector *)user; c->done = true;
}

static bool cflow_eval_source(const cflow_graph *graph,
                              cflow_source *source,
                              cflow_result *out) {
    if (!graph || !source || !out) return false;
    memset(out, 0, sizeof(*out));

    cflow_graph normalized = {0};
    normalized.root = CMETA_INVALID_ID;
    const cflow_graph *exec_graph = graph;
    if (!cflow_graph_is_normalized(graph)) {
        if (!cflow_graph_normalize(&normalized, graph)) return false;
        exec_graph = &normalized;
    }

    cflow_scheduler scheduler = {0};
    cflow_run run = {0};
    collector c = {0};
    cflow_sink_callbacks sink_cb = { collect_value, collect_error, collect_done, &c };
    cflow_sink sink = cflow_sink_from_callbacks(&sink_cb);
    if (!cflow_scheduler_test_init(&scheduler)) {
        cflow_graph_destroy(&normalized);
        cflow_source_destroy(source);
        return false;
    }
    if (!cflow_run_open(&run, exec_graph, source, &scheduler, &sink)) {
        cflow_source_destroy(source);
        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        return false;
    }
    if (!cflow_run_request(&run, SIZE_MAX)) {
        cflow_run_close(&run);
        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        return false;
    }
    (void)cflow_scheduler_run_until_idle(&scheduler, 0);
    bool ok = c.done && !c.error && !cflow_run_error(&run);
    if (ok) {
        out->data = c.data;
        out->count = c.count;
        out->type = c.type ? c.type : cflow_graph_output_type(exec_graph);
        c.data = NULL;
    }
    free(c.data);
    cflow_run_close(&run);
    cflow_scheduler_destroy(&scheduler);
    cflow_graph_destroy(&normalized);
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
    return cflow_eval_source(graph, &source, out);
}

bool cflow_eval_stream(const cflow_stream *stream, cflow_result *out) {
    cflow_source source = {0};
    if (!stream || !stream->has_source_range || !out) return false;
    if (!cflow_source_from_range(&source, stream->source_range)) return false;
    return cflow_eval_source(&stream->graph, &source, out);
}

void cflow_result_destroy(cflow_result *result) {
    if (!result) return;
    free(result->data); memset(result, 0, sizeof(*result));
}
