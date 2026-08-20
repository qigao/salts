#ifndef CFLOW_ADAPTERS_H
#define CFLOW_ADAPTERS_H

#include <cflow/runtime.h>
#include <cflow/stream.h>

#include <stdbool.h>
#include <stddef.h>

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
void cflow_result_destroy(cflow_result *result);

#endif
