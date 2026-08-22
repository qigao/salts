#ifndef CFLOW_ADAPTERS_H
#define CFLOW_ADAPTERS_H

#include <cflow/runtime.h>
#include <cflow/stream.h>
#include <cmeta/collector.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cflow_result {
    void *data;
    size_t count;
    const cmeta_type_desc *type;
} cflow_result;

/* Collection is a façade over the unified resumable runtime:
 * array source + unbounded demand + collecting observer. */
bool cflow_eval_array(const cflow_graph *graph,
                      const void *inputs,
                      size_t input_count,
                      cflow_result *out);

/* Evaluate a stream whose source was bound by cflow_stream_from_range/stream(). */
bool cflow_eval_stream(const cflow_stream *stream, cflow_result *out);

/**
 * Evaluate a bound Stream into an owned byte array containing at most
 * max_items values. Returns false and leaves out zeroed if evaluation fails or
 * one more value would exceed the limit. Destroy a successful result with
 * cflow_result_destroy().
 */
bool cflow_eval_stream_limit(const cflow_stream *stream,
                             size_t max_items,
                             cflow_result *out);

/*
 * Evaluate a bound Stream directly into a zero-state transactional CMeta
 * collector. The collector owns its output transaction; any runtime or
 * collector failure aborts it. Returns true only after commit. out_error
 * receives a borrowed static diagnostic when non-NULL; collector->status
 * preserves a more specific collector failure such as capacity exceeded.
 */
bool cflow_eval_collect(const cflow_stream *stream,
                        cmeta_collector *collector,
                        const char **out_error);

void cflow_result_destroy(cflow_result *result);

#ifdef __cplusplus
}
#endif

#endif
