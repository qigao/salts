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
 * Fluent skip()/take() are positional CFlow Graph operations. Their immutable
 * bounds are reusable, while every execution owns fresh counters. take(0)
 * performs no Source resume and reaching a limit completes normally.
 * turbostl_stream_count() executes the complete interpreted Graph, borrows
 * each terminal value without retaining it, and publishes only on completion.
 *
 * Example:
 *   stream(&input, &pipeline)->filter(&pipeline, keep)->map(&pipeline, map_fn);
 *   result = to_list_typed(&pipeline, OutputList, &output, output_limit);
 */
typedef cflow_stream turbostl_stream_t;
typedef cflow_stream_execution turbostl_stream_execution_t;

typedef struct turbostl_collect_result {
    bool ok;
    cmeta_status status;
    const char *error;
    size_t count;
} turbostl_collect_result;

typedef struct turbostl_count_result {
    bool ok;
    const char *error;
    size_t count;
} turbostl_count_result;

static inline turbostl_collect_result
turbostl_stream_collect(const turbostl_stream_t *stream,
                        cmeta_collector collector) {
    turbostl_collect_result result = {false, CMETA_INVALID_ARGUMENT, NULL, 0u};
    result.ok = cflow_eval_collect(stream, &collector, &result.error);
    result.status = collector.status;
    result.count = collector.count;
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

#ifdef __cplusplus
}
#endif

#endif /* TURBOSTL_STREAM_H */
