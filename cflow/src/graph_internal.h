#ifndef CFLOW_GRAPH_INTERNAL_H
#define CFLOW_GRAPH_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>

/* Reserve a process-unique nonzero Graph mutation token. Tokens are skipped
 * when a later allocation fails, but are never reused within the process. */
bool cflow_graph_version_acquire(uint64_t *version);

/* Internal MAP constructor for adapter-only callables whose canonical input
 * and output types are supplied explicitly instead of through cmeta_sig. */
bool cflow_graph_add_explicit_adapter_map(
    cflow_graph *graph, cmeta_callable adapter,
    const cmeta_type_desc *input_type,
    const cmeta_type_desc *output_type);

bool cflow_graph_create_explicit_adapter_map_node(
    cflow_graph *graph, cflow_subgraph_id subgraph,
    cmeta_callable adapter,
    const cmeta_type_desc *input_type,
    const cmeta_type_desc *output_type,
    cflow_node_id *out_node);

#endif
