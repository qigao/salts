#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cflow/adapters.h>
#include <cflow/graph.h>
#include <cflow/opt.h>

#define TOPO_PROPS (CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS)

typed_raw(map, CMETA_EFFECT_PURE, TOPO_PROPS,
          long, topo_reachable_i_l, (int x)) {
    return (long)x * 2L;
}

typed_raw(map, CMETA_EFFECT_PURE, TOPO_PROPS,
          long, topo_dead_i_l, (int x)) {
    return (long)x + 100L;
}

static int append_map(cflow_graph *g,
                      cflow_subgraph_id sgid,
                      cmeta_callable fn) {
    const cflow_subgraph *sg;
    cflow_node_id source;
    cflow_node_id map = CMETA_INVALID_ID;

    sg = cflow_graph_subgraph(g, sgid);
    if (!sg || sg->entry == CMETA_INVALID_ID) return 0;
    source = sg->entry;
    if (!cflow_graph_create_node(g, sgid, CFLOW_OP_MAP, fn, NULL, 0u, &map))
        return 0;
    if (!cflow_graph_connect(g, sgid, source, 0u, map, 0u)) return 0;
    return cflow_graph_set_subgraph_exit(g, sgid, map) ? 1 : 0;
}

static int build_graph(cflow_graph *g) {
    cflow_subgraph_id reachable;
    cflow_subgraph_id dead;
    cflow_node_id relation = CMETA_INVALID_ID;
    const cflow_subgraph *root;
    cflow_node_id root_source;
    cflow_relation_schema schema = {
        CFLOW_REL_COORD_SEQUENCE,
        CFLOW_REL_COMPLETE_COORDINATOR,
        CFLOW_REL_RESULT_SELECT,
        CFLOW_REL_ERROR_FAIL_FAST
    };

    cflow_graph_init(g, &cmeta_type_int);
    if (g->root == CMETA_INVALID_ID) return 0;

    reachable = cflow_graph_create_subgraph(g, &cmeta_type_int);
    dead = cflow_graph_create_subgraph(g, &cmeta_type_int);
    if (reachable == CMETA_INVALID_ID || dead == CMETA_INVALID_ID) return 0;
    if (!append_map(g, reachable, topo_reachable_i_l.fn)) return 0;
    if (!append_map(g, dead, topo_dead_i_l.fn)) return 0;

    if (!cflow_graph_create_relation_node(g, g->root, &cmeta_type_int,
                                          &reachable, 1u, schema,
                                          (cmeta_callable){0}, &relation))
        return 0;
    root = cflow_graph_subgraph(g, g->root);
    if (!root || root->entry == CMETA_INVALID_ID) return 0;
    root_source = root->entry;
    if (!cflow_graph_connect(g, g->root, root_source, 0u, relation, 0u)) return 0;
    if (!cflow_graph_set_subgraph_exit(g, g->root, relation)) return 0;

    return cflow_graph_validate(g, NULL) ? 1 : 0;
}

static void mark_reachable(const cflow_graph *g,
                           cflow_subgraph_id id,
                           unsigned char *seen) {
    const cflow_subgraph *sg;
    if (!g || !seen || id >= g->subgraph_count || seen[id]) return;
    seen[id] = 1u;
    sg = cflow_graph_subgraph(g, id);
    if (!sg) return;
    for (size_t n = 0; n < sg->node_count; ++n) {
        const cflow_node *node = cflow_subgraph_node(sg, (cflow_node_id)n);
        if (!node) continue;
        for (size_t k = 0; k < node->subgraph_count; ++k)
            mark_reachable(g, node->subgraphs[k], seen);
    }
}

static size_t reachable_count(const cflow_graph *g) {
    unsigned char *seen;
    size_t count = 0u;
    if (!g || g->root >= g->subgraph_count) return 0u;
    seen = calloc(g->subgraph_count ? g->subgraph_count : 1u, 1u);
    if (!seen) return 0u;
    mark_reachable(g, g->root, seen);
    for (size_t i = 0; i < g->subgraph_count; ++i) count += seen[i] ? 1u : 0u;
    free(seen);
    return count;
}

static int root_relation_shape_ok(const cflow_graph *g) {
    const cflow_subgraph *root;
    const cflow_node *relation;
    const cflow_subgraph *branch;
    if (!g || g->root >= g->subgraph_count) return 0;
    root = cflow_graph_subgraph(g, g->root);
    relation = root ? cflow_subgraph_node(root, root->tail) : NULL;
    if (!relation || relation->op != CFLOW_OP_RELATION ||
        relation->subgraph_count != 1u ||
        relation->subgraphs[0] >= g->subgraph_count ||
        !cmeta_type_equal(relation->input_type, &cmeta_type_int) ||
        !cmeta_type_equal(relation->output_type, &cmeta_type_long))
        return 0;
    branch = cflow_graph_subgraph(g, relation->subgraphs[0]);
    if (!branch || !cmeta_type_equal(branch->input_type, &cmeta_type_int) ||
        !cmeta_type_equal(branch->output_type, &cmeta_type_long) ||
        branch->node_count != 2u)
        return 0;
    return 1;
}

static int eval_long(const cflow_graph *g,
                     const int *inputs,
                     size_t input_count,
                     cflow_result *out) {
    return cflow_eval_array(g, inputs, input_count, out) &&
           cmeta_type_equal(out->type, &cmeta_type_long);
}

static int emit_values(const cflow_result *result) {
    const long *values;
    if (!result || !cmeta_type_equal(result->type, &cmeta_type_long) ||
        (result->count && !result->data)) return 0;
    values = (const long *)result->data;
    printf("[");
    for (size_t i = 0; i < result->count; ++i) {
        if (i) printf(", ");
        printf("%lld", (long long)values[i]);
    }
    printf("]");
    return 1;
}

int main(void) {
    static const int inputs[] = { -2, 0, 3 };
    cflow_graph before = {0};
    cflow_graph dead_on = {0};
    cflow_graph dead_off = {0};
    cflow_opt_stats on_stats = {0};
    cflow_opt_stats off_stats = {0};
    cflow_result before_result = {0};
    cflow_result on_result = {0};
    cflow_result off_result = {0};
    cflow_opt_options on_options = { CMETA_OPT_DEAD_SUBGRAPHS };
    cflow_opt_options off_options = { CMETA_OPT_CANONICALIZE };
    int ok = 0;

    before.root = CMETA_INVALID_ID;
    dead_on.root = CMETA_INVALID_ID;
    dead_off.root = CMETA_INVALID_ID;

    if (!build_graph(&before)) goto done;
    if (!root_relation_shape_ok(&before)) goto done;
    if (!cflow_graph_optimize(&dead_on, &before, on_options, &on_stats)) goto done;
    if (!cflow_graph_optimize(&dead_off, &before, off_options, &off_stats)) goto done;
    if (!root_relation_shape_ok(&dead_on) || !root_relation_shape_ok(&dead_off)) goto done;

    if (!eval_long(&before, inputs, 3u, &before_result) ||
        !eval_long(&dead_on, inputs, 3u, &on_result) ||
        !eval_long(&dead_off, inputs, 3u, &off_result))
        goto done;

    puts("module");
    puts("import Std");
    puts("");
    puts("/-! GENERATED by formal/cmeta_optimizer_topology_conformance_gen.c using real CFlow reachability optimization/runtime. -/");
    puts("");
    puts("namespace CMeta.COptimizerTopologyGenerated");
    puts("");
    puts("structure TopologyWitness where");
    puts("  name : String");
    puts("  beforeSubgraphs : Nat");
    puts("  beforeReachable : Nat");
    puts("  deadOnSubgraphs : Nat");
    puts("  deadOnReachable : Nat");
    puts("  deadOnRemoved : Nat");
    puts("  deadOffSubgraphs : Nat");
    puts("  deadOffReachable : Nat");
    puts("  deadOffRemoved : Nat");
    puts("  reachableBranchRetainedOn : Bool");
    puts("  reachableBranchRetainedOff : Bool");
    puts("  input : List Int");
    puts("  beforeOutput : List Int");
    puts("  deadOnOutput : List Int");
    puts("  deadOffOutput : List Int");
    puts("  deriving Repr, DecidableEq");
    puts("");
    puts("def topologyWitnesses : List TopologyWitness :=");
    puts("  { name := \"dead_subgraph_reachability_i_l\",");
    printf("    beforeSubgraphs := %zu, beforeReachable := %zu,\n",
           before.subgraph_count, reachable_count(&before));
    printf("    deadOnSubgraphs := %zu, deadOnReachable := %zu, deadOnRemoved := %zu,\n",
           dead_on.subgraph_count, reachable_count(&dead_on), on_stats.dead_subgraphs_removed);
    printf("    deadOffSubgraphs := %zu, deadOffReachable := %zu, deadOffRemoved := %zu,\n",
           dead_off.subgraph_count, reachable_count(&dead_off), off_stats.dead_subgraphs_removed);
    printf("    reachableBranchRetainedOn := %s, reachableBranchRetainedOff := %s,\n",
           root_relation_shape_ok(&dead_on) ? "true" : "false",
           root_relation_shape_ok(&dead_off) ? "true" : "false");
    puts("    input := [-2, 0, 3],");
    printf("    beforeOutput := ");
    if (!emit_values(&before_result)) goto done;
    printf(",\n    deadOnOutput := ");
    if (!emit_values(&on_result)) goto done;
    printf(",\n    deadOffOutput := ");
    if (!emit_values(&off_result)) goto done;
    puts(" } ::");
    puts("  []");
    puts("");
    puts("end CMeta.COptimizerTopologyGenerated");
    ok = 1;

done:
    cflow_result_destroy(&before_result);
    cflow_result_destroy(&on_result);
    cflow_result_destroy(&off_result);
    cflow_graph_destroy(&before);
    cflow_graph_destroy(&dead_on);
    cflow_graph_destroy(&dead_off);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
