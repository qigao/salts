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

#ifdef __cplusplus
}
#endif
#endif
