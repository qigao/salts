#ifndef CFLOW_VERIFY_H
#define CFLOW_VERIFY_H

#include <cflow/adapters.h>
#include <cflow/opt.h>

#include <stdbool.h>
#include <stddef.h>

typedef struct cflow_verify_report {
    size_t input_count;
    size_t output_count;
    size_t normalized_subgraphs;
    size_t optimized_subgraphs;
    size_t normalized_nodes;
    size_t optimized_nodes;
    cflow_opt_stats opt_stats;
    bool compiled_plan_checked;
    size_t compiled_instructions;
    const char *error;
} cflow_verify_report;

/* Structural equality ignores mutable diagnostics/version counters but compares
 * executable IR: topology, node descriptors, relation schema and fused chains. */
bool cflow_graph_structural_equal(const cflow_graph *a, const cflow_graph *b);
bool cflow_result_equal(const cflow_result *a, const cflow_result *b);

/* C-only differential verification pipeline:
 *   validate surface
 *   normalize + validate
 *   normalize(normalized) and compare structure
 *   optimize + validate
 *   optimize(optimized) and compare structure
 *   execute surface/normalized/optimized and compare observable output
 *   when eligible, compile optimized IR to a direct plan and compare again
 */
bool cflow_verify_pipeline(const cflow_graph *surface,
                           const void *inputs,
                           size_t input_count,
                           cflow_verify_report *report);

#endif
