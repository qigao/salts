#include <cflow/operators.h>
#include <cflow/stream.h>

#include <string.h>

static cflow_stream *mark_result(cflow_stream *s, bool ok) {
    if (!s) return NULL;
    if (!ok) s->failed = true;
    return s;
}

static bool validate_callable(cflow_stream *s, cmeta_callable fn, cmeta_callable *out) {
    if (!s || !out) return false;
    if (!cmeta_callable_bind(fn, out)) {
        s->failed = true;
        s->graph.error = "callable signature/effect/property contract is invalid";
        return false;
    }
    return true;
}

#define CMETA_RESOLVE_CALLABLE(s, wrapper, out) do { \
    if (!validate_callable((s), (wrapper).fn, &(out))) return (s); \
} while (0)

cflow_stream *cflow_stream_init(cflow_stream *s, const cmeta_type_desc *source_type) {
    if (!s || !source_type) return NULL;
    memset(s, 0, sizeof(*s));
    cflow_graph_init(&s->graph, source_type);
    if (s->graph.error) return NULL;
#define CFLOW_OP_ROW(E, method, margc, fnarg, subgrapharg, farity, p0, p1, p2, ret, out, card, subgraphrule, semantic, intrinsic_effects) \
    s->method = cflow_stream_##method;
Replay(CFlowOperators, CFLOW_OP_ROW)
#undef CFLOW_OP_ROW
    s->take = cflow_stream_take;
    s->skip = cflow_stream_skip;
    return s;
}

cflow_stream *cflow_stream_from_range(cflow_stream *s, cmeta_range range) {
    if (!range.object || !range.element_type || !range.next) return NULL;
    if (!cflow_stream_init(s, range.element_type)) return NULL;
    s->source_range = range;
    s->has_source_range = true;
    return s;
}

cflow_stream *cflow_stream_from_object_view(cflow_stream *s, const void *object,
                                             cmeta_container_view view) {
    cmeta_range range;
    if (!s || !cmeta_container_range_view(object, view, &range)) return NULL;
    return cflow_stream_from_range(s, range);
}

cflow_stream *cflow_stream_from_object(cflow_stream *s, const void *object) {
    return cflow_stream_from_object_view(s, object, CMETA_CONTAINER_VIEW_DEFAULT);
}

void cflow_stream_destroy(cflow_stream *s) {
    if (!s) return;
    cflow_graph_destroy(&s->graph);
    memset(s, 0, sizeof(*s));
}

cflow_stream *cflow_stream_take(cflow_stream *s, size_t limit) {
    if (!s || s->failed) return s;
    return mark_result(s, cflow_graph_take(&s->graph, limit));
}

cflow_stream *cflow_stream_skip(cflow_stream *s, size_t count) {
    if (!s || s->failed) return s;
    return mark_result(s, cflow_graph_skip(&s->graph, count));
}

#define CFLOW_STREAM_IMPL_1(E, method) \
    cflow_stream *cflow_stream_##method(cflow_stream *s, cflow_##method##_callable callable) { \
        cmeta_callable fn; \
        if (!s || s->failed) return s; \
        CMETA_RESOLVE_CALLABLE(s, callable, fn); \
        return mark_result(s, cflow_graph_add(&s->graph, CFLOW_OP_##E, fn, NULL)); \
    }
#define CFLOW_STREAM_IMPL_2(E, method) \
    cflow_stream *cflow_stream_##method(cflow_stream *s, const cflow_stream *other, \
                                        cflow_##method##_callable callable) { \
        cmeta_callable fn; \
        if (!s || s->failed) return s; \
        if (!other || other->failed) { \
            s->failed = true; s->graph.error = "subgraph branch is invalid"; return s; \
        } \
        CMETA_RESOLVE_CALLABLE(s, callable, fn); \
        return mark_result(s, cflow_graph_add(&s->graph, CFLOW_OP_##E, fn, &other->graph)); \
    }
#define CFLOW_STREAM_IMPL_I(n, E, method) CFLOW_STREAM_IMPL_##n(E, method)
#define CFLOW_STREAM_IMPL(n, E, method) CFLOW_STREAM_IMPL_I(n, E, method)
#define CFLOW_OP_ROW(E, method, margc, fnarg, subgrapharg, farity, p0, p1, p2, ret, out, card, subgraphrule, semantic, intrinsic_effects) \
    CFLOW_STREAM_IMPL(margc, E, method)
Replay(CFlowOperators, CFLOW_OP_ROW)
#undef CFLOW_OP_ROW
#undef CFLOW_STREAM_IMPL
#undef CFLOW_STREAM_IMPL_I
#undef CFLOW_STREAM_IMPL_1
#undef CFLOW_STREAM_IMPL_2

bool cflow_stream_ok(const cflow_stream *s) { return s && !s->failed && !s->graph.error; }
const char *cflow_stream_error(const cflow_stream *s) { return s ? s->graph.error : "stream is null"; }
const cmeta_type_desc *cflow_stream_output_type(const cflow_stream *s) {
    return s ? cflow_graph_output_type(&s->graph) : NULL;
}
const cflow_graph *cflow_stream_graph(const cflow_stream *s) { return s ? &s->graph : NULL; }

#undef CMETA_RESOLVE_CALLABLE
