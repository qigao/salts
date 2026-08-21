#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cflow/adapters.h>
#include <cflow/effect.h>
#include <cflow/graph.h>
#include <cflow/lower.h>
#include <cflow/meta.h>
#include <cflow/opt.h>
#include <cflow/property.h>

#define GATE_STABLE (CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS)

typed_raw(map, CMETA_EFFECT_STATEFUL, GATE_STABLE,
          int, gate_stateful_plus_one, (int x)) {
    return x + 1;
}

typed_raw(map, CMETA_EFFECT_PURE, GATE_STABLE,
          int, gate_pure_twice, (int x)) {
    return x * 2;
}

typed_raw(map, CMETA_EFFECT_PURE, GATE_STABLE,
          int, gate_rel_pure_stable, (int x)) {
    return x * 3;
}

typed_raw(map, CMETA_EFFECT_PURE, CMETA_PROP_NONE,
          int, gate_rel_pure_unstable, (int x)) {
    return x * 3;
}

typed_raw(map, CMETA_EFFECT_STATEFUL, GATE_STABLE,
          int, gate_rel_impure_stable, (int x)) {
    return x * 3;
}

static int emit_ints(const void *data, size_t count) {
    const int *xs = (const int *)data;
    if (count && !data) return 0;
    printf("[");
    for (size_t i = 0; i < count; ++i) {
        if (i) printf(", ");
        printf("%d", xs[i]);
    }
    printf("]");
    return 1;
}

static int results_equal(const cflow_result *a, const cflow_result *b) {
    if (!a || !b || !cmeta_type_equal(a->type, b->type) || a->count != b->count)
        return 0;
    if (!a->count) return 1;
    return a->data && b->data &&
        memcmp(a->data, b->data, a->count * a->type->size) == 0;
}

static size_t graph_node_count(const cflow_graph *g) {
    size_t count = 0;
    if (!g) return 0;
    for (size_t i = 0; i < g->subgraph_count; ++i)
        count += g->subgraphs[i].node_count;
    return count;
}

static int build_map_effect_graph(cflow_graph *g) {
    cflow_graph_init(g, &cmeta_type_int);
    return g->root != CMETA_INVALID_ID &&
        cflow_graph_map(g, gate_stateful_plus_one.fn) &&
        cflow_graph_map(g, gate_pure_twice.fn);
}

static int emit_map_effect_witness(void) {
    static const int inputs[] = { -2, 0, 3 };
    cflow_graph surface = {0}, normalized = {0}, optimized = {0};
    cflow_opt_stats stats = {0};
    cflow_result before = {0}, after = {0};
    const cflow_subgraph *sg;
    const cflow_node *first;
    const cflow_node *second;
    cflow_node_id first_id = CMETA_INVALID_ID;
    cflow_node_id second_id = CMETA_INVALID_ID;
    int first_pure, second_pure;
    int ok = 0;

    normalized.root = optimized.root = CMETA_INVALID_ID;
    if (!build_map_effect_graph(&surface) ||
        !cflow_graph_normalize(&normalized, &surface))
        goto done;

    sg = cflow_graph_subgraph(&normalized, normalized.root);
    if (!sg || !cflow_subgraph_single_successor(sg, sg->entry, &first_id) ||
        !cflow_subgraph_single_successor(sg, first_id, &second_id))
        goto done;
    first = cflow_subgraph_node(sg, first_id);
    second = cflow_subgraph_node(sg, second_id);
    if (!first || !second || first->op != CFLOW_OP_MAP || second->op != CFLOW_OP_MAP)
        goto done;
    first_pure = cmeta_effects_are_pure(cflow_node_effects(&normalized, first)) ? 1 : 0;
    second_pure = cmeta_effects_are_pure(cflow_node_effects(&normalized, second)) ? 1 : 0;

    if (!cflow_eval_array(&normalized, inputs, 3u, &before) ||
        !cflow_graph_optimize(&optimized, &normalized,
                              (cflow_opt_options){ CMETA_OPT_DEFAULT }, &stats) ||
        !cflow_eval_array(&optimized, inputs, 3u, &after) ||
        !results_equal(&before, &after))
        goto done;

    printf("  { name := \"impure_map_blocks_fusion\", input := ");
    if (!emit_ints(inputs, 3u)) goto done;
    printf(", firstPure := %s, secondPure := %s,\n",
           first_pure ? "true" : "false", second_pure ? "true" : "false");
    printf("    nodesBefore := %zu, nodesAfter := %zu, mapNodesFused := %zu, effectBlocked := %zu,\n",
           graph_node_count(&normalized), graph_node_count(&optimized),
           stats.map_nodes_fused, stats.effect_blocked_map_fusions);
    printf("    before := ");
    if (!emit_ints(before.data, before.count)) goto done;
    printf(", after := ");
    if (!emit_ints(after.data, after.count)) goto done;
    printf(" } ::\n");
    ok = 1;

done:
    cflow_result_destroy(&after);
    cflow_result_destroy(&before);
    cflow_graph_destroy(&optimized);
    cflow_graph_destroy(&normalized);
    cflow_graph_destroy(&surface);
    return ok;
}

typedef cflow_map_callable (*relation_callable_provider)(void);

static cflow_map_callable relation_pure_stable(void) { return gate_rel_pure_stable; }
static cflow_map_callable relation_pure_unstable(void) { return gate_rel_pure_unstable; }
static cflow_map_callable relation_impure_stable(void) { return gate_rel_impure_stable; }

static int build_single_relation(cflow_graph *g, relation_callable_provider provider) {
    cflow_graph branch = {0};
    const cflow_graph *branches[1];
    cflow_relation_schema schema = {
        CFLOW_REL_COORD_SEQUENCE,
        CFLOW_REL_COMPLETE_FIRST_RESULT,
        CFLOW_REL_RESULT_SELECT,
        CFLOW_REL_ERROR_FAIL_FAST
    };
    int ok = 0;

    cflow_graph_init(g, &cmeta_type_int);
    cflow_graph_init(&branch, &cmeta_type_int);
    if (g->root == CMETA_INVALID_ID || branch.root == CMETA_INVALID_ID)
        goto done;
    if (!cflow_graph_map(&branch, provider().fn)) goto done;
    branches[0] = &branch;
    ok = cflow_graph_relation(g, branches, 1u, schema, (cmeta_callable){0});

done:
    cflow_graph_destroy(&branch);
    return ok;
}

static int emit_relation_gate_witness(const char *name,
                                      relation_callable_provider provider) {
    static const int inputs[] = { -2, 0, 3 };
    cflow_graph surface = {0}, normalized = {0}, optimized = {0};
    cflow_opt_stats stats = {0};
    cflow_result before = {0}, after = {0};
    const cflow_subgraph *before_sg;
    const cflow_subgraph *after_sg;
    const cflow_node *before_rel;
    const cflow_node *after_rel;
    int is_pure, is_stable;
    int ok = 0;

    normalized.root = optimized.root = CMETA_INVALID_ID;
    if (!build_single_relation(&surface, provider) ||
        !cflow_graph_normalize(&normalized, &surface))
        goto done;

    before_sg = cflow_graph_subgraph(&normalized, normalized.root);
    before_rel = before_sg ? cflow_subgraph_node(before_sg, before_sg->tail) : NULL;
    if (!before_rel || before_rel->op != CFLOW_OP_RELATION ||
        before_rel->subgraph_count != 1u)
        goto done;

    is_pure = cmeta_effects_are_pure(cflow_node_effects(&normalized, before_rel)) ? 1 : 0;
    is_stable = cmeta_properties_include(
        cflow_node_properties(&normalized, before_rel), CMETA_PROP_STABLE) ? 1 : 0;

    if (!cflow_eval_array(&normalized, inputs, 3u, &before) ||
        !cflow_graph_optimize(&optimized, &normalized,
                              (cflow_opt_options){ CMETA_OPT_DEFAULT }, &stats) ||
        !cflow_eval_array(&optimized, inputs, 3u, &after) ||
        !results_equal(&before, &after))
        goto done;

    after_sg = cflow_graph_subgraph(&optimized, optimized.root);
    after_rel = after_sg ? cflow_subgraph_node(after_sg, after_sg->tail) : NULL;
    if (!after_rel || after_rel->op != CFLOW_OP_RELATION)
        goto done;

    printf("  { name := \"%s\", input := ", name);
    if (!emit_ints(inputs, 3u)) goto done;
    printf(", isPure := %s, isStable := %s,\n",
           is_pure ? "true" : "false", is_stable ? "true" : "false");
    printf("    beforeCoordination := \"SEQUENCE\", afterCoordination := \"%s\",\n",
           after_rel->relation.coordination == CFLOW_REL_COORD_ANY ? "ANY" : "SEQUENCE");
    printf("    simplified := %zu, effectBlocked := %zu, propertyBlocked := %zu,\n",
           stats.relation_schemas_simplified,
           stats.effect_blocked_relation_simplifications,
           stats.property_blocked_relation_simplifications);
    printf("    before := ");
    if (!emit_ints(before.data, before.count)) goto done;
    printf(", after := ");
    if (!emit_ints(after.data, after.count)) goto done;
    printf(" } ::\n");
    ok = 1;

done:
    cflow_result_destroy(&after);
    cflow_result_destroy(&before);
    cflow_graph_destroy(&optimized);
    cflow_graph_destroy(&normalized);
    cflow_graph_destroy(&surface);
    return ok;
}

int main(void) {
    puts("module");
    puts("import Std");
    puts("");
    puts("/-! GENERATED by formal/cmeta_optimizer_gating_conformance_gen.c using real opt.c gates. -/");
    puts("");
    puts("namespace CMeta.COptimizerGatingGenerated");
    puts("");
    puts("structure MapEffectWitness where");
    puts("  name : String");
    puts("  input : List Int");
    puts("  firstPure : Bool");
    puts("  secondPure : Bool");
    puts("  nodesBefore : Nat");
    puts("  nodesAfter : Nat");
    puts("  mapNodesFused : Nat");
    puts("  effectBlocked : Nat");
    puts("  before : List Int");
    puts("  after : List Int");
    puts("  deriving Repr, DecidableEq");
    puts("");
    puts("structure RelationGateWitness where");
    puts("  name : String");
    puts("  input : List Int");
    puts("  isPure : Bool");
    puts("  isStable : Bool");
    puts("  beforeCoordination : String");
    puts("  afterCoordination : String");
    puts("  simplified : Nat");
    puts("  effectBlocked : Nat");
    puts("  propertyBlocked : Nat");
    puts("  before : List Int");
    puts("  after : List Int");
    puts("  deriving Repr, DecidableEq");
    puts("");
    puts("def mapEffectWitnesses : List MapEffectWitness :=");
    if (!emit_map_effect_witness()) return EXIT_FAILURE;
    puts("  []");
    puts("");
    puts("def relationGateWitnesses : List RelationGateWitness :=");
    if (!emit_relation_gate_witness("relation_pure_stable_simplifies", relation_pure_stable) ||
        !emit_relation_gate_witness("relation_pure_unstable_blocks", relation_pure_unstable) ||
        !emit_relation_gate_witness("relation_impure_stable_blocks", relation_impure_stable))
        return EXIT_FAILURE;
    puts("  []");
    puts("");
    puts("end CMeta.COptimizerGatingGenerated");
    return EXIT_SUCCESS;
}

#undef GATE_STABLE
