#ifndef CFLOW_SUBRUN_H
#define CFLOW_SUBRUN_H

#include <cflow/runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Instantiate one immutable Subgraph as a dynamic resumable SubRun for one
 * copied source value. Static topology stays in cflow_graph; mutable state is
 * owned by the returned machine. Interpreted storage accepts trivial values
 * or complete COPY/MOVE/DESTROY lifecycle traits. */
bool cflow_resumable_from_subgraph(cflow_resumable *out,
                                    const cflow_graph *graph,
                                    cflow_subgraph_id subgraph,
                                    const void *source_value);

#ifdef __cplusplus
}
#endif
#endif
