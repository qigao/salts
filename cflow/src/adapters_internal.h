#ifndef CFLOW_ADAPTERS_INTERNAL_H
#define CFLOW_ADAPTERS_INTERNAL_H

#include <cflow/adapters.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal bounded form used to verify the public SIZE_MAX boundary without
 * requiring SIZE_MAX source values. */
bool cflow_eval_count_bounded(const cflow_stream *stream,
                              size_t max_count,
                              size_t *out_count,
                              const char **out_error);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_ADAPTERS_INTERNAL_H */
