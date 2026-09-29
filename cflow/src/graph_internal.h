#ifndef CFLOW_GRAPH_INTERNAL_H
#define CFLOW_GRAPH_INTERNAL_H

#include <cflow/graph.h>

#include <stdint.h>
#include <stdbool.h>

/* Reserve a process-unique nonzero Graph mutation token. Tokens are skipped
 * when a later allocation fails, but are never reused within the process. */
bool cflow_graph_version_acquire(uint64_t *version);

/* Private marker-aware path for explicit logical typed adapters. Ordinary
 * cmeta_callable_bind() deliberately remains finite-signature-only. */
bool cflow_graph_explicit_adapter_callable_valid(cmeta_callable fn);
bool cflow_graph_create_explicit_typed_adapter_node(
    cflow_graph *g,
    cflow_subgraph_id subgraph,
    cflow_op op,
    cmeta_callable fn,
    const cmeta_type_desc *input_type,
    const cmeta_type_desc *output_type,
    cflow_node_id *out_node);
bool cflow_graph_add_explicit_typed_adapter(
    cflow_graph *g,
    cflow_op op,
    cmeta_callable fn,
    const cmeta_type_desc *input_type,
    const cmeta_type_desc *output_type);
bool cflow_graph_create_explicit_map_adapter_node(
    cflow_graph *g,
    cflow_subgraph_id subgraph,
    cmeta_callable fn,
    const cmeta_type_desc *input_type,
    const cmeta_type_desc *output_type,
    cflow_node_id *out_node);
bool cflow_graph_add_explicit_map_adapter(
    cflow_graph *g,
    cmeta_callable fn,
    const cmeta_type_desc *input_type,
    const cmeta_type_desc *output_type);

#endif