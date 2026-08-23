#ifndef CFLOW_PLAN_H
#define CFLOW_PLAN_H

#include <cflow/adapters.h>
#include <cflow/executor.h>

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

typedef enum cflow_plan_execution_mode {
    CFLOW_PLAN_EXECUTION_SEQUENTIAL = 0,
    CFLOW_PLAN_EXECUTION_PARALLEL_REDUCE
} cflow_plan_execution_mode;

typedef struct cflow_plan_eval_options {
    cflow_plan_execution_mode mode;
    cflow_executor *executor;
    size_t max_tasks;
    size_t min_items_per_task;
} cflow_plan_eval_options;

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

/* True only when immutable Plan metadata proves that the plan has a supported
 * linear prefix and one terminal reducer with the complete ordered-parallel
 * admission contract. No property is inferred from a function pointer. */
bool cflow_plan_parallel_reduce_supported(const cflow_plan *plan);

/* Execute the pre-decoded plan without Graph topology queries. */
bool cflow_plan_eval_array(const cflow_plan *plan,
                           const void *inputs,
                           size_t input_count,
                           cflow_result *out);

/* Parallel mode is explicit and fail-fast: unsupported plans, insufficient
 * nonempty chunks, invalid options, or rejected tasks return false without a
 * sequential retry. The borrowed input and executor must outlive this call. */
bool cflow_plan_eval_array_with_options(
    const cflow_plan *plan,
    const void *inputs,
    size_t input_count,
    const cflow_plan_eval_options *options,
    cflow_result *out);

#ifdef __cplusplus
}
#endif
#endif
