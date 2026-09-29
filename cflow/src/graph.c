#include <cflow/operators.h>
#include <cflow/graph.h>
#include "dense_successor_index.h"
#include "graph_internal.h"
#include "value_storage.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static _Atomic uint64_t cflow_graph_next_version = UINT64_C(1);

bool cflow_graph_version_acquire(uint64_t *version) {
    uint64_t next;

    if (!version) return false;
    next = atomic_load_explicit(&cflow_graph_next_version, memory_order_relaxed);
    while (next != UINT64_MAX) {
        if (atomic_compare_exchange_weak_explicit(
                &cflow_graph_next_version, &next, next + UINT64_C(1),
                memory_order_relaxed, memory_order_relaxed)) {
            *version = next;
            return true;
        }
    }
    return false;
}

#define P_RULE(x) CFLOW_PARAM_##x
#define R_RULE(x) CFLOW_RETURN_##x
#define O_RULE(x) CFLOW_OUTPUT_##x
#define C_RULE(x) CMETA_CARD_##x
#define SUBGRAPH_RULE_NONE CFLOW_SUBGRAPH_NONE
#define SUBGRAPH_RULE_SUBGRAPH_1TO1 CFLOW_SUBGRAPH_1TO1
#define SUBGRAPH_RULE(x) SUBGRAPH_RULE_##x
#define CMETA_STR_I(x) #x
#define CMETA_STR(x) CMETA_STR_I(x)

static const cflow_op_schema schemas[CFLOW_OP_COUNT] = {
#define CFLOW_OP_ROW(E, method_, margc, fnarg, subgrapharg, farity, p0, p1, p2, ret_, out_, card_, subgraphrule_, semantic_, intrinsic_effects_) \
    [CFLOW_OP_##E] = { CFLOW_OP_##E, #method_, (margc), (fnarg), (subgrapharg), (farity), \
        { P_RULE(p0), P_RULE(p1), P_RULE(p2) }, R_RULE(ret_), O_RULE(out_), C_RULE(card_), \
        SUBGRAPH_RULE(subgraphrule_), CMETA_STR(semantic_), (cmeta_effects)(intrinsic_effects_) },
Replay(CFlowOperators, CFLOW_OP_ROW)
#undef CFLOW_OP_ROW
    [CFLOW_OP_TAKE] = { CFLOW_OP_TAKE, "take", 1u, -1, -1, 0u,
        { CFLOW_PARAM_NONE, CFLOW_PARAM_NONE, CFLOW_PARAM_NONE },
        CFLOW_RETURN_INPUT, CFLOW_OUTPUT_SAME, CMETA_CARD_FILTER,
        CFLOW_SUBGRAPH_NONE, "take", CMETA_EFFECT_STATEFUL },
    [CFLOW_OP_SKIP] = { CFLOW_OP_SKIP, "skip", 1u, -1, -1, 0u,
        { CFLOW_PARAM_NONE, CFLOW_PARAM_NONE, CFLOW_PARAM_NONE },
        CFLOW_RETURN_INPUT, CFLOW_OUTPUT_SAME, CMETA_CARD_FILTER,
        CFLOW_SUBGRAPH_NONE, "skip", CMETA_EFFECT_STATEFUL },
    [CFLOW_OP_DISTINCT] = { CFLOW_OP_DISTINCT, "distinct", 1u, -1, -1, 0u,
        { CFLOW_PARAM_NONE, CFLOW_PARAM_NONE, CFLOW_PARAM_NONE },
        CFLOW_RETURN_INPUT, CFLOW_OUTPUT_SAME, CMETA_CARD_FILTER,
        CFLOW_SUBGRAPH_NONE, "distinct", CMETA_EFFECT_STATEFUL },
    [CFLOW_OP_SORTED] = { CFLOW_OP_SORTED, "sorted", 1u, -1, -1, 0u,
        { CFLOW_PARAM_NONE, CFLOW_PARAM_NONE, CFLOW_PARAM_NONE },
        CFLOW_RETURN_INPUT, CFLOW_OUTPUT_SAME, CMETA_CARD_FILTER,
        CFLOW_SUBGRAPH_NONE, "sorted", CMETA_EFFECT_STATEFUL },
};

#undef P_RULE
#undef R_RULE
#undef O_RULE
#undef C_RULE
#undef SUBGRAPH_RULE
#undef SUBGRAPH_RULE_SUBGRAPH_1TO1
#undef SUBGRAPH_RULE_NONE
#undef CMETA_STR
#undef CMETA_STR_I

const cflow_op_schema *cflow_op_schema_get(cflow_op op) {
    if (op == CFLOW_OP_INPUT || op == CFLOW_OP_RELATION || op >= CFLOW_OP_COUNT) return NULL;
    return &schemas[op];
}

const char *cflow_op_name(cflow_op op) {
    if (op == CFLOW_OP_INPUT) return "input";
    if (op == CFLOW_OP_RELATION) return "relation";
    const cflow_op_schema *s = cflow_op_schema_get(op);
    return s ? s->method : "?";
}


#include <cflow/operator_policy.h>
#define CMETA_ALLOWED_CASE(id, ignored) case CMETA_SIG_NAME(id): return true;

bool cflow_op_signature_allowed(cflow_op op, cmeta_sig sig) {
    switch (op) {
#define CFLOW_OP_ROW(E, method, margc, fnarg, subgrapharg, farity, p0, p1, p2, ret, out, card, subgraphrule, semantic, intrinsic_effects) \
        case CFLOW_OP_##E: \
            switch (sig) { \
                CMETA_PP_FOR_EACH_A(CMETA_ALLOWED_CASE, ~, CFLOW_OP_SIGNATURE_LIST(method)) \
                default: return false; \
            }
Replay(CFlowOperators, CFLOW_OP_ROW)
#undef CFLOW_OP_ROW
        case CFLOW_OP_INPUT:
        case CFLOW_OP_RELATION:
        case CFLOW_OP_TAKE:
        case CFLOW_OP_SKIP:
        case CFLOW_OP_DISTINCT:
        case CFLOW_OP_SORTED:
        case CFLOW_OP_COUNT:
            return false;
    }
    return false;
}
#undef CMETA_ALLOWED_CASE

static bool fail(cflow_graph *g, const char *message) {
    if (g) g->error = message;
    return false;
}

bool cflow_graph_explicit_adapter_callable_valid(cmeta_callable fn) {
    /*
     * CMETA_SIG_INVALID means no member of the canonical raw target union has
     * semantic identity. Requiring ADAPTER dispatch + no resolver makes this
     * path unambiguously erased-invoke only without depending on function
     * pointer object representations.
     */
    return fn.dispatch == CMETA_CALLABLE_DISPATCH_ADAPTER &&
           fn.meta.sig == CMETA_SIG_INVALID &&
           fn.resolve == NULL &&
           fn.invoke != NULL &&
           fn.generate == NULL &&
           fn.capture_size <= CMETA_CAPTURE_INLINE &&
           cmeta_effect_property_contract_valid(
               fn.meta.effects, fn.meta.properties);
}

static bool reserve_subgraphs(cflow_graph *g, size_t need) {
    if (need <= g->subgraph_capacity) return true;
    size_t cap = g->subgraph_capacity ? g->subgraph_capacity * 2u : 4u;
    while (cap < need) cap *= 2u;
    cflow_subgraph *p = realloc(g->subgraphs, cap * sizeof(*p));
    if (!p) return false;
    memset(p + g->subgraph_capacity, 0, (cap - g->subgraph_capacity) * sizeof(*p));
    g->subgraphs = p;
    g->subgraph_capacity = cap;
    return true;
}

static bool reserve_nodes(cflow_subgraph *sg, size_t need) {
    if (need <= sg->node_capacity) return true;
    size_t cap = sg->node_capacity ? sg->node_capacity * 2u : 8u;
    while (cap < need) cap *= 2u;
    cflow_node *p = realloc(sg->nodes, cap * sizeof(*p));
    if (!p) return false;
    memset(p + sg->node_capacity, 0, (cap - sg->node_capacity) * sizeof(*p));
    sg->nodes = p;
    sg->node_capacity = cap;
    return true;
}

static bool reserve_edges(cflow_subgraph *sg, size_t need) {
    if (need <= sg->edge_capacity) return true;
    size_t cap = sg->edge_capacity ? sg->edge_capacity * 2u : 8u;
    while (cap < need) cap *= 2u;
    cflow_edge *p = realloc(sg->edges, cap * sizeof(*p));
    if (!p) return false;
    sg->edges = p;
    sg->edge_capacity = cap;
    return true;
}

static cflow_value_slot *node_reduce_seed_slot(const cflow_node *node) {
    if (!node || node->op != CFLOW_OP_REDUCE ||
        node->param_kind != CFLOW_NODE_PARAM_REDUCE_SEED ||
        !node->params.reduce_seed.owner)
        return NULL;
    return (cflow_value_slot *)node->params.reduce_seed.owner;
}

static bool node_set_reduce_seed(cflow_node *node, const void *seed) {
    cflow_value_slot *slot;
    if (!node || node->op != CFLOW_OP_REDUCE || !seed ||
        node->param_kind != CFLOW_NODE_PARAM_NONE ||
        !cmeta_type_equal(node->input_type, node->output_type) ||
        cmeta_type_require_traits(node->input_type, CMETA_TRAIT_EQUAL) != CMETA_OK ||
        !cflow_value_type_supported(node->input_type))
        return false;
    slot = (cflow_value_slot *)calloc(1u, sizeof(*slot));
    if (!slot) return false;
    if (!cflow_value_slot_init(slot, node->input_type) ||
        !cflow_value_slot_copy(slot, seed)) {
        cflow_value_slot_destroy(slot);
        free(slot);
        return false;
    }
    node->param_kind = CFLOW_NODE_PARAM_REDUCE_SEED;
    node->params.reduce_seed.owner = slot;
    return true;
}

static void node_destroy(cflow_node *n) {
    cflow_value_slot *seed_slot;
    if (!n) return;
    seed_slot = node_reduce_seed_slot(n);
    if (seed_slot) {
        cflow_value_slot_destroy(seed_slot);
        free(seed_slot);
    }
    free(n->subgraphs);
    free(n->fn_chain);
    memset(n, 0, sizeof(*n));
}

static void subgraph_destroy(cflow_subgraph *sg) {
    if (!sg) return;
    for (size_t i = 0; i < sg->node_count; ++i) node_destroy(&sg->nodes[i]);
    free(sg->nodes);
    free(sg->edges);
    memset(sg, 0, sizeof(*sg));
    sg->entry = CMETA_INVALID_ID;
    sg->tail = CMETA_INVALID_ID;
}

static void truncate_subgraphs(cflow_graph *g, size_t keep) {
    if (!g) return;
    while (g->subgraph_count > keep) {
        --g->subgraph_count;
        subgraph_destroy(&g->subgraphs[g->subgraph_count]);
    }
}

static cflow_subgraph_id graph_create_subgraph(cflow_graph *g,
                                                const cmeta_type_desc *input_type) {
    if (!g || !input_type || g->subgraph_count >= CMETA_INVALID_ID) return CMETA_INVALID_ID;
    if (!reserve_subgraphs(g, g->subgraph_count + 1u)) return CMETA_INVALID_ID;
    cflow_subgraph_id id = (cflow_subgraph_id)g->subgraph_count++;
    cflow_subgraph *sg = &g->subgraphs[id];
    memset(sg, 0, sizeof(*sg));
    sg->entry = CMETA_INVALID_ID;
    sg->tail = CMETA_INVALID_ID;
    sg->input_type = input_type;
    sg->output_type = input_type;
    return id;
}

static cflow_node_id subgraph_append_node(cflow_subgraph *sg, cflow_node node) {
    if (!sg || sg->node_count >= CMETA_INVALID_ID || !reserve_nodes(sg, sg->node_count + 1u))
        return CMETA_INVALID_ID;
    cflow_node_id id = (cflow_node_id)sg->node_count;
    sg->nodes[sg->node_count++] = node;
    if (sg->entry == CMETA_INVALID_ID) sg->entry = id;
    sg->tail = id;
    sg->output_type = node.output_type;
    return id;
}

static bool subgraph_add_edge(cflow_subgraph *sg, cflow_node_id from, cflow_node_id to) {
    if (!sg || from >= sg->node_count || to >= sg->node_count) return false;
    if (!reserve_edges(sg, sg->edge_count + 1u)) return false;
    sg->edges[sg->edge_count++] = (cflow_edge){ from, 0u, to, 0u };
    return true;
}

const cflow_subgraph *cflow_graph_subgraph(const cflow_graph *g, cflow_subgraph_id id) {
    return g && id < g->subgraph_count ? &g->subgraphs[id] : NULL;
}

const cflow_node *cflow_subgraph_node(const cflow_subgraph *sg, cflow_node_id id) {
    return sg && id < sg->node_count ? &sg->nodes[id] : NULL;
}

const cflow_edge *cflow_subgraph_edge(const cflow_subgraph *sg, cflow_edge_id id) {
    return sg && id < sg->edge_count ? &sg->edges[id] : NULL;
}

size_t cflow_subgraph_out_degree(const cflow_subgraph *sg, cflow_node_id node) {
    if (!sg || node >= sg->node_count) return 0;
    size_t n = 0;
    for (size_t i = 0; i < sg->edge_count; ++i)
        if (sg->edges[i].from == node) ++n;
    return n;
}

bool cflow_subgraph_single_successor(const cflow_subgraph *sg,
                                     cflow_node_id node,
                                     cflow_node_id *successor) {
    if (!sg || node >= sg->node_count || !successor) return false;
    size_t n = 0;
    cflow_node_id next = CMETA_INVALID_ID;
    for (size_t i = 0; i < sg->edge_count; ++i) {
        if (sg->edges[i].from != node) continue;
        ++n;
        next = sg->edges[i].to;
    }
    if (n != 1u) return false;
    *successor = next;
    return true;
}

static bool clone_node(cflow_node *dst, const cflow_node *src) {
    *dst = *src;
    dst->subgraphs = NULL;
    dst->fn_chain = NULL;
    if (src->param_kind == CFLOW_NODE_PARAM_REDUCE_SEED) {
        const cflow_value_slot *src_seed = node_reduce_seed_slot(src);
        const void *seed = src_seed ? src_seed->storage : NULL;
        memset(&dst->params, 0, sizeof(dst->params));
        dst->param_kind = CFLOW_NODE_PARAM_NONE;
        if (!seed || !node_set_reduce_seed(dst, seed)) return false;
    }
    if (src->fn_chain_count) {
        dst->fn_chain = malloc(src->fn_chain_count * sizeof(*dst->fn_chain));
        if (!dst->fn_chain) return false;
        memcpy(dst->fn_chain, src->fn_chain, src->fn_chain_count * sizeof(*dst->fn_chain));
    }
    if (!src->subgraph_count) return true;
    dst->subgraphs = malloc(src->subgraph_count * sizeof(*dst->subgraphs));
    if (!dst->subgraphs) { free(dst->fn_chain); dst->fn_chain = NULL; return false; }
    memcpy(dst->subgraphs, src->subgraphs, src->subgraph_count * sizeof(*dst->subgraphs));
    return true;
}

static bool clone_subgraph(cflow_subgraph *dst, const cflow_subgraph *src) {
    memset(dst, 0, sizeof(*dst));
    dst->entry = src->entry;
    dst->tail = src->tail;
    dst->input_type = src->input_type;
    dst->output_type = src->output_type;
    if (src->node_count) {
        dst->nodes = calloc(src->node_count, sizeof(*dst->nodes));
        if (!dst->nodes) return false;
        dst->node_capacity = src->node_count;
        for (size_t i = 0; i < src->node_count; ++i) {
            if (!clone_node(&dst->nodes[i], &src->nodes[i])) {
                dst->node_count = i + 1u;
                subgraph_destroy(dst);
                return false;
            }
            ++dst->node_count;
        }
    }
    if (src->edge_count) {
        dst->edges = malloc(src->edge_count * sizeof(*dst->edges));
        if (!dst->edges) { subgraph_destroy(dst); return false; }
        memcpy(dst->edges, src->edges, src->edge_count * sizeof(*dst->edges));
        dst->edge_count = src->edge_count;
        dst->edge_capacity = src->edge_count;
    }
    return true;
}

void cflow_graph_destroy(cflow_graph *g) {
    if (!g) return;
    for (size_t i = 0; i < g->subgraph_count; ++i) subgraph_destroy(&g->subgraphs[i]);
    free(g->subgraphs);
    memset(g, 0, sizeof(*g));
    g->root = CMETA_INVALID_ID;
}

bool cflow_graph_clone(cflow_graph *dst, const cflow_graph *src) {
    uint64_t version;
    if (!dst || !src || src->root >= src->subgraph_count) return false;
    if (!cflow_graph_version_acquire(&version)) return false;
    memset(dst, 0, sizeof(*dst));
    dst->root = CMETA_INVALID_ID;
    if (!reserve_subgraphs(dst, src->subgraph_count)) return false;
    for (size_t i = 0; i < src->subgraph_count; ++i) {
        if (!clone_subgraph(&dst->subgraphs[i], &src->subgraphs[i])) {
            dst->subgraph_count = i + 1u;
            cflow_graph_destroy(dst);
            return false;
        }
        ++dst->subgraph_count;
    }
    dst->root = src->root;
    dst->version = version;
    dst->error = src->error;
    return true;
}

static bool import_graph(cflow_graph *dst,
                         const cflow_graph *src,
                         cflow_subgraph_id *imported_root) {
    if (!dst || !src || src->root >= src->subgraph_count || !imported_root) return false;
    size_t base = dst->subgraph_count;
    if (base + src->subgraph_count >= CMETA_INVALID_ID ||
        !reserve_subgraphs(dst, base + src->subgraph_count)) return false;

    size_t added = 0;
    for (size_t i = 0; i < src->subgraph_count; ++i) {
        cflow_subgraph *target = &dst->subgraphs[base + i];
        if (!clone_subgraph(target, &src->subgraphs[i])) goto rollback;
        ++added;
        for (size_t n = 0; n < target->node_count; ++n) {
            for (size_t k = 0; k < target->nodes[n].subgraph_count; ++k) {
                cflow_subgraph_id old = target->nodes[n].subgraphs[k];
                if (old >= src->subgraph_count) goto rollback;
                target->nodes[n].subgraphs[k] = (cflow_subgraph_id)(base + old);
            }
        }
    }
    dst->subgraph_count = base + added;
    *imported_root = (cflow_subgraph_id)(base + src->root);
    return true;

rollback:
    for (size_t j = 0; j < added; ++j) subgraph_destroy(&dst->subgraphs[base + j]);
    return false;
}

void cflow_graph_init(cflow_graph *g, const cmeta_type_desc *source) {
    uint64_t version;
    if (!g) return;
    memset(g, 0, sizeof(*g));
    g->root = CMETA_INVALID_ID;
    if (!source) { g->error = "source type is null"; return; }
    if (!cflow_graph_version_acquire(&version)) {
        g->error = "graph version space exhausted";
        return;
    }
    cflow_subgraph_id root = graph_create_subgraph(g, source);
    if (root == CMETA_INVALID_ID) { g->error = "graph allocation failed"; return; }
    cflow_node input_node = {0};
    input_node.op = CFLOW_OP_INPUT;
    input_node.input_type = source;
    input_node.output_type = source;
    if (subgraph_append_node(&g->subgraphs[root], input_node) == CMETA_INVALID_ID) {
        cflow_graph_destroy(g);
        g->error = "source node allocation failed";
        return;
    }
    g->root = root;
    g->version = version;
}

const cmeta_type_desc *cflow_subgraph_input_type(const cflow_graph *g, cflow_subgraph_id id) {
    const cflow_subgraph *sg = cflow_graph_subgraph(g, id);
    return sg ? sg->input_type : NULL;
}

const cmeta_type_desc *cflow_subgraph_output_type(const cflow_graph *g, cflow_subgraph_id id) {
    const cflow_subgraph *sg = cflow_graph_subgraph(g, id);
    return sg ? sg->output_type : NULL;
}

bool cflow_subgraph_is_one_to_one(const cflow_graph *g, cflow_subgraph_id id) {
    const cflow_subgraph *sg = cflow_graph_subgraph(g, id);
    if (!sg || !sg->node_count) return false;
    for (size_t i = 0; i < sg->node_count; ++i) {
        const cflow_node *node = &sg->nodes[i];
        if (node->op == CFLOW_OP_INPUT) continue;
        if (node->op == CFLOW_OP_RELATION) {
            if (!node->has_relation) return false;
            if (node->relation.completion == CFLOW_REL_COMPLETE_FIRST_RESULT) continue;
            if (node->relation.completion == CFLOW_REL_COMPLETE_ALL_DONE) continue;
            if (node->relation.coordination == CFLOW_REL_COORD_ALL ||
                node->relation.coordination == CFLOW_REL_COORD_ANY) continue;
            return false;
        }
        const cflow_op_schema *schema = cflow_op_schema_get(node->op);
        if (!schema || schema->cardinality != CMETA_CARD_ONE) return false;
    }
    return true;
}

const cmeta_type_desc *cflow_graph_input_type(const cflow_graph *g) {
    return g ? cflow_subgraph_input_type(g, g->root) : NULL;
}

const cmeta_type_desc *cflow_graph_output_type(const cflow_graph *g) {
    return g ? cflow_subgraph_output_type(g, g->root) : NULL;
}

bool cflow_graph_is_one_to_one(const cflow_graph *g) {
    return g && cflow_subgraph_is_one_to_one(g, g->root);
}

static bool check_param_rule(cflow_graph *g,
                             cflow_param_rule rule,
                             const cmeta_type_desc *actual,
                             const cmeta_type_desc *input,
                             const cmeta_type_desc *child) {
    switch (rule) {
        case CFLOW_PARAM_NONE: return true;
        case CFLOW_PARAM_INPUT:
            return cmeta_type_equal(actual, input) || fail(g, "operator input type mismatch");
        case CFLOW_PARAM_SUBGRAPH:
            return (child && cmeta_type_equal(actual, child)) || fail(g, "operator child type mismatch");
        case CFLOW_PARAM_OUT_PTR:
            return (actual && actual->kind == CMETA_T_POINTER && actual->pointee &&
                    actual->pointee->kind != CMETA_T_VOID) ||
                   fail(g, "operator requires a non-void output pointer");
        case CFLOW_PARAM_CURSOR:
            return cmeta_type_equal(actual, &cmeta_type_size_ptr) ||
                   fail(g, "operator requires size_t * cursor");
    }
    return fail(g, "unknown parameter rule");
}

static bool check_return_rule(cflow_graph *g,
                              cflow_return_rule rule,
                              const cmeta_type_desc *ret,
                              const cmeta_type_desc *input) {
    switch (rule) {
        case CFLOW_RETURN_BOOL:
            return cmeta_type_equal(ret, &cmeta_type_bool) || fail(g, "operator must return bool/_Bool");
        case CFLOW_RETURN_VALUE:
            return (ret && ret->kind != CMETA_T_VOID) || fail(g, "operator cannot return void");
        case CFLOW_RETURN_INPUT:
            return cmeta_type_equal(ret, input) || fail(g, "operator return type must equal input type");
        case CFLOW_RETURN_GENERATOR:
            return cmeta_type_equal(ret, &cmeta_type_gen_status) || fail(g, "flatMap must return cmeta_gen_status");
    }
    return fail(g, "unknown return rule");
}


static bool relation_schema_valid(cflow_graph *g, cflow_relation_schema schema);

cflow_subgraph_id cflow_graph_create_subgraph(cflow_graph *g,
                                               const cmeta_type_desc *input_type) {
    uint64_t version;
    if (!g || !input_type) return CMETA_INVALID_ID;
    if (!cflow_graph_version_acquire(&version)) {
        g->error = "graph version space exhausted";
        return CMETA_INVALID_ID;
    }
    cflow_subgraph_id id = graph_create_subgraph(g, input_type);
    if (id == CMETA_INVALID_ID) { g->error = "subgraph allocation failed"; return id; }
    cflow_node input_node = {0};
    input_node.op = CFLOW_OP_INPUT;
    input_node.input_type = input_type;
    input_node.output_type = input_type;
    if (subgraph_append_node(&g->subgraphs[id], input_node) == CMETA_INVALID_ID) {
        subgraph_destroy(&g->subgraphs[id]);
        --g->subgraph_count;
        g->error = "subgraph source allocation failed";
        return CMETA_INVALID_ID;
    }
    g->version = version;
    g->error = NULL;
    return id;
}

bool cflow_graph_create_explicit_typed_adapter_node(
    cflow_graph *g,
    cflow_subgraph_id subgraph,
    cflow_op op,
    cmeta_callable fn,
    const cmeta_type_desc *input_type,
    const cmeta_type_desc *output_type,
    cflow_node_id *out_node) {
    cflow_subgraph *sg =
        g && subgraph < g->subgraph_count ? &g->subgraphs[subgraph] : NULL;
    cflow_node node = {0};
    uint64_t version;

    if (!sg || !out_node ||
        (op != CFLOW_OP_MAP && op != CFLOW_OP_FILTER) ||
        !cmeta_type_desc_valid(input_type) || input_type->size == 0u ||
        !cmeta_type_desc_valid(output_type) || output_type->size == 0u ||
        (op == CFLOW_OP_FILTER &&
         !cmeta_type_equal(input_type, output_type)) ||
        !cflow_graph_explicit_adapter_callable_valid(fn))
        return fail(g, "invalid explicit typed adapter node");

    if (!cflow_graph_version_acquire(&version))
        return fail(g, "graph version space exhausted");

    node.op = op;
    node.fn = fn;
    node.has_fn = true;
    node.input_type = input_type;
    node.output_type = output_type;
    node.param_kind = CFLOW_NODE_PARAM_TYPED_ADAPTER;

    cflow_node_id id = subgraph_append_node(sg, node);
    if (id == CMETA_INVALID_ID)
        return fail(g, "explicit typed adapter node allocation failed");

    g->version = version;
    g->error = NULL;
    *out_node = id;
    return true;
}

bool cflow_graph_create_explicit_map_adapter_node(
    cflow_graph *g,
    cflow_subgraph_id subgraph,
    cmeta_callable fn,
    const cmeta_type_desc *input_type,
    const cmeta_type_desc *output_type,
    cflow_node_id *out_node) {
    return cflow_graph_create_explicit_typed_adapter_node(
        g, subgraph, CFLOW_OP_MAP, fn,
        input_type, output_type, out_node);
}

static bool derive_node(cflow_graph *g,
                        cflow_op op,
                        cmeta_callable fn,
                        const cflow_subgraph_id *nested,
                        size_t nested_count,
                        cflow_node *out_node) {
    const cflow_op_schema *schema = cflow_op_schema_get(op);
    cmeta_callable bound;
    if (!schema) return fail(g, "unsupported typed graph operator");
    if (!cmeta_callable_bind(fn, &bound))
        return fail(g, "callable effect/property contract is invalid");
    const cmeta_sig_desc *fsig = cmeta_callable_signature(bound);
    if (!fsig || fsig->param_count != schema->fn_arity)
        return fail(g, "operator function arity mismatch");
    if (!cflow_op_signature_allowed(op, bound.meta.sig))
        return fail(g, "signature is not enabled for this operator");

    const cmeta_type_desc *in = fsig->params[0];
    const cmeta_type_desc *subgraph_out = NULL;
    switch (schema->subgraph_rule) {
        case CFLOW_SUBGRAPH_NONE:
            if (nested_count != 0) return fail(g, "operator does not accept nested subgraphs");
            break;
        case CFLOW_SUBGRAPH_1TO1:
            if (nested_count != 1 || !nested || nested[0] >= g->subgraph_count)
                return fail(g, "operator requires one nested subgraph");
            if (!cflow_subgraph_is_one_to_one(g, nested[0]))
                return fail(g, "operator requires a 1:1 nested subgraph");
            subgraph_out = cflow_subgraph_output_type(g, nested[0]);
            break;
    }

    for (size_t i = 0; i < schema->fn_arity; ++i)
        if (!check_param_rule(g, schema->params[i], fsig->params[i], in, subgraph_out)) return false;
    if (!check_return_rule(g, schema->ret, fsig->return_type, in)) return false;

    const cmeta_type_desc *out = NULL;
    switch (schema->output) {
        case CFLOW_OUTPUT_SAME: out = in; break;
        case CFLOW_OUTPUT_RETURN: out = fsig->return_type; break;
        case CFLOW_OUTPUT_POINTEE1:
            if (fsig->param_count < 2 || !fsig->params[1] ||
                fsig->params[1]->kind != CMETA_T_POINTER || !fsig->params[1]->pointee)
                return fail(g, "operator cannot infer output pointee type");
            if (fsig->protocol != CMETA_FN_PROTOCOL_GENERATOR)
                return fail(g, "flatMap requires generator protocol");
            out = fsig->params[1]->pointee;
            break;
    }

    cflow_node n = {0};
    n.op = op;
    n.fn = bound;
    n.has_fn = true;
    n.input_type = in;
    n.output_type = out;
    if (nested_count) {
        n.subgraphs = malloc(nested_count * sizeof(*n.subgraphs));
        if (!n.subgraphs) return fail(g, "nested subgraph reference allocation failed");
        memcpy(n.subgraphs, nested, nested_count * sizeof(*n.subgraphs));
        n.subgraph_count = nested_count;
    }
    *out_node = n;
    return true;
}

bool cflow_graph_create_node(cflow_graph *g,
                             cflow_subgraph_id subgraph,
                             cflow_op op,
                             cmeta_callable fn,
                             const cflow_subgraph_id *nested,
                             size_t nested_count,
                             cflow_node_id *out_node) {
    uint64_t version;
    cflow_subgraph *sg = g && subgraph < g->subgraph_count ? &g->subgraphs[subgraph] : NULL;
    if (!sg || !out_node) return fail(g, "invalid target subgraph");
    cflow_node node = {0};
    if (!derive_node(g, op, fn, nested, nested_count, &node)) return false;
    if (!cflow_graph_version_acquire(&version)) {
        node_destroy(&node);
        return fail(g, "graph version space exhausted");
    }
    cflow_node_id id = subgraph_append_node(sg, node);
    if (id == CMETA_INVALID_ID) { node_destroy(&node); return fail(g, "node allocation failed"); }
    g->version = version;
    g->error = NULL;
    *out_node = id;
    return true;
}

bool cflow_graph_create_slice_node(cflow_graph *g,
                                   cflow_subgraph_id subgraph,
                                   cflow_op op,
                                   const cmeta_type_desc *input_type,
                                   size_t limit,
                                   cflow_node_id *out_node) {
    uint64_t version;
    cflow_subgraph *sg = g && subgraph < g->subgraph_count
        ? &g->subgraphs[subgraph] : NULL;
    if (!sg || !out_node || !cmeta_type_desc_valid(input_type))
        return fail(g, "invalid slice node arguments");
    if (op != CFLOW_OP_TAKE && op != CFLOW_OP_SKIP)
        return fail(g, "slice node requires TAKE or SKIP");
    if (!cflow_graph_version_acquire(&version))
        return fail(g, "graph version space exhausted");

    cflow_node node = {0};
    node.op = op;
    node.input_type = input_type;
    node.output_type = input_type;
    node.has_size_parameter = true;
    node.size_parameter = limit;
    node.param_kind = op == CFLOW_OP_TAKE
        ? CFLOW_NODE_PARAM_TAKE : CFLOW_NODE_PARAM_SKIP;
    if (op == CFLOW_OP_TAKE) node.params.take.count = limit;
    else node.params.skip.count = limit;
    cflow_node_id id = subgraph_append_node(sg, node);
    if (id == CMETA_INVALID_ID)
        return fail(g, "slice node allocation failed");
    g->version = version;
    g->error = NULL;
    *out_node = id;
    return true;
}

bool cflow_graph_create_distinct_node(cflow_graph *g,
                                      cflow_subgraph_id subgraph,
                                      const cmeta_type_desc *input_type,
                                      size_t max_unique,
                                      cflow_node_id *out_node) {
    uint64_t version;
    cflow_subgraph *sg = g && subgraph < g->subgraph_count
        ? &g->subgraphs[subgraph] : NULL;
    cflow_node node = {0};
    cflow_node_id id;
    const cmeta_trait_flags required = CMETA_TRAIT_HASH | CMETA_TRAIT_EQUAL;

    if (!sg || !out_node || !cmeta_type_desc_valid(input_type) ||
        max_unique == 0u)
        return fail(g, "invalid distinct node arguments");
    if (cmeta_type_require_traits(input_type, required) != CMETA_OK)
        return fail(g, "distinct requires HASH and EQUAL traits");
    if (!cflow_graph_version_acquire(&version))
        return fail(g, "graph version space exhausted");
    node.op = CFLOW_OP_DISTINCT;
    node.input_type = input_type;
    node.output_type = input_type;
    node.param_kind = CFLOW_NODE_PARAM_DISTINCT;
    node.params.distinct.max_unique = max_unique;
    id = subgraph_append_node(sg, node);
    if (id == CMETA_INVALID_ID)
        return fail(g, "distinct node allocation failed");
    g->version = version;
    g->error = NULL;
    *out_node = id;
    return true;
}

bool cflow_graph_create_sorted_node(cflow_graph *g,
                                    cflow_subgraph_id subgraph,
                                    const cmeta_type_desc *input_type,
                                    size_t max_elements,
                                    cflow_node_id *out_node) {
    cflow_subgraph *sg;
    cflow_node node = {0};
    cflow_node_id id;
    uint64_t version;
    const cmeta_trait_flags required = CMETA_TRAIT_COMPARE |
        CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY;
    if (!g || subgraph >= g->subgraph_count || !input_type ||
        max_elements == 0u || !out_node)
        return fail(g, "invalid sorted node arguments");
    if (cmeta_type_require_traits(input_type, required) != CMETA_OK)
        return fail(g, "sorted requires COMPARE and lifecycle traits");
    if (!cflow_graph_version_acquire(&version))
        return fail(g, "graph version space exhausted");
    sg = &g->subgraphs[subgraph];
    node.op = CFLOW_OP_SORTED;
    node.input_type = input_type;
    node.output_type = input_type;
    node.param_kind = CFLOW_NODE_PARAM_SORTED;
    node.params.sorted.max_elements = max_elements;
    id = subgraph_append_node(sg, node);
    if (id == CMETA_INVALID_ID)
        return fail(g, "sorted node allocation failed");
    g->version = version;
    g->error = NULL;
    *out_node = id;
    return true;
}


static bool build_seeded_reduce_node(cflow_graph *g,
                                     cmeta_callable reducer,
                                     const void *seed,
                                     cflow_node *out_node) {
    cflow_node node = {0};
    if (!g || !seed || !out_node ||
        !derive_node(g, CFLOW_OP_REDUCE, reducer, NULL, 0u, &node))
        return false;
    if (!cmeta_type_equal(node.input_type, node.output_type) ||
        !node_set_reduce_seed(&node, seed)) {
        node_destroy(&node);
        return fail(g, "seeded reduce requires an owned comparable T seed");
    }
    *out_node = node;
    return true;
}

bool cflow_graph_create_seeded_reduce_node(
    cflow_graph *g,
    cflow_subgraph_id subgraph,
    cmeta_callable reducer,
    const void *seed,
    cflow_node_id *out_node) {
    uint64_t version;
    cflow_subgraph *sg =
        g && subgraph < g->subgraph_count ? &g->subgraphs[subgraph] : NULL;
    cflow_node node = {0};
    cflow_node_id id;
    if (!sg || !out_node || !build_seeded_reduce_node(g, reducer, seed, &node))
        return false;
    if (!cflow_graph_version_acquire(&version)) {
        node_destroy(&node);
        return fail(g, "graph version space exhausted");
    }
    id = subgraph_append_node(sg, node);
    if (id == CMETA_INVALID_ID) {
        node_destroy(&node);
        return fail(g, "seeded reduce node allocation failed");
    }
    g->version = version;
    g->error = NULL;
    *out_node = id;
    return true;
}

bool cflow_graph_create_relation_node(cflow_graph *g,
                                      cflow_subgraph_id subgraph,
                                      const cmeta_type_desc *input_type,
                                      const cflow_subgraph_id *branches,
                                      size_t branch_count,
                                      cflow_relation_schema schema,
                                      cmeta_callable reducer,
                                      cflow_node_id *out_node) {
    uint64_t version;
    cflow_subgraph *sg = g && subgraph < g->subgraph_count ? &g->subgraphs[subgraph] : NULL;
    if (!sg || !input_type || !branches || branch_count == 0u || !out_node)
        return fail(g, "invalid relation node arguments");
    if (!relation_schema_valid(g, schema)) return false;

    const cmeta_type_desc *out_type = NULL;
    const cmeta_sig_desc *rsig = NULL;
    cmeta_callable bound_reducer = {0};
    if (schema.result == CFLOW_REL_RESULT_FOLD ||
        schema.result == CFLOW_REL_RESULT_INVOKE) {
        if (!cmeta_callable_bind(reducer, &bound_reducer))
            return fail(g, "relation callable effect/property contract is invalid");
        rsig = cmeta_callable_signature(bound_reducer);
    }

    if (schema.result == CFLOW_REL_RESULT_INVOKE) {
        if (!rsig || rsig->protocol != CMETA_FN_PROTOCOL_VALUE ||
            rsig->param_count != branch_count || branch_count != 2u ||
            !rsig->return_type || rsig->return_type->kind == CMETA_T_VOID)
            return fail(g, "relation INVOKE requires a binary value callable");
        out_type = rsig->return_type;
    }

    const cmeta_type_desc *homogeneous = NULL;
    for (size_t i = 0; i < branch_count; ++i) {
        if (branches[i] >= g->subgraph_count ||
            !cmeta_type_equal(cflow_subgraph_input_type(g, branches[i]), input_type))
            return fail(g, "relation branch source type mismatch");
        const cmeta_type_desc *bout = cflow_subgraph_output_type(g, branches[i]);
        if (schema.result == CFLOW_REL_RESULT_INVOKE) {
            if (!cmeta_type_equal(rsig->params[i], bout))
                return fail(g, "relation INVOKE parameter/branch type mismatch");
        } else {
            if (!homogeneous) homogeneous = bout;
            if (!cmeta_type_equal(homogeneous, bout))
                return fail(g, "relation branches must have one homogeneous output type");
        }
    }
    if (!out_type) out_type = homogeneous;

    if (schema.result == CFLOW_REL_RESULT_FOLD) {
        if (!rsig || rsig->protocol != CMETA_FN_PROTOCOL_VALUE || rsig->param_count != 2u ||
            !cmeta_type_equal(rsig->params[0], out_type) ||
            !cmeta_type_equal(rsig->params[1], out_type) ||
            !cmeta_type_equal(rsig->return_type, out_type) ||
            !cflow_op_signature_allowed(CFLOW_OP_REDUCE, bound_reducer.meta.sig))
            return fail(g, "relation FOLD reducer/type contract is invalid");
    }

    cflow_node node = {0};
    node.op = CFLOW_OP_RELATION;
    node.input_type = input_type;
    node.output_type = out_type;
    node.has_relation = true;
    node.relation = schema;
    node.subgraphs = malloc(branch_count * sizeof(*node.subgraphs));
    if (!node.subgraphs) return fail(g, "relation subgraph reference allocation failed");
    memcpy(node.subgraphs, branches, branch_count * sizeof(*node.subgraphs));
    node.subgraph_count = branch_count;
    if (schema.result == CFLOW_REL_RESULT_FOLD ||
        schema.result == CFLOW_REL_RESULT_INVOKE) {
        node.fn = bound_reducer;
        node.has_fn = true;
    }
    if (!cflow_graph_version_acquire(&version)) {
        node_destroy(&node);
        return fail(g, "graph version space exhausted");
    }
    cflow_node_id id = subgraph_append_node(sg, node);
    if (id == CMETA_INVALID_ID) { node_destroy(&node); return fail(g, "relation node allocation failed"); }
    g->version = version;
    g->error = NULL;
    *out_node = id;
    return true;
}

bool cflow_graph_connect(cflow_graph *g,
                         cflow_subgraph_id subgraph,
                         cflow_node_id from,
                         uint16_t from_port,
                         cflow_node_id to,
                         uint16_t to_port) {
    uint64_t version;
    cflow_subgraph *sg = g && subgraph < g->subgraph_count ? &g->subgraphs[subgraph] : NULL;
    if (!sg || from >= sg->node_count || to >= sg->node_count) return fail(g, "invalid edge endpoint");
    if (!cmeta_type_equal(sg->nodes[from].output_type, sg->nodes[to].input_type))
        return fail(g, "edge type mismatch");
    if (!cflow_graph_version_acquire(&version))
        return fail(g, "graph version space exhausted");
    if (!reserve_edges(sg, sg->edge_count + 1u)) return fail(g, "edge allocation failed");
    sg->edges[sg->edge_count++] = (cflow_edge){ from, from_port, to, to_port };
    g->version = version;
    g->error = NULL;
    return true;
}

bool cflow_graph_set_subgraph_exit(cflow_graph *g,
                                   cflow_subgraph_id subgraph,
                                   cflow_node_id exit_node) {
    uint64_t version;
    cflow_subgraph *sg = g && subgraph < g->subgraph_count ? &g->subgraphs[subgraph] : NULL;
    if (!sg || exit_node >= sg->node_count) return fail(g, "invalid subgraph exit");
    if (!cflow_graph_version_acquire(&version))
        return fail(g, "graph version space exhausted");
    sg->tail = exit_node;
    sg->output_type = sg->nodes[exit_node].output_type;
    g->version = version;
    g->error = NULL;
    return true;
}

bool cflow_graph_add(cflow_graph *g, cflow_op op,
                     cmeta_callable fn,
                     const cflow_graph *nested_graph) {
    uint64_t version;
    if (!g || g->root >= g->subgraph_count) return fail(g, "graph is not initialized");
    cflow_subgraph_id root_id = g->root;
    cflow_subgraph *root = &g->subgraphs[root_id];
    const cflow_op_schema *schema = cflow_op_schema_get(op);
    cmeta_callable bound;
    if (!schema) return fail(g, "unsupported graph operator");
    if (!cmeta_callable_bind(fn, &bound))
        return fail(g, "callable effect/property contract is invalid");
    const cmeta_sig_desc *fsig = cmeta_callable_signature(bound);
    if (!fsig || fsig->param_count != schema->fn_arity)
        return fail(g, "operator function arity mismatch");
    if (!cflow_op_signature_allowed(op, bound.meta.sig))
        return fail(g, "signature is not enabled for this operator");
    if (!cflow_graph_version_acquire(&version))
        return fail(g, "graph version space exhausted");

    const cmeta_type_desc *in = root->output_type;
    const cmeta_type_desc *subgraph_out = NULL;
    cflow_subgraph_id imported_subgraph = CMETA_INVALID_ID;
    switch (schema->subgraph_rule) {
        case CFLOW_SUBGRAPH_NONE:
            if (nested_graph) return fail(g, "operator does not accept a subgraph branch");
            break;
        case CFLOW_SUBGRAPH_1TO1:
            if (!nested_graph || nested_graph->root >= nested_graph->subgraph_count)
                return fail(g, "operator requires a subgraph branch");
            if (!cmeta_type_equal(root->input_type, cflow_graph_input_type(nested_graph)))
                return fail(g, "subgraph branch must have the same source type");
            if (!cflow_subgraph_is_one_to_one(g, g->root) || !cflow_graph_is_one_to_one(nested_graph))
                return fail(g, "operator requires 1:1 branches");
            subgraph_out = cflow_graph_output_type(nested_graph);
            break;
    }

    for (size_t i = 0; i < schema->fn_arity; ++i)
        if (!check_param_rule(g, schema->params[i], fsig->params[i], in, subgraph_out)) return false;
    if (!check_return_rule(g, schema->ret, fsig->return_type, in)) return false;

    const cmeta_type_desc *out = NULL;
    switch (schema->output) {
        case CFLOW_OUTPUT_SAME: out = in; break;
        case CFLOW_OUTPUT_RETURN: out = fsig->return_type; break;
        case CFLOW_OUTPUT_POINTEE1:
            if (fsig->param_count < 2 || !fsig->params[1] ||
                fsig->params[1]->kind != CMETA_T_POINTER || !fsig->params[1]->pointee)
                return fail(g, "operator cannot infer output pointee type");
            if (fsig->protocol != CMETA_FN_PROTOCOL_GENERATOR)
                return fail(g, "flatMap requires generator protocol");
            out = fsig->params[1]->pointee;
            break;
    }

    size_t subgraph_mark = g->subgraph_count;
    if (nested_graph && !import_graph(g, nested_graph, &imported_subgraph))
        return fail(g, "subgraph snapshot import failed");
    /* import_graph may grow/realloc the Graph-wide subgraph table. */
    root = &g->subgraphs[root_id];

    cflow_node node = {0};
    node.op = op;
    node.fn = bound;
    node.has_fn = true;
    node.input_type = in;
    node.output_type = out;
    if (imported_subgraph != CMETA_INVALID_ID) {
        node.subgraphs = malloc(sizeof(*node.subgraphs));
        if (!node.subgraphs) { truncate_subgraphs(g, subgraph_mark); return fail(g, "subgraph reference allocation failed"); }
        node.subgraphs[0] = imported_subgraph;
        node.subgraph_count = 1;
    }

    cflow_node_id old_tail = root->tail;
    cflow_node_id id = subgraph_append_node(root, node);
    if (id == CMETA_INVALID_ID) {
        node_destroy(&node);
        truncate_subgraphs(g, subgraph_mark);
        return fail(g, "node allocation failed");
    }
    if (old_tail != CMETA_INVALID_ID && !subgraph_add_edge(root, old_tail, id)) {
        node_destroy(&root->nodes[id]);
        --root->node_count;
        root->tail = old_tail;
        root->output_type = old_tail < root->node_count ? root->nodes[old_tail].output_type : root->input_type;
        truncate_subgraphs(g, subgraph_mark);
        return fail(g, "edge allocation failed");
    }
    g->version = version;
    g->error = NULL;
    return true;
}


bool cflow_graph_add_explicit_typed_adapter(
    cflow_graph *g,
    cflow_op op,
    cmeta_callable fn,
    const cmeta_type_desc *input_type,
    const cmeta_type_desc *output_type) {
    cflow_subgraph *root;
    cflow_node_id old_tail;
    cflow_node_id id = CMETA_INVALID_ID;

    if (!g || g->root >= g->subgraph_count)
        return fail(g, "graph is not initialized");
    root = &g->subgraphs[g->root];
    if (!cmeta_type_equal(root->output_type, input_type))
        return fail(g, "explicit typed adapter input type does not match graph output");

    old_tail = root->tail;
    if (!cflow_graph_create_explicit_typed_adapter_node(
            g, g->root, op, fn, input_type, output_type, &id))
        return false;

    root = &g->subgraphs[g->root];
    if (old_tail != CMETA_INVALID_ID &&
        !subgraph_add_edge(root, old_tail, id)) {
        node_destroy(&root->nodes[id]);
        --root->node_count;
        root->tail = old_tail;
        root->output_type = old_tail < root->node_count
            ? root->nodes[old_tail].output_type : root->input_type;
        return fail(g, "explicit typed adapter edge allocation failed");
    }
    g->error = NULL;
    return true;
}

bool cflow_graph_add_explicit_map_adapter(
    cflow_graph *g,
    cmeta_callable fn,
    const cmeta_type_desc *input_type,
    const cmeta_type_desc *output_type) {
    return cflow_graph_add_explicit_typed_adapter(
        g, CFLOW_OP_MAP, fn, input_type, output_type);
}

static bool graph_add_slice(cflow_graph *g, cflow_op op, size_t limit) {
    uint64_t version;
    cflow_subgraph *root;
    cflow_node node = {0};
    cflow_node_id old_tail;
    cflow_node_id id;

    if (!g || g->root >= g->subgraph_count)
        return fail(g, "graph is not initialized");
    if (op != CFLOW_OP_TAKE && op != CFLOW_OP_SKIP)
        return fail(g, "slice operator is invalid");
    if (!cflow_graph_version_acquire(&version))
        return fail(g, "graph version space exhausted");
    root = &g->subgraphs[g->root];
    node.op = op;
    node.input_type = root->output_type;
    node.output_type = root->output_type;
    node.has_size_parameter = true;
    node.size_parameter = limit;
    node.param_kind = op == CFLOW_OP_TAKE
        ? CFLOW_NODE_PARAM_TAKE : CFLOW_NODE_PARAM_SKIP;
    if (op == CFLOW_OP_TAKE) node.params.take.count = limit;
    else node.params.skip.count = limit;
    old_tail = root->tail;
    id = subgraph_append_node(root, node);
    if (id == CMETA_INVALID_ID)
        return fail(g, "slice node allocation failed");
    if (old_tail != CMETA_INVALID_ID &&
        !subgraph_add_edge(root, old_tail, id)) {
        node_destroy(&root->nodes[id]);
        --root->node_count;
        root->tail = old_tail;
        root->output_type = old_tail < root->node_count
            ? root->nodes[old_tail].output_type : root->input_type;
        return fail(g, "edge allocation failed");
    }
    g->version = version;
    g->error = NULL;
    return true;
}

bool cflow_graph_take(cflow_graph *g, size_t limit) {
    return graph_add_slice(g, CFLOW_OP_TAKE, limit);
}

bool cflow_graph_skip(cflow_graph *g, size_t count) {
    return graph_add_slice(g, CFLOW_OP_SKIP, count);
}

bool cflow_graph_distinct(cflow_graph *g, size_t max_unique) {
    uint64_t version;
    cflow_subgraph *root;
    cflow_node node = {0};
    cflow_node_id old_tail;
    cflow_node_id id;
    const cmeta_trait_flags required = CMETA_TRAIT_HASH | CMETA_TRAIT_EQUAL;

    if (!g || g->root >= g->subgraph_count)
        return fail(g, "graph is not initialized");
    root = &g->subgraphs[g->root];
    if (max_unique == 0u)
        return fail(g, "distinct max_unique must be nonzero");
    if (cmeta_type_require_traits(root->output_type, required) != CMETA_OK)
        return fail(g, "distinct requires HASH and EQUAL traits");
    if (!cflow_graph_version_acquire(&version))
        return fail(g, "graph version space exhausted");
    node.op = CFLOW_OP_DISTINCT;
    node.input_type = root->output_type;
    node.output_type = root->output_type;
    node.param_kind = CFLOW_NODE_PARAM_DISTINCT;
    node.params.distinct.max_unique = max_unique;
    old_tail = root->tail;
    id = subgraph_append_node(root, node);
    if (id == CMETA_INVALID_ID)
        return fail(g, "distinct node allocation failed");
    if (old_tail != CMETA_INVALID_ID &&
        !subgraph_add_edge(root, old_tail, id)) {
        node_destroy(&root->nodes[id]);
        --root->node_count;
        root->tail = old_tail;
        root->output_type = old_tail < root->node_count
            ? root->nodes[old_tail].output_type : root->input_type;
        return fail(g, "edge allocation failed");
    }
    g->version = version;
    g->error = NULL;
    return true;
}

bool cflow_graph_sorted(cflow_graph *g, size_t max_elements) {
    cflow_subgraph *root;
    cflow_node node = {0};
    cflow_node_id old_tail;
    cflow_node_id id;
    uint64_t version;
    const cmeta_trait_flags required = CMETA_TRAIT_COMPARE |
        CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY;
    if (!g || g->root >= g->subgraph_count)
        return fail(g, "invalid graph");
    root = &g->subgraphs[g->root];
    if (max_elements == 0u)
        return fail(g, "sorted max_elements must be nonzero");
    if (cmeta_type_require_traits(root->output_type, required) != CMETA_OK)
        return fail(g, "sorted requires COMPARE and lifecycle traits");
    if (!cflow_graph_version_acquire(&version))
        return fail(g, "graph version space exhausted");
    node.op = CFLOW_OP_SORTED;
    node.input_type = root->output_type;
    node.output_type = root->output_type;
    node.param_kind = CFLOW_NODE_PARAM_SORTED;
    node.params.sorted.max_elements = max_elements;
    old_tail = root->tail;
    id = subgraph_append_node(root, node);
    if (id == CMETA_INVALID_ID)
        return fail(g, "sorted node allocation failed");
    if (old_tail != CMETA_INVALID_ID &&
        !cflow_graph_connect(g, g->root, old_tail, 0u, id, 0u))
        return false;
    root->tail = id;
    root->output_type = node.output_type;
    g->version = version;
    g->error = NULL;
    return true;
}


bool cflow_graph_reduce_seeded(cflow_graph *g,
                               cmeta_callable reducer,
                               const void *seed) {
    uint64_t version;
    cflow_subgraph *root;
    cflow_node node = {0};
    cflow_node_id old_tail;
    cflow_node_id id;
    if (!g || g->root >= g->subgraph_count)
        return fail(g, "graph is not initialized");
    root = &g->subgraphs[g->root];
    if (!build_seeded_reduce_node(g, reducer, seed, &node))
        return false;
    if (!cmeta_type_equal(root->output_type, node.input_type)) {
        node_destroy(&node);
        return fail(g, "seeded reduce input type does not match graph output");
    }
    if (!cflow_graph_version_acquire(&version)) {
        node_destroy(&node);
        return fail(g, "graph version space exhausted");
    }
    old_tail = root->tail;
    id = subgraph_append_node(root, node);
    if (id == CMETA_INVALID_ID) {
        node_destroy(&node);
        return fail(g, "seeded reduce node allocation failed");
    }
    if (old_tail != CMETA_INVALID_ID &&
        !subgraph_add_edge(root, old_tail, id)) {
        node_destroy(&root->nodes[id]);
        --root->node_count;
        root->tail = old_tail;
        root->output_type = old_tail < root->node_count
            ? root->nodes[old_tail].output_type : root->input_type;
        return fail(g, "seeded reduce edge allocation failed");
    }
    g->version = version;
    g->error = NULL;
    return true;
}

const void *cflow_node_reduce_seed(const cflow_node *node) {
    const cflow_value_slot *slot = node_reduce_seed_slot(node);
    return slot && slot->live ? slot->storage : NULL;
}


static bool relation_schema_valid(cflow_graph *g, cflow_relation_schema schema) {
    switch (schema.coordination) {
        case CFLOW_REL_COORD_ALL:
            if (schema.result != CFLOW_REL_RESULT_FOLD &&
                schema.result != CFLOW_REL_RESULT_INVOKE)
                return fail(g, "ALL relation requires FOLD or INVOKE result policy");
            break;
        case CFLOW_REL_COORD_ANY:
            if (schema.result != CFLOW_REL_RESULT_SELECT)
                return fail(g, "ANY relation requires SELECT result policy");
            if (schema.completion == CFLOW_REL_COMPLETE_ALL_DONE)
                return fail(g, "ANY relation does not support ALL_DONE completion");
            break;
        case CFLOW_REL_COORD_LATEST:
            if (schema.result != CFLOW_REL_RESULT_FOLD)
                return fail(g, "LATEST relation requires FOLD result policy");
            break;
        case CFLOW_REL_COORD_SEQUENCE:
            if (schema.result != CFLOW_REL_RESULT_SELECT)
                return fail(g, "SEQUENCE relation requires SELECT result policy");
            break;
        default:
            return fail(g, "relation coordination policy is invalid");
    }
    if (schema.completion != CFLOW_REL_COMPLETE_COORDINATOR &&
        schema.completion != CFLOW_REL_COMPLETE_FIRST_RESULT &&
        schema.completion != CFLOW_REL_COMPLETE_ALL_DONE)
        return fail(g, "relation completion policy is invalid");
    switch (schema.error) {
        case CFLOW_REL_ERROR_FAIL_FAST:
            break;
        case CFLOW_REL_ERROR_IGNORE:
            if (schema.coordination == CFLOW_REL_COORD_ALL)
                return fail(g, "ALL relation cannot IGNORE a child that may have no value");
            break;
        case CFLOW_REL_ERROR_TRY_NEXT:
            if (schema.coordination != CFLOW_REL_COORD_SEQUENCE ||
                schema.result != CFLOW_REL_RESULT_SELECT ||
                schema.completion != CFLOW_REL_COMPLETE_FIRST_RESULT)
                return fail(g, "TRY_NEXT requires SEQUENCE + SELECT + FIRST_RESULT");
            break;
        default:
            return fail(g, "relation error policy is invalid");
    }
    return true;
}

static bool build_relation(cflow_graph *g,
                           const cflow_graph *const *branches,
                           size_t branch_count,
                           cflow_relation_schema schema,
                           cmeta_callable reducer) {
    uint64_t version;
    if (!g || g->root >= g->subgraph_count || !branches || branch_count < 1u)
        return fail(g, "relation requires at least one branch");
    if (!relation_schema_valid(g, schema)) return false;
    if (schema.result == CFLOW_REL_RESULT_INVOKE)
        return fail(g, "INVOKE relation is reserved for structured lowering/IR builder");
    if (!cflow_graph_version_acquire(&version))
        return fail(g, "graph version space exhausted");

    cflow_subgraph_id root_id = g->root;
    cflow_subgraph *root = &g->subgraphs[root_id];
    const cmeta_type_desc *relation_input = root->output_type;
    const cmeta_type_desc *branch_output = NULL;

    const cmeta_sig_desc *rsig = NULL;
    cmeta_callable bound_reducer = {0};
    if (schema.result == CFLOW_REL_RESULT_FOLD) {
        if (!cmeta_callable_bind(reducer, &bound_reducer))
            return fail(g, "relation FOLD reducer contract is invalid");
        rsig = cmeta_callable_signature(bound_reducer);
        if (!rsig || rsig->protocol != CMETA_FN_PROTOCOL_VALUE || rsig->param_count != 2u ||
            !rsig->return_type || !cmeta_type_equal(rsig->params[0], rsig->params[1]) ||
            !cmeta_type_equal(rsig->params[0], rsig->return_type))
            return fail(g, "relation FOLD reducer must have T(T,T) signature");
        if (!cflow_op_signature_allowed(CFLOW_OP_REDUCE, bound_reducer.meta.sig))
            return fail(g, "relation FOLD reducer signature is not enabled by reduce policy");
        branch_output = rsig->return_type;
    }

    cflow_subgraph_id *ids = calloc(branch_count, sizeof(*ids));
    if (!ids) return fail(g, "relation branch id allocation failed");
    for (size_t i = 0; i < branch_count; ++i) ids[i] = CMETA_INVALID_ID;

    size_t subgraph_mark = g->subgraph_count;
    for (size_t i = 0; i < branch_count; ++i) {
        const cflow_graph *b = branches[i];
        if (!b || b->root >= b->subgraph_count ||
            !cmeta_type_equal(cflow_graph_input_type(b), relation_input)) {
            truncate_subgraphs(g, subgraph_mark);
            free(ids);
            return fail(g, "relation branch source type mismatch");
        }
        const cmeta_type_desc *bout = cflow_graph_output_type(b);
        if (!branch_output) branch_output = bout;
        if (!cmeta_type_equal(bout, branch_output)) {
            truncate_subgraphs(g, subgraph_mark);
            free(ids);
            return fail(g, "relation branches must have one homogeneous output type");
        }
        if (!import_graph(g, b, &ids[i])) {
            truncate_subgraphs(g, subgraph_mark);
            free(ids);
            return fail(g, "relation branch snapshot import failed");
        }
    }

    root = &g->subgraphs[root_id];
    cflow_node relation = {0};
    relation.op = CFLOW_OP_RELATION;
    relation.input_type = relation_input;
    relation.output_type = branch_output;
    relation.subgraphs = ids;
    relation.subgraph_count = branch_count;
    relation.has_relation = true;
    relation.relation = schema;
    if (schema.result == CFLOW_REL_RESULT_FOLD) {
        relation.fn = bound_reducer;
        relation.has_fn = true;
    }

    cflow_node_id old_tail = root->tail;
    cflow_node_id relation_id = subgraph_append_node(root, relation);
    if (relation_id == CMETA_INVALID_ID) {
        truncate_subgraphs(g, subgraph_mark);
        free(ids);
        return fail(g, "relation node allocation failed");
    }
    if (old_tail != CMETA_INVALID_ID && !subgraph_add_edge(root, old_tail, relation_id))
        return fail(g, "relation input edge allocation failed");

    root->tail = relation_id;
    root->output_type = branch_output;
    g->version = version;
    g->error = NULL;
    return true;
}

bool cflow_graph_relation(cflow_graph *g,
                          const cflow_graph *const *branches,
                          size_t branch_count,
                          cflow_relation_schema schema,
                          cmeta_callable reducer) {
    if (!g) return false;
    cflow_graph work;
    memset(&work, 0, sizeof(work));
    work.root = CMETA_INVALID_ID;
    if (!cflow_graph_clone(&work, g)) return fail(g, "relation transaction clone failed");
    if (!build_relation(&work, branches, branch_count, schema, reducer)) {
        const char *err = work.error;
        cflow_graph_destroy(&work);
        return fail(g, err ? err : "relation construction failed");
    }
    const char *validation_error = NULL;
    if (!cflow_graph_validate(&work, &validation_error)) {
        cflow_graph_destroy(&work);
        return fail(g, validation_error ? validation_error : "relation graph validation failed");
    }
    cflow_graph_destroy(g);