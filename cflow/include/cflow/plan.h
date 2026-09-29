#ifndef CFLOW_PLAN_H
#define CFLOW_PLAN_H

#include <cflow/adapters.h>
#include <cflow/executor.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Owning compiled-plan handle. Zero-initialize the complete object and use the
 * compile/destroy APIs for every state change. */
typedef struct cflow_plan {
    void *impl; /* Internal implementation carrier; never dereference or assign. */
    /* Read-only introspection, valid until the next compile or destroy. */
    const cmeta_type_desc *input_type;
    const cmeta_type_desc *output_type;
    const char *error; /* Borrowed diagnostic retained by the Plan. */
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

/**
 * Reusable bounded scratch for one immutable fused raw-batch Plan.
 *
 * Zero-initialize before init. The Plan must outlive the workspace. Init
 * performs all heap allocation needed by workspace-backed evaluation; destroy
 * releases it. A workspace is single-owner and must not be used concurrently.
 */
typedef struct cflow_plan_batch_workspace {
    void *impl;
} cflow_plan_batch_workspace;

/**
 * Borrowed workspace result.
 *
 * data remains valid only until the next evaluation using the same workspace
 * or until workspace destruction. It must not be passed to
 * cflow_result_destroy().
 */
typedef struct cflow_plan_batch_result {
    const void *data;
    size_t count;
    const cmeta_type_desc *type;
} cflow_plan_batch_result;

/* Compile an already-normalized primitive Graph root into a direct synchronous
 * collection plan. The plan pre-resolves topology and execution step handlers;
 * execution never queries Graph/Node/Edge/Subgraph. Sequential materialized
 * execution accepts either TRIVIAL_COPY/TRIVIAL_DESTROY values or values with
 * COPY/MOVE/DESTROY lifecycle traits. Structured RELATION and other unsupported
 * resumable semantics are rejected instead of falling back. */
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

/**
 * Return true only for fused value Plans whose FILTER/MAP calls all expose
 * canonical raw-batch dispatch and whose value storage is trivial.
 */
bool cflow_plan_batch_workspace_supported(const cflow_plan *plan);

/**
 * Reserve bounded reusable scratch for at most item_capacity input values.
 *
 * Fails for unsupported Plans, zero capacity, or size/allocation overflow.
 * Failure leaves workspace zeroed.
 */
bool cflow_plan_batch_workspace_init(cflow_plan_batch_workspace *workspace,
                                     const cflow_plan *plan,
                                     size_t item_capacity);

/** Release workspace storage and restore the handle to zero. */
void cflow_plan_batch_workspace_destroy(cflow_plan_batch_workspace *workspace);

/** Return the admitted item capacity, or zero for an invalid workspace. */
size_t cflow_plan_batch_workspace_capacity(const cflow_plan_batch_workspace *workspace);

/** Return the pre-reserved scratch bytes owned by the workspace. */
size_t cflow_plan_batch_workspace_bytes(const cflow_plan_batch_workspace *workspace);

/**
 * Execute a supported fused Plan using only pre-reserved workspace storage.
 *
 * No heap allocation or release occurs in this call. input_count must not
 * exceed the workspace capacity. The returned result is borrowed from the
 * workspace and is overwritten by the next evaluation on that workspace.
 * Failure leaves result zeroed and the workspace reusable.
 */
bool cflow_plan_eval_array_workspace(const cflow_plan *plan,
                                     const void *inputs,
                                     size_t input_count,
                                     cflow_plan_batch_workspace *workspace,
                                     cflow_plan_batch_result *out);

/* Execute the pre-decoded plan without Graph topology queries. Caller input is
 * borrowed. A successful result independently owns every returned value and
 * must be released with cflow_result_destroy(). */
bool cflow_plan_eval_array(const cflow_plan *plan,
                           const void *inputs,
                           size_t input_count,
                           cflow_result *out);

/* Parallel mode currently requires trivial value storage and is explicit and
 * fail-fast: unsupported plans, insufficient
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
