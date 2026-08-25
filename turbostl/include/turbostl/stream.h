#ifndef TURBOSTL_STREAM_H
#define TURBOSTL_STREAM_H

#include <cflow/adapters.h>
#include <turbostl/typed.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Optional TurboSTL/CFlow integration facade.
 *
 * stream()/stream_keys()/stream_values()/stream_entries() and every fluent
 * operator are owned by CFlow. The bound TurboSTL container is borrowed and
 * must remain alive and unmodified until collection finishes.
 * Interpreted collection supports managed COPY/MOVE/DESTROY element types.
 * to_array() remains a trivial-value byte terminal and fails for managed T.
 *
 * Example:
 *   stream(&input, &pipeline)->filter(&pipeline, keep)->map(&pipeline, map_fn);
 *   result = to_list_typed(&pipeline, OutputList, &output, output_limit);
 */
typedef cflow_stream turbostl_stream_t;

typedef struct turbostl_collect_result {
    bool ok;
    cmeta_status status;
    const char *error;
    size_t count;
} turbostl_collect_result;

static inline turbostl_collect_result
turbostl_stream_collect(const turbostl_stream_t *stream,
                        cmeta_collector collector) {
    turbostl_collect_result result = {false, CMETA_INVALID_ARGUMENT, NULL, 0u};
    result.ok = cflow_eval_collect(stream, &collector, &result.error);
    result.status = collector.status;
    result.count = collector.count;
    return result;
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
 * relies on variadic-arity dispatch over arbitrary C expressions. */
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
#define to_array(stream_ptr, max_items, output_ptr) \
    cflow_eval_stream_limit((stream_ptr), (max_items), (output_ptr))

#ifdef __cplusplus
}
#endif

#endif /* TURBOSTL_STREAM_H */
