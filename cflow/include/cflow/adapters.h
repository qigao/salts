#ifndef CFLOW_ADAPTERS_H
#define CFLOW_ADAPTERS_H

#include <cflow/runtime.h>
#include <cflow/status.h>
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

/* Opaque owned result for find_first. Zero-initialize before use. A successful
 * empty result keeps impl NULL; a found managed value is independently
 * copy-constructed and remains live until cflow_find_result_destroy(). */
typedef struct cflow_find_result {
    void *impl;
} cflow_find_result;

/**
 * Allocation-free collection outcome. It owns no resources.
 *
 * status classifies CFlow admission or Runtime execution. collector_status,
 * collector_state, and count are the exact final CMeta transaction snapshot.
 * A Runtime failure can therefore coexist with CMETA_CALLBACK_ERROR after the
 * Runtime aborts an otherwise healthy collector.
 */
typedef struct cflow_collect_result {
    cflow_status status;
    cmeta_status collector_status;
    cmeta_collector_state collector_state;
    size_t count;
} cflow_collect_result;

bool cflow_collect_result_is_ok(cflow_collect_result result);

/* cflow_result owns one contiguous value buffer. Result-producing adapters
 * accept only graphs whose value types have TRIVIAL_COPY and TRIVIAL_DESTROY;
 * sequential compiled Plans may additionally return COPY/MOVE/DESTROY values.
 * cflow_result_destroy() applies the result type's lifecycle when required. */

/* Byte collection is a façade over the unified resumable runtime. Structured
 * variants classify source admission, trivial-storage admission, allocation,
 * capacity, and Runtime failure without retaining borrowed diagnostics. They
 * always zero out on entry and transfer owned bytes only on success. */
cflow_status_result cflow_eval_array_result(
    const cflow_graph *graph,
    const void *inputs,
    size_t input_count,
    cflow_result *out);

/* Compatibility wrapper over cflow_eval_array_result(). */
bool cflow_eval_array(const cflow_graph *graph,
                      const void *inputs,
                      size_t input_count,
                      cflow_result *out);

/* Evaluate a stream whose source was bound by cflow_stream_from_range/stream().
 * Byte results require trivial copy/destroy throughout the Graph. */
cflow_status_result cflow_eval_stream_result(
    const cflow_stream *stream, cflow_result *out);

/* Compatibility wrapper over cflow_eval_stream_result(). */
bool cflow_eval_stream(const cflow_stream *stream, cflow_result *out);

/**
 * Evaluate a bound Stream into an owned byte array containing at most
 * max_items values. One more value returns CAPACITY_EXCEEDED and leaves out
 * zeroed. Destroy a successful result with cflow_result_destroy().
 */
cflow_status_result cflow_eval_stream_limit_result(
    const cflow_stream *stream,
    size_t max_items,
    cflow_result *out);

/* Compatibility wrapper over cflow_eval_stream_limit_result(). */
bool cflow_eval_stream_limit(const cflow_stream *stream,
                             size_t max_items,
                             cflow_result *out);

/*
 * Evaluate a bound Stream directly into a zero-state transactional CMeta
 * collector. The collector owns its output transaction; any runtime or
 * collector failure aborts it. cflow_eval_collect_result() preserves the
 * CFlow outcome and the exact final Collector transaction as separate status
 * domains. out_error receives a borrowed diagnostic when non-NULL and must
 * not be freed or retained beyond the source's documented lifetime.
 * Deterministic Range admission failures terminate the collector before begin
 * without invoking its abort callback.
 * Interpreted streams may carry managed COPY/MOVE/DESTROY values through their
 * operator Graph when the Range declares CMETA_RANGE_CONSTRUCTS_VALUES.
 * accept() borrows each live value only for the duration of the callback and
 * must copy or move it before returning.
 */
cflow_collect_result cflow_eval_collect_result(
    const cflow_stream *stream,
    cmeta_collector *collector,
    const char **out_error);

/* Compatibility wrapper. Returns cflow_collect_result_is_ok() for the same
 * single evaluation and preserves the legacy collector/out_error behavior. */
bool cflow_eval_collect(const cflow_stream *stream,
                        cmeta_collector *collector,
                        const char **out_error);

/* Synchronous bound-Stream terminals. Scalar outputs are reset to zero on
 * entry and receive their terminal value only on success. any_match/all_match
 * validate their typed predicate before source admission. any_match,
 * all_match, and find_first short-circuit the current
 * Run when their result becomes final. out_error is optional, borrowed, and
 * must not be freed; library diagnostics have static storage while source
 * diagnostics retain the source's documented lifetime.
 *
 * stream borrows a reusable bound Stream for the complete call. count returns
 * zero for empty input. any_match returns false and all_match returns true for
 * empty input. find_first leaves a zero result for empty input. for_each action
 * receives user and a callback-duration borrowed value. Every function returns
 * true only after a normal terminal result or its own successful short circuit;
 * invalid arguments, type mismatch, unsupported lifecycle, allocation,
 * callback, source, and Runtime errors return false.
 *
 * Example:
 *   size_t n = 0; const char *error = NULL;
 *   bool ok = cflow_stream_count(&stream, &n, &error); */
bool cflow_stream_count(const cflow_stream *stream,
                        size_t *out_count,
                        const char **out_error);
bool cflow_stream_any_match(const cflow_stream *stream,
                            cflow_filter_callable predicate,
                            bool *out_matches,
                            const char **out_error);
bool cflow_stream_all_match(const cflow_stream *stream,
                            cflow_filter_callable predicate,
                            bool *out_matches,
                            const char **out_error);
bool cflow_stream_find_first(const cflow_stream *stream,
                             cflow_find_result *out,
                             const char **out_error);
bool cflow_stream_for_each(const cflow_stream *stream,
                           cflow_value_fn action,
                           void *user,
                           const char **out_error);

/* Structured terminal variants classify failures without retaining borrowed
 * Runtime diagnostics. Use cflow_status_result_message() for canonical static
 * text. Value outputs follow the same zero-on-failure contract as the legacy
 * bool variants above. The returned Result owns no resources. */
cflow_status_result cflow_stream_count_result(
    const cflow_stream *stream, size_t *out_count);
cflow_status_result cflow_stream_any_match_result(
    const cflow_stream *stream, cflow_filter_callable predicate,
    bool *out_matches);
cflow_status_result cflow_stream_all_match_result(
    const cflow_stream *stream, cflow_filter_callable predicate,
    bool *out_matches);
cflow_status_result cflow_stream_find_first_result(
    const cflow_stream *stream, cflow_find_result *out);
cflow_status_result cflow_stream_for_each_result(
    const cflow_stream *stream, cflow_value_fn action, void *user);

bool cflow_find_result_has_value(const cflow_find_result *result);
/* Accessors return borrowed metadata/value pointers valid until destroy.
 * destroy accepts NULL or a zero result and restores the handle to zero. */
const cmeta_type_desc *
cflow_find_result_type(const cflow_find_result *result);
const void *cflow_find_result_value(const cflow_find_result *result);
void cflow_find_result_destroy(cflow_find_result *result);

void cflow_result_destroy(cflow_result *result);

#ifdef __cplusplus
}
#endif

#endif
