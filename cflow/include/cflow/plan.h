#ifndef CFLOW_PLAN_H
#define CFLOW_PLAN_H

#include <cflow/adapters.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cflow_plan {
    void *impl;
    const cmeta_type_desc *input_type;
    const cmeta_type_desc *output_type;
    const char *error;
} cflow_plan;

typedef struct cflow_plan_compile_stats {
    size_t graph_nodes;
    size_t instructions;
    size_t map_callbacks;
    size_t inference_queries;
} cflow_plan_compile_stats;

/* Compile an already-normalized primitive Graph root into a direct synchronous
 * collection plan. The plan pre-resolves topology and execution step handlers;
 * execution never queries Graph/Node/Edge/Subgraph. Structured RELATION and
 * other unsupported resumable semantics are rejected instead of falling back. */
bool cflow_plan_compile(cflow_plan *plan,
                        const cflow_graph *graph,
                        cflow_plan_compile_stats *stats);

/* Convenience frontend: surface -> normalize -> structural optimize -> plan. */
bool cflow_plan_compile_surface(cflow_plan *plan,
                                const cflow_graph *surface,
                                cflow_plan_compile_stats *stats);

void cflow_plan_destroy(cflow_plan *plan);

/* Static capability query used by differential verification. */
bool cflow_plan_graph_supported(const cflow_graph *graph);

/* Execute the pre-decoded plan without Graph topology queries. */
bool cflow_plan_eval_array(const cflow_plan *plan,
                           const void *inputs,
                           size_t input_count,
                           cflow_result *out);

#ifdef __cplusplus
}
#endif
#endif
