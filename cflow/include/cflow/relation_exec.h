#ifndef CFLOW_RELATION_EXEC_H
#define CFLOW_RELATION_EXEC_H

#include <cflow/runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lower one immutable RELATION node plus the current typed input into a
 * runtime resumable. The generic Run does not interpret coordination/result
 * policy; it only drives the returned VALUE/WAIT/DONE/ERROR machine. Current
 * byte storage requires TRIVIAL_COPY and TRIVIAL_DESTROY throughout Graph. */
bool cflow_resumable_from_relation(cflow_resumable *out,
                                    const cflow_graph *graph,
                                    const cflow_node *node,
                                    const void *input);
bool cflow_resumable_from_relation_with_options(
    cflow_resumable *out,
    const cflow_graph *graph,
    const cflow_node *node,
    const void *input,
    const cflow_eval_options *options);

#ifdef __cplusplus
}
#endif
#endif
