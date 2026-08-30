#ifndef CFLOW_SUBFLOW_H
#define CFLOW_SUBFLOW_H

#include <cflow/reactive.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Instantiate one immutable Subgraph as a dynamic resumable Subflow for one
 * copied source value. Static topology stays in cflow_graph; mutable state is
 * owned by the returned machine. Interpreted storage accepts trivial values
 * or complete COPY/MOVE/DESTROY lifecycle traits. */
bool cflow_resumable_from_subgraph(cflow_resumable *out,
                                    const cflow_graph *graph,
                                    cflow_subgraph_id subgraph,
                                    const void *input_value);
bool cflow_resumable_from_subgraph_with_options(
    cflow_resumable *out,
    const cflow_graph *graph,
    cflow_subgraph_id subgraph,
    const void *input_value,
    const cflow_eval_options *options);

#ifdef __cplusplus
}
#endif
#endif
