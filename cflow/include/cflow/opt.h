#ifndef CFLOW_OPT_H
#define CFLOW_OPT_H

#include <cflow/graph.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cflow_opt_pass {
    CMETA_OPT_CANONICALIZE       = 1u << 0,
    CMETA_OPT_DEAD_SUBGRAPHS     = 1u << 1,
    CMETA_OPT_MAP_FUSION         = 1u << 2,
    CMETA_OPT_RELATION_SIMPLIFY  = 1u << 3,
    CMETA_OPT_PROPERTY_REWRITES  = 1u << 4,
    CMETA_OPT_DEFAULT = CMETA_OPT_CANONICALIZE |
                        CMETA_OPT_DEAD_SUBGRAPHS |
                        CMETA_OPT_MAP_FUSION |
                        CMETA_OPT_RELATION_SIMPLIFY |
                        CMETA_OPT_PROPERTY_REWRITES
} cflow_opt_pass;

typedef struct cflow_opt_options {
    unsigned passes;
} cflow_opt_options;

/** Stable semantic rewrite identifiers emitted by the optimizer. */
typedef enum cflow_opt_rule {
    CFLOW_OPT_RULE_IDEMPOTENT_MAP_ELIMINATION = 1
} cflow_opt_rule;

/** One source-coordinate proof-trace event. */
typedef struct cflow_opt_rewrite_event {
    cflow_opt_rule rule;
    cflow_subgraph_id source_subgraph;
    cflow_node_id retained_node;
    size_t retained_callable_index;
    cflow_node_id removed_node;
    size_t removed_callable_index;
} cflow_opt_rewrite_event;

/** Owned opaque optimizer trace. Initialize to zero and do not copy. */
typedef struct cflow_opt_trace {
    void *impl;
} cflow_opt_trace;

typedef struct cflow_opt_stats {
    size_t subgraphs_before;
    size_t subgraphs_after;
    size_t nodes_before;
    size_t nodes_after;
    size_t transforms_canonicalized;
    size_t map_nodes_fused;
    size_t relation_schemas_simplified;
    size_t dead_subgraphs_removed;
    size_t effect_blocked_map_fusions;
    size_t effect_blocked_relation_simplifications;
    size_t property_blocked_relation_simplifications;
    size_t idempotent_maps_eliminated;
    size_t property_blocked_idempotent_eliminations;
} cflow_opt_stats;

/* Optimize one normalized primitive Graph into a new normalized Graph.
 * dst must be empty. The source remains immutable. */
bool cflow_graph_optimize(cflow_graph *dst,
                          const cflow_graph *src,
                          cflow_opt_options options,
                          cflow_opt_stats *stats);

/** Optimize and transactionally commit an owned proof trace.
 * @param dst Empty destination Graph which owns optimized IR on success.
 * @param src Borrowed immutable normalized source Graph.
 * @param options Explicit pass mask; zero selects `CMETA_OPT_DEFAULT`.
 * @param stats Optional statistics committed by the ordinary optimizer path.
 * @param trace Required zero-state output; owns its event storage on success.
 * @return true on complete Graph and trace commit; false leaves `trace` zero.
 * The binding is valid only for the exact unchanged Graph objects. */
bool cflow_graph_optimize_with_trace(cflow_graph *dst,
                                     const cflow_graph *src,
                                     cflow_opt_options options,
                                     cflow_opt_stats *stats,
                                     cflow_opt_trace *trace);

/** Release owned event storage and restore zero state. NULL/repeated calls are safe. */
void cflow_opt_trace_destroy(cflow_opt_trace *trace);
/** Return the number of committed events, or zero for NULL/zero state. */
size_t cflow_opt_trace_count(const cflow_opt_trace *trace);
/** Copy one event without exposing owned storage. Invalid input leaves `event`
 * unchanged and returns false. */
bool cflow_opt_trace_event_at(const cflow_opt_trace *trace,
                              size_t index,
                              cflow_opt_rewrite_event *event);
/** Return true only for the exact live source/output objects and versions. */
bool cflow_opt_trace_bound_to(const cflow_opt_trace *trace,
                              const cflow_graph *source,
                              const cflow_graph *optimized);

#ifdef __cplusplus
}
#endif
#endif
