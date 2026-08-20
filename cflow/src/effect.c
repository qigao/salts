#include <cflow/effect.h>

#include <stdlib.h>

static cmeta_effects fn_effects(cmeta_callable fn) {
    return cmeta_effects_valid(fn.meta.effects) ? fn.meta.effects : CMETA_EFFECT_UNKNOWN;
}

static cmeta_effects subgraph_effects_rec(const cflow_graph *g,
                                           cflow_subgraph_id id,
                                           unsigned char *state,
                                           cmeta_effects *memo) {
    if (!g || id >= g->subgraph_count) return CMETA_EFFECT_UNKNOWN;
    if (state[id] == 2u) return memo[id];
    if (state[id] == 1u) return CMETA_EFFECT_UNKNOWN;
    state[id] = 1u;

    cmeta_effects effects = CMETA_EFFECT_PURE;
    const cflow_subgraph *sg = &g->subgraphs[id];
    for (size_t n = 0; n < sg->node_count; ++n) {
        const cflow_node *node = &sg->nodes[n];
        if (node->op != CFLOW_OP_SOURCE && node->op != CFLOW_OP_RELATION) {
            const cflow_op_schema *schema = cflow_op_schema_get(node->op);
            effects |= schema ? schema->intrinsic_effects : CMETA_EFFECT_UNKNOWN;
        }
        if (node->fn_chain_count) {
            if (!node->fn_chain) effects |= CMETA_EFFECT_UNKNOWN;
            else for (size_t i = 0; i < node->fn_chain_count; ++i)
                effects |= fn_effects(node->fn_chain[i]);
        } else if (node->has_fn) {
            effects |= fn_effects(node->fn);
        }
        for (size_t i = 0; i < node->subgraph_count; ++i)
            effects |= subgraph_effects_rec(g, node->subgraphs[i], state, memo);
    }

    state[id] = 2u;
    memo[id] = effects;
    return effects;
}

cmeta_effects cflow_node_effects(const cflow_graph *g, const cflow_node *node) {
    if (!g || !node) return CMETA_EFFECT_UNKNOWN;
    cmeta_effects effects = CMETA_EFFECT_PURE;
    if (node->op != CFLOW_OP_SOURCE && node->op != CFLOW_OP_RELATION) {
        const cflow_op_schema *schema = cflow_op_schema_get(node->op);
        effects |= schema ? schema->intrinsic_effects : CMETA_EFFECT_UNKNOWN;
    }
    if (node->fn_chain_count) {
        if (!node->fn_chain) effects |= CMETA_EFFECT_UNKNOWN;
        else for (size_t i = 0; i < node->fn_chain_count; ++i)
            effects |= fn_effects(node->fn_chain[i]);
    } else if (node->has_fn) {
        effects |= fn_effects(node->fn);
    }

    if (!node->subgraph_count) return effects;
    unsigned char *state = calloc(g->subgraph_count ? g->subgraph_count : 1u, 1u);
    cmeta_effects *memo = calloc(g->subgraph_count ? g->subgraph_count : 1u, sizeof(*memo));
    if (!state || !memo) {
        free(state); free(memo);
        return effects | CMETA_EFFECT_UNKNOWN;
    }
    for (size_t i = 0; i < node->subgraph_count; ++i)
        effects |= subgraph_effects_rec(g, node->subgraphs[i], state, memo);
    free(state); free(memo);
    return effects;
}

cmeta_effects cflow_subgraph_effects(const cflow_graph *g, cflow_subgraph_id subgraph) {
    if (!g || subgraph >= g->subgraph_count) return CMETA_EFFECT_UNKNOWN;
    unsigned char *state = calloc(g->subgraph_count ? g->subgraph_count : 1u, 1u);
    cmeta_effects *memo = calloc(g->subgraph_count ? g->subgraph_count : 1u, sizeof(*memo));
    if (!state || !memo) {
        free(state); free(memo);
        return CMETA_EFFECT_UNKNOWN;
    }
    cmeta_effects effects = subgraph_effects_rec(g, subgraph, state, memo);
    free(state); free(memo);
    return effects;
}

cmeta_effects cflow_graph_effects(const cflow_graph *g) {
    return g && g->root < g->subgraph_count
        ? cflow_subgraph_effects(g, g->root)
        : CMETA_EFFECT_UNKNOWN;
}
