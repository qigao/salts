#ifndef TURBOSTL_STREAM_H
#define TURBOSTL_STREAM_H

#include <cflow/adapters.h>
#include <cflow/stream_execution.h>
#include <turbostl/typed.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Optional TurboSTL/CFlow integration facade.
 *
 * This API provides modern, typed operations over TurboSTL containers. Fluent
 * names are a readability choice, not a promise of Java Stream compatibility.
 * The object retains a reusable CFlow Graph rather than single-use traversal
 * state. Each terminal adapter creates independent execution state; repeated
 * evaluation requires the source Range's REUSABLE contract. CFlow defines
 * operator, terminal, ordering, error, and parallel semantics.
 *
 * stream()/stream_keys()/stream_values()/stream_entries() and every fluent
 * operator are owned by CFlow. The bound TurboSTL container is borrowed and
 * must remain alive and unmodified until collection finishes.
 * Interpreted collection supports managed COPY/MOVE/DESTROY element types.
 * to_array() remains a trivial-value byte terminal and fails for managed T.
 * Its max_items argument is a hard capacity bound, not a truncating operation.
 * Use fluent skip()/take() for positional truncation; their bounds are Graph
 * semantics and each evaluation owns fresh counters.
 *
 * Example:
 *   stream(&input, &pipeline)->filter(&pipeline, keep)->map(&pipeline, map_fn);
 *   result = to_list_typed(&pipeline, OutputList, &output, output_limit);
 */
typedef cflow_stream turbostl_stream_t;
typedef cflow_stream_execution turbostl_stream_execution_t;
typedef cflow_find_result turbostl_find_result;
typedef cflow_status_result turbostl_status_result;

/* TurboSTL supplies the bounded state backend explicitly; CFlow continues to
 * own Graph and execution semantics. Returned options are process-lifetime
 * immutable data. The constructed Stream borrows its source container. */
const cflow_eval_options *turbostl_stream_eval_options(void);
turbostl_stream_t *turbostl_stream_from_object(
    turbostl_stream_t *stream, const void *object);
turbostl_stream_t *turbostl_stream_from_object_view(
    turbostl_stream_t *stream,
    const void *object,
    cmeta_container_view view);

#undef stream
#undef stream_keys
#undef stream_values
#undef stream_entries
#define stream(object, stream_ptr) \
    turbostl_stream_from_object((stream_ptr), (object))
#define stream_keys(object, stream_ptr) \
    turbostl_stream_from_object_view( \
        (stream_ptr), (object), CMETA_CONTAINER_VIEW_KEYS)
#define stream_values(object, stream_ptr) \
    turbostl_stream_from_object_view( \
        (stream_ptr), (object), CMETA_CONTAINER_VIEW_VALUES)
#define stream_entries(object, stream_ptr) \
    turbostl_stream_from_object_view( \
        (stream_ptr), (object), CMETA_CONTAINER_VIEW_ENTRIES)

typedef struct turbostl_collect_result {
    bool ok;
    cmeta_status status;
    const char *error;
    size_t count;
    cflow_status flow_status;
} turbostl_collect_result;

typedef struct turbostl_count_result {
    bool ok;
    const char *error;
    size_t count;
} turbostl_count_result;

static inline turbostl_collect_result
turbostl_stream_collect(const turbostl_stream_t *stream,
                        cmeta_collector collector) {
    turbostl_collect_result result = {
        false, CMETA_INVALID_ARGUMENT, NULL, 0u,
        CFLOW_STATUS_INVALID_ARGUMENT
    };
    cflow_collect_result collected =
        cflow_eval_collect_result(stream, &collector, &result.error);

    result.ok = cflow_collect_result_is_ok(collected);
    result.status = collected.collector_status;
    result.count = collected.count;
    result.flow_status = collected.status;
    return result;
}

static inline turbostl_count_result
turbostl_stream_count(const turbostl_stream_t *stream) {
    turbostl_count_result result = {false, NULL, 0u};

    result.ok = cflow_eval_count(stream, &result.count, &result.error);
    return result;
}

static inline cflow_stream_execution_status
turbostl_stream_collect_async(turbostl_stream_execution_t *execution,
                              const turbostl_stream_t *stream,
                              cflow_scheduler *scheduler,
                              cmeta_collector collector) {
    return cflow_stream_execution_start(
        execution, stream, scheduler, collector);
}

static inline void turbostl_stream_destroy(turbostl_stream_t *stream) {
    cflow_stream_destroy(stream);
}

static inline turbostl_status_result turbostl_stream_to_array_result(
    const turbostl_stream_t *stream,
    size_t max_items,
    cflow_result *out) {
    return cflow_eval_stream_limit_result(stream, max_items, out);
}

static inline bool turbostl_stream_count(
    const turbostl_stream_t *stream, size_t *out_count,
    const char **out_error) {
    return cflow_stream_count(stream, out_count, out_error);
}

static inline bool turbostl_stream_any_match(
    const turbostl_stream_t *stream, cflow_filter_callable predicate,
    bool *out_matches, const char **out_error) {
    return cflow_stream_any_match(
        stream, predicate, out_matches, out_error);
}

static inline bool turbostl_stream_all_match(
    const turbostl_stream_t *stream, cflow_filter_callable predicate,
    bool *out_matches, const char **out_error) {
    return cflow_stream_all_match(
        stream, predicate, out_matches, out_error);
}

static inline bool turbostl_stream_find_first(
    const turbostl_stream_t *stream, turbostl_find_result *out,
    const char **out_error) {
    return cflow_stream_find_first(stream, out, out_error);
}

static inline bool turbostl_stream_for_each(
    const turbostl_stream_t *stream, cflow_value_fn action, void *user,
    const char **out_error) {
    return cflow_stream_for_each(stream, action, user, out_error);
}

static inline turbostl_status_result turbostl_stream_count_result(
    const turbostl_stream_t *stream, size_t *out_count) {
    return cflow_stream_count_result(stream, out_count);
}

static inline turbostl_status_result turbostl_stream_any_match_result(
    const turbostl_stream_t *stream, cflow_filter_callable predicate,
    bool *out_matches) {
    return cflow_stream_any_match_result(stream, predicate, out_matches);
}

static inline turbostl_status_result turbostl_stream_all_match_result(
    const turbostl_stream_t *stream, cflow_filter_callable predicate,
    bool *out_matches) {
    return cflow_stream_all_match_result(stream, predicate, out_matches);
}

static inline turbostl_status_result turbostl_stream_find_first_result(
    const turbostl_stream_t *stream, turbostl_find_result *out) {
    return cflow_stream_find_first_result(stream, out);
}

static inline turbostl_status_result turbostl_stream_for_each_result(
    const turbostl_stream_t *stream, cflow_value_fn action, void *user) {
    return cflow_stream_for_each_result(stream, action, user);
}

static inline bool turbostl_status_result_is_ok(
    turbostl_status_result result) {
    return cflow_status_result_is_ok(result);
}

static inline const char *turbostl_status_result_message(
    turbostl_status_result result) {
    return cflow_status_result_message(result);
}

static inline bool turbostl_find_result_has_value(
    const turbostl_find_result *result) {
    return cflow_find_result_has_value(result);
}

static inline const cmeta_type_desc *turbostl_find_result_type(
    const turbostl_find_result *result) {
    return cflow_find_result_type(result);
}

static inline const void *turbostl_find_result_value(
    const turbostl_find_result *result) {
    return cflow_find_result_value(result);
}

static inline void turbostl_find_result_destroy(
    turbostl_find_result *result) {
    cflow_find_result_destroy(result);
}

static inline cmeta_collector
turbostl_container_collector(void *output, size_t limit) {
    const cmeta_container_desc *desc = cmeta_container_descriptor(output);
    cmeta_collector invalid = {0};
    if (desc != NULL && desc->collector != NULL)
        return desc->collector(output, limit);
    invalid.context = output;
    invalid.zero_output = output;
    invalid.limit = limit;
    invalid.status = CMETA_INVALID_ARGUMENT;
    return invalid;
}

/* Typed terminals use distinct names so the three-argument #53 surface never
 * relies on variadic-arity dispatch over arbitrary C expressions. Count stays
 * a prefixed function because a global count(...) macro would intercept C++
 * calls such as std::count(...). */
#define collector(container_type, output_ptr, limit) \
    CMETA_TYPED_CALL(container_type, collector, (output_ptr), (limit))
#define collect(stream_ptr, output_ptr, limit) \
    turbostl_stream_collect((stream_ptr), \
                            turbostl_container_collector((output_ptr), (limit)))
#define to_list(stream_ptr, output_ptr, limit) \
    collect((stream_ptr), (output_ptr), (limit))
#define collect_typed(stream_ptr, container_type, output_ptr, limit) \
    turbostl_stream_collect((stream_ptr), \
                            collector(container_type, (output_ptr), (limit)))
#define to_list_typed(stream_ptr, container_type, output_ptr, limit) \
    collect_typed((stream_ptr), container_type, (output_ptr), (limit))
#define collect_async(execution_ptr, stream_ptr, scheduler_ptr, output_ptr, limit) \
    turbostl_stream_collect_async(                                      \
        (execution_ptr), (stream_ptr), (scheduler_ptr),                 \
        turbostl_container_collector((output_ptr), (limit)))
#define collect_async_typed(execution_ptr, stream_ptr, scheduler_ptr,          \
                            container_type, output_ptr, limit)                 \
    turbostl_stream_collect_async(                                             \
        (execution_ptr), (stream_ptr), (scheduler_ptr),                        \
        collector(container_type, (output_ptr), (limit)))
#define to_array(stream_ptr, max_items, output_ptr) \
    cflow_eval_stream_limit((stream_ptr), (max_items), (output_ptr))
#define to_array_result(stream_ptr, max_items, output_ptr) \
    turbostl_stream_to_array_result( \
        (stream_ptr), (max_items), (output_ptr))

#ifdef __cplusplus
}
#endif

#endif /* TURBOSTL_STREAM_H */
