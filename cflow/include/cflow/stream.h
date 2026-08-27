#include <cflow/operators.h>
#include <cflow/meta.h>
#ifndef CFLOW_STREAM_H
#define CFLOW_STREAM_H

#include <cflow/graph.h>
#include <cmeta/range.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cflow_stream cflow_stream;
typedef cflow_stream *(*cflow_stream_slice_method)(cflow_stream *self,
                                                   size_t count);

#define CFLOW_STREAM_METHOD_1(method) \
    typedef cflow_stream *(*cflow_stream_##method##_method)( \
        cflow_stream *self, cflow_##method##_callable fn);
#define CFLOW_STREAM_METHOD_2(method) \
    typedef cflow_stream *(*cflow_stream_##method##_method)( \
        cflow_stream *self, const cflow_stream *other, cflow_##method##_callable fn);
#define CFLOW_STREAM_METHOD_I(n, method) CFLOW_STREAM_METHOD_##n(method)
#define CFLOW_STREAM_METHOD(n, method) CFLOW_STREAM_METHOD_I(n, method)
#define CFLOW_OP_ROW(E, method, margc, fnarg, subgrapharg, farity, p0, p1, p2, ret, out, card, subgraphrule, semantic, intrinsic_effects) \
    CFLOW_STREAM_METHOD(margc, method)
Replay(CFlowOperators, CFLOW_OP_ROW)
#undef CFLOW_OP_ROW
#undef CFLOW_STREAM_METHOD
#undef CFLOW_STREAM_METHOD_I
#undef CFLOW_STREAM_METHOD_1
#undef CFLOW_STREAM_METHOD_2

/* Explicit-self ISO C11 fluent Graph builder for modern typed collection
 * operations. This is not a single-use traversal object or a compatibility
 * implementation of another language's Stream contract. Operators append to
 * this Graph; each evaluation creates independent runtime state. A bound Range
 * may be evaluated repeatedly only when its own REUSABLE contract permits it. */
struct cflow_stream {
    cflow_graph graph;
    cmeta_range source_range;
    bool has_source_range;
    bool failed;
#define CFLOW_OP_ROW(E, method, margc, fnarg, subgrapharg, farity, p0, p1, p2, ret, out, card, subgraphrule, semantic, intrinsic_effects) \
    cflow_stream_##method##_method method;
Replay(CFlowOperators, CFLOW_OP_ROW)
#undef CFLOW_OP_ROW
    cflow_stream_slice_method take;
    cflow_stream_slice_method skip;
};

cflow_stream *cflow_stream_init(cflow_stream *s, const cmeta_type_desc *source_type);
cflow_stream *cflow_stream_from_range(cflow_stream *s, cmeta_range range);
void cflow_stream_destroy(cflow_stream *s);

/**
 * Append a positional TAKE operation and return `s` for fluent chaining.
 * `take(0)` performs no Source resume. Reaching a positive limit stops
 * unneeded upstream work as normal completion. Graph construction failure is
 * retained by `cflow_stream_error()` and marks the Stream failed.
 */
cflow_stream *cflow_stream_take(cflow_stream *s, size_t limit);
/**
 * Append a positional SKIP operation and return `s` for fluent chaining.
 * Dropped values do not consume downstream demand. Graph construction failure
 * is retained by `cflow_stream_error()` and marks the Stream failed.
 */
cflow_stream *cflow_stream_skip(cflow_stream *s, size_t count);

cflow_stream *cflow_stream_from_object(cflow_stream *s, const void *object);
cflow_stream *cflow_stream_from_object_view(cflow_stream *s, const void *object,
                                             cmeta_container_view view);

#define stream_range(range_expr, stream_ptr) \
    cflow_stream_from_range((stream_ptr), (range_expr))
#define stream(object, stream_ptr) \
    cflow_stream_from_object((stream_ptr), (object))
#define stream_keys(object, stream_ptr) \
    cflow_stream_from_object_view((stream_ptr), (object), CMETA_CONTAINER_VIEW_KEYS)
#define stream_values(object, stream_ptr) \
    cflow_stream_from_object_view((stream_ptr), (object), CMETA_CONTAINER_VIEW_VALUES)
#define stream_entries(object, stream_ptr) \
    cflow_stream_from_object_view((stream_ptr), (object), CMETA_CONTAINER_VIEW_ENTRIES)

#define CFLOW_STREAM_PROTO_1(method) \
    cflow_stream *cflow_stream_##method(cflow_stream *s, cflow_##method##_callable fn);
#define CFLOW_STREAM_PROTO_2(method) \
    cflow_stream *cflow_stream_##method(cflow_stream *s, const cflow_stream *other, \
                                        cflow_##method##_callable fn);
#define CFLOW_STREAM_PROTO_I(n, method) CFLOW_STREAM_PROTO_##n(method)
#define CFLOW_STREAM_PROTO(n, method) CFLOW_STREAM_PROTO_I(n, method)
#define CFLOW_OP_ROW(E, method, margc, fnarg, subgrapharg, farity, p0, p1, p2, ret, out, card, subgraphrule, semantic, intrinsic_effects) \
    CFLOW_STREAM_PROTO(margc, method)
Replay(CFlowOperators, CFLOW_OP_ROW)
#undef CFLOW_OP_ROW
#undef CFLOW_STREAM_PROTO
#undef CFLOW_STREAM_PROTO_I
#undef CFLOW_STREAM_PROTO_1
#undef CFLOW_STREAM_PROTO_2

bool cflow_stream_ok(const cflow_stream *s);
const char *cflow_stream_error(const cflow_stream *s);
const cmeta_type_desc *cflow_stream_output_type(const cflow_stream *s);
const cflow_graph *cflow_stream_graph(const cflow_stream *s);

#ifdef __cplusplus
}
#endif

#endif
