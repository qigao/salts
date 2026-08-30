#ifndef CFLOW_EFFECT_H
#define CFLOW_EFFECT_H

#include <cflow/graph.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Recursive static effect analysis over immutable Graph/Subgraph IR. PURE is
 * the empty effect set. UNKNOWN is returned conservatively for malformed or
 * recursively referenced IR. Publisher effects are intentionally outside this
 * analysis because Publishers belong to a Subscription, not a Graph. */
cmeta_effects cflow_node_effects(const cflow_graph *g, const cflow_node *node);
cmeta_effects cflow_subgraph_effects(const cflow_graph *g, cflow_subgraph_id subgraph);
cmeta_effects cflow_graph_effects(const cflow_graph *g);

#ifdef __cplusplus
}
#endif
#endif
