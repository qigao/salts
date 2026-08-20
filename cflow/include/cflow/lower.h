#ifndef CFLOW_LOWER_H
#define CFLOW_LOWER_H

#include <cflow/graph.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Surface Graphs may contain high-level structured operators. Normalization
 * creates an independent primitive-IR snapshot suitable for Run. The
 * destination must be zero-initialized/empty and must differ from src.
 * The pass validates its input and output and performs only static IR
 * rewriting. Runtime scheduling/resource state is outside this layer. */
bool cflow_graph_is_normalized(const cflow_graph *g);
bool cflow_graph_normalize(cflow_graph *dst, const cflow_graph *src);

#ifdef __cplusplus
}
#endif
#endif
