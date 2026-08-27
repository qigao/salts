#include <cflow/property.h>

#include <stdlib.h>

#define COMPOSABLE (CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS)

bool cmeta_callable_has_properties(cmeta_callable fn, cmeta_properties required) {
    return cmeta_callable_contract_valid(fn) && cmeta_properties_include(fn.meta.properties, required);
}

bool cflow_callable_declares_idempotent_endomap(cmeta_callable fn) {
    const cmeta_sig_desc *sig = cmeta_callable_signature(fn);
    return cmeta_callable_contract_valid(fn) &&
        cmeta_effects_are_pure(fn.meta.effects) &&
        cmeta_properties_include(fn.meta.properties,
            CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_IDEMPOTENT) &&
        sig && sig->protocol == CMETA_FN_PROTOCOL_VALUE && sig->param_count == 1u &&
        cmeta_type_equal(sig->params[0], sig->return_type);
}

bool cflow_callable_declares_associative_endomap(cmeta_callable fn) {
    const cmeta_properties required = CMETA_PROP_TOTAL |
        CMETA_PROP_ASSOCIATIVE | CMETA_PROP_NO_ALIAS;
    cmeta_callable bound;
    const cmeta_sig_desc *sig;

    if (!cmeta_callable_bind(fn, &bound) ||
        !cmeta_callable_contract_valid(bound) ||
        !cmeta_effects_are_pure(bound.meta.effects) ||
        !cmeta_properties_include(bound.meta.properties, required))
        return false;
    sig = cmeta_fn_signature(bound.meta);
    return sig && sig->protocol == CMETA_FN_PROTOCOL_VALUE &&
        sig->param_count == 2u && sig->params[0] && sig->params[1] &&
        sig->return_type && cmeta_type_equal(sig->params[0], sig->params[1]) &&
        cmeta_type_equal(sig->params[0], sig->return_type);
}

static cmeta_properties fn_props(cmeta_callable fn) {
    return cmeta_callable_contract_valid(fn) ? fn.meta.properties : CMETA_PROP_NONE;
}

static cmeta_properties subgraph_props_rec(const cflow_graph *g,
                                            cflow_subgraph_id id,
                                            unsigned char *state,
                                            cmeta_properties *memo);

static cmeta_properties node_props_rec(const cflow_graph *g,
                                        const cflow_node *node,
                                        unsigned char *state,
                                        cmeta_properties *memo) {
    if (!g || !node) return CMETA_PROP_NONE;
    if (node->op == CFLOW_OP_SOURCE)
        return CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_IDEMPOTENT;

    if (node->op == CFLOW_OP_RELATION) {
        cmeta_properties p = COMPOSABLE;
        for (size_t i = 0; i < node->subgraph_count; ++i)
            p &= subgraph_props_rec(g, node->subgraphs[i], state, memo) & COMPOSABLE;
        /* ANY/LATEST with multiple children can expose scheduler-dependent
         * winner/update order even when every child is deterministic. */
        if (node->subgraph_count > 1u &&
            (node->relation.coordination == CFLOW_REL_COORD_ANY ||
             node->relation.coordination == CFLOW_REL_COORD_LATEST))
            p &= ~CMETA_PROP_DETERMINISTIC;
        if (node->has_fn)
            p &= fn_props(node->fn) & COMPOSABLE;
        return p;
    }

    if (node->op == CFLOW_OP_TAKE || node->op == CFLOW_OP_SKIP ||
        node->op == CFLOW_OP_DISTINCT || node->op == CFLOW_OP_SORTED)
        return COMPOSABLE;

    cmeta_properties p = COMPOSABLE;
    if (node->fn_chain_count) {
        if (!node->fn_chain) return CMETA_PROP_NONE;
        for (size_t i = 0; i < node->fn_chain_count; ++i)
            p &= fn_props(node->fn_chain[i]) & COMPOSABLE;
        if (node->fn_chain_count == 1u &&
            cmeta_type_equal(node->input_type, node->output_type) &&
            cmeta_callable_has_properties(node->fn_chain[0], CMETA_PROP_IDEMPOTENT))
            p |= CMETA_PROP_IDEMPOTENT;
        return p;
    }

    if (!node->has_fn) return CMETA_PROP_NONE;
    p &= fn_props(node->fn) & COMPOSABLE;
    if ((node->op == CFLOW_OP_MAP || node->op == CFLOW_OP_TRANSFORM) &&
        cmeta_type_equal(node->input_type, node->output_type) &&
        cmeta_callable_has_properties(node->fn, CMETA_PROP_IDEMPOTENT))
        p |= CMETA_PROP_IDEMPOTENT;
    return p;
}

static cmeta_properties subgraph_props_rec(const cflow_graph *g,
                                            cflow_subgraph_id id,
                                            unsigned char *state,
                                            cmeta_properties *memo) {
    if (!g || id >= g->subgraph_count) return CMETA_PROP_NONE;
    if (state[id] == 2u) return memo[id];
    if (state[id] == 1u) return CMETA_PROP_NONE;
    state[id] = 1u;

    const cflow_subgraph *sg = &g->subgraphs[id];
    cmeta_properties p = COMPOSABLE;
    size_t semantic_nodes = 0u;
    cmeta_properties sole = CMETA_PROP_NONE;
    for (size_t i = 0; i < sg->node_count; ++i) {
        const cflow_node *n = &sg->nodes[i];
        if (n->op == CFLOW_OP_SOURCE) continue;
        cmeta_properties np = node_props_rec(g, n, state, memo);
        p &= np & COMPOSABLE;
        ++semantic_nodes;
        sole = np;
    }
    if (semantic_nodes == 0u && cmeta_type_equal(sg->input_type, sg->output_type))
        p |= CMETA_PROP_IDEMPOTENT;
    else if (semantic_nodes == 1u && cmeta_type_equal(sg->input_type, sg->output_type) &&
             (sole & CMETA_PROP_IDEMPOTENT))
        p |= CMETA_PROP_IDEMPOTENT;

    state[id] = 2u;
    memo[id] = p;
    return p;
}

static cmeta_properties analyze_subgraph(const cflow_graph *g, cflow_subgraph_id id) {
    if (!g || id >= g->subgraph_count) return CMETA_PROP_NONE;
    unsigned char *state = calloc(g->subgraph_count ? g->subgraph_count : 1u, 1u);
    cmeta_properties *memo = calloc(g->subgraph_count ? g->subgraph_count : 1u, sizeof(*memo));
    if (!state || !memo) { free(state); free(memo); return CMETA_PROP_NONE; }
    cmeta_properties p = subgraph_props_rec(g, id, state, memo);
    free(state); free(memo);
    return p;
}

cmeta_properties cflow_node_properties(const cflow_graph *g, const cflow_node *node) {
    if (!g || !node) return CMETA_PROP_NONE;
    unsigned char *state = calloc(g->subgraph_count ? g->subgraph_count : 1u, 1u);
    cmeta_properties *memo = calloc(g->subgraph_count ? g->subgraph_count : 1u, sizeof(*memo));
    if (!state || !memo) { free(state); free(memo); return CMETA_PROP_NONE; }
    cmeta_properties p = node_props_rec(g, node, state, memo);
    free(state); free(memo);
    return p;
}

cmeta_properties cflow_subgraph_properties(const cflow_graph *g, cflow_subgraph_id subgraph) {
    return analyze_subgraph(g, subgraph);
}

cmeta_properties cflow_graph_properties(const cflow_graph *g) {
    return g ? analyze_subgraph(g, g->root) : CMETA_PROP_NONE;
}

#undef COMPOSABLE
