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

/* cflow_result owns one byte buffer and has no per-element destructor.
 * Result-producing adapters therefore accept only graphs whose value types
 * have TRIVIAL_COPY and TRIVIAL_DESTROY. */

/* Collection is a façade over the unified resumable runtime:
 * array source + unbounded demand + collecting observer. */
bool cflow_eval_array(const cflow_graph *graph,
                      const void *inputs,
                      size_t input_count,
                      cflow_result *out);

/* Evaluate a stream whose source was bound by cflow_stream_from_range/stream().
 * Returns false and leaves out zeroed when source admission or evaluation
 * fails. */
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
 * Deterministic Range admission failures terminate the collector before begin
 * without invoking its abort callback.
 * Interpreted streams may carry managed COPY/MOVE/DESTROY values through their
 * operator Graph when the Range declares CMETA_RANGE_CONSTRUCTS_VALUES.
 * accept() borrows each live value only for the duration of the callback and
 * must copy or move it before returning.
 */
bool cflow_eval_collect(const cflow_stream *stream,
                        cmeta_collector *collector,
                        const char **out_error);

/*
 * Count values that reach the terminal after executing the bound Stream's
 * Graph. This uses the interpreted resumable runtime and does not infer the
 * result from Range size metadata. Managed values are borrowed only for each
 * sink callback and are never retained by this terminal.
 *
 * out_count is required, is zeroed before validation, and is published only
 * after normal completion. out_error is optional and receives a borrowed
 * diagnostic for invalid input, Range admission, runtime, or size overflow.
 * An unbounded source must be bounded upstream (for example with take()).
 */
bool cflow_eval_count(const cflow_stream *stream,
                      size_t *out_count,
                      const char **out_error);

void cflow_result_destroy(cflow_result *result);

#ifdef __cplusplus
}
#endif

#endif
