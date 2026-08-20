#include <cflow/adapters.h>
#include <cflow/opt.h>
#include <cflow/effect.h>
#include <cflow/property.h>
#include <cflow/lower.h>
#include <cflow/stream.h>
#include "ops.h"

#include <math.h>
#include <stdio.h>

static int near(double a, double b) { return fabs(a - b) < 1e-9; }

static size_t total_nodes(const cflow_graph *g) {
    size_t n = 0;
    for (size_t i = 0; i < g->subgraph_count; ++i) n += g->subgraphs[i].node_count;
    return n;
}

static const cflow_node *find_op(const cflow_graph *g, cflow_subgraph_id sgid, cflow_op op) {
    const cflow_subgraph *sg = cflow_graph_subgraph(g, sgid);
    if (!sg) return NULL;
    for (size_t i = 0; i < sg->node_count; ++i)
        if (sg->nodes[i].op == op) return &sg->nodes[i];
    return NULL;
}

int main(void) {
    cflow_stream s;
    cflow_stream_init(&s, &cmeta_type_int);
    s.transform(&s, times_two_transform)  /* int -> long */
     ->map(&s, half);                     /* long -> double */
    if (!cflow_stream_ok(&s) || !cflow_graph_is_normalized(&s.graph)) return 1;

    /* Add one valid but graph-wide unreachable Subgraph. Its own nodes are
     * reachable, so Graph validation still succeeds; DCE should drop it. */
    cflow_subgraph_id dead = cflow_graph_create_subgraph(&s.graph, &cmeta_type_int);
    cflow_node_id dead_map = CMETA_INVALID_ID;
    if (dead == CMETA_INVALID_ID ||
        !cflow_graph_create_node(&s.graph, dead, CFLOW_OP_MAP, times_ten.fn,
                                 NULL, 0, &dead_map) ||
        !cflow_graph_connect(&s.graph, dead, s.graph.subgraphs[dead].entry, 0, dead_map, 0) ||
        !cflow_graph_set_subgraph_exit(&s.graph, dead, dead_map)) return 2;
    const char *error = NULL;
    if (!cflow_graph_validate(&s.graph, &error)) return 3;

    cflow_graph optimized = {0}; optimized.root = CMETA_INVALID_ID;
    cflow_opt_stats stats = {0};
    if (!cflow_graph_optimize(&optimized, &s.graph,
                              (cflow_opt_options){ CMETA_OPT_DEFAULT }, &stats)) return 4;
    if (!cflow_graph_validate(&optimized, &error) || !cflow_graph_is_normalized(&optimized)) return 5;
    if (stats.transforms_canonicalized != 1u || stats.map_nodes_fused != 1u ||
        stats.dead_subgraphs_removed != 1u || optimized.subgraph_count != 1u) return 6;

    const cflow_node *map = find_op(&optimized, optimized.root, CFLOW_OP_MAP);
    if (!map || map->fn_chain_count != 2u || map->op != CFLOW_OP_MAP ||
        !cmeta_type_equal(map->input_type, &cmeta_type_int) ||
        !cmeta_type_equal(map->output_type, &cmeta_type_double)) return 7;
    if (find_op(&optimized, optimized.root, CFLOW_OP_TRANSFORM)) return 8;
    printf("optimization: TRANSFORM canonicalized; two maps fused; dead Subgraph removed\n");
    printf("IR size: subgraphs %zu -> %zu, nodes %zu -> %zu\n",
           stats.subgraphs_before, stats.subgraphs_after,
           stats.nodes_before, stats.nodes_after);

    int inputs[] = {1,2,3};
    cflow_result before = {0}, after = {0};
    if (!cflow_eval_array(&s.graph, inputs, 3, &before) ||
        !cflow_eval_array(&optimized, inputs, 3, &after)) return 9;
    if (before.count != after.count || before.count != 3u ||
        !cmeta_type_equal(before.type, after.type) || !cmeta_type_equal(after.type, &cmeta_type_double)) return 10;
    double *a = (double *)before.data, *b = (double *)after.data;
    for (size_t i = 0; i < 3; ++i)
        if (!near(a[i], b[i]) || !near(b[i], (double)(i + 1u))) return 11;
    printf("semantic equivalence: before/after -> 1 2 3\n");
    cflow_result_destroy(&before); cflow_result_destroy(&after);

    /* Optimizing optimized IR is stable: the fused chain remains one MAP. */
    cflow_graph optimized2 = {0}; optimized2.root = CMETA_INVALID_ID;
    cflow_opt_stats stats2 = {0};
    if (!cflow_graph_optimize(&optimized2, &optimized,
                              (cflow_opt_options){ CMETA_OPT_DEFAULT }, &stats2)) return 12;
    const cflow_node *map2 = find_op(&optimized2, optimized2.root, CFLOW_OP_MAP);
    if (!map2 || map2->fn_chain_count != 2u || stats2.map_nodes_fused != 0u ||
        total_nodes(&optimized2) != total_nodes(&optimized)) return 13;
    printf("optimization idempotence: fused MAP chain remains stable\n");

    /* Lowering an already optimized Graph preserves optimizer-owned MAP chain
     * metadata; normalization remains a structural pass, not a de-optimizer. */
    cflow_graph renorm = {0}; renorm.root = CMETA_INVALID_ID;
    if (!cflow_graph_normalize(&renorm, &optimized)) return 20;
    const cflow_node *rmap = find_op(&renorm, renorm.root, CFLOW_OP_MAP);
    if (!rmap || rmap->fn_chain_count != 2u) return 21;
    printf("lowering boundary: normalized fused MAP chain is preserved\n");

    /* Safe one-branch relation canonicalization. FIRST_RESULT+SELECT has one
     * externally visible first value, so coordination canonicalizes to ANY. */
    cflow_stream branch;
    cflow_stream_init(&branch, &cmeta_type_int);
    branch.map(&branch, square);
    cflow_graph rg; cflow_graph_init(&rg, &cmeta_type_int);
    cflow_relation_schema rs = { CFLOW_REL_COORD_SEQUENCE,
        CFLOW_REL_COMPLETE_FIRST_RESULT, CFLOW_REL_RESULT_SELECT,
        CFLOW_REL_ERROR_FAIL_FAST };
    const cflow_graph *branches[] = { &branch.graph };
    if (!cflow_graph_relation(&rg, branches, 1, rs, (cmeta_callable){0})) return 14;
    cflow_graph ropt = {0}; ropt.root = CMETA_INVALID_ID;
    cflow_opt_stats rstats = {0};
    if (!cflow_graph_optimize(&ropt, &rg,
                              (cflow_opt_options){ CMETA_OPT_DEFAULT }, &rstats)) return 15;
    const cflow_node *rel = find_op(&ropt, ropt.root, CFLOW_OP_RELATION);
    if (!rel || rel->relation.coordination != CFLOW_REL_COORD_ANY ||
        rstats.relation_schemas_simplified != 1u) return 16;
    if (!cflow_eval_array(&rg, inputs, 3, &before) ||
        !cflow_eval_array(&ropt, inputs, 3, &after)) return 17;
    if (before.count != after.count || before.type != after.type) return 18;
    for (size_t i = 0; i < before.count; ++i)
        if (((long *)before.data)[i] != ((long *)after.data)[i]) return 19;
    printf("relation canonicalization: one-branch FIRST_RESULT/SELECT -> ANY\n");
    cflow_result_destroy(&before); cflow_result_destroy(&after);

    /* Effect-aware fusion: an IO-tagged MAP is an optimization barrier even
     * though the callback is deterministic in this reference demo. */
    cflow_stream effectful;
    cflow_stream_init(&effectful, &cmeta_type_int);
    effectful.map(&effectful, io_tagged)->map(&effectful, half);
    if (!(cflow_graph_effects(&effectful.graph) & CMETA_EFFECT_IO)) return 22;
    cflow_graph eopt = {0}; eopt.root = CMETA_INVALID_ID;
    cflow_opt_stats estats = {0};
    if (!cflow_graph_optimize(&eopt, &effectful.graph,
                              (cflow_opt_options){ CMETA_OPT_DEFAULT }, &estats)) return 23;
    const cflow_subgraph *esg = cflow_graph_subgraph(&eopt, eopt.root);
    if (!esg || esg->node_count != 3u || estats.effect_blocked_map_fusions == 0u) return 24;
    if (!cflow_eval_array(&effectful.graph, inputs, 3, &before) ||
        !cflow_eval_array(&eopt, inputs, 3, &after)) return 25;
    if (before.count != after.count) return 26;
    for (size_t i = 0; i < before.count; ++i)
        if (!near(((double *)before.data)[i], ((double *)after.data)[i])) return 27;
    printf("effect-aware fusion: IO map blocks fusion; semantics preserved\n");
    cflow_result_destroy(&before); cflow_result_destroy(&after);

    /* Intrinsic operator effects compose with callback effects. REDUCE is
     * stateful even when its reducer callback is PURE. */
    cflow_stream stateful;
    cflow_stream_init(&stateful, &cmeta_type_long);
    stateful.reduce(&stateful, add_long);
    if (!(cflow_graph_effects(&stateful.graph) & CMETA_EFFECT_STATEFUL)) return 28;
    printf("effect propagation: PURE reducer + REDUCE intrinsic => STATEFUL graph\n");

    /* Effectful relation branches prevent scheduler-sensitive canonicalization. */
    cflow_stream ebranch;
    cflow_stream_init(&ebranch, &cmeta_type_int);
    ebranch.map(&ebranch, io_tagged);
    cflow_graph erg; cflow_graph_init(&erg, &cmeta_type_int);
    const cflow_graph *ebranches[] = { &ebranch.graph };
    if (!cflow_graph_relation(&erg, ebranches, 1, rs, (cmeta_callable){0})) return 29;
    cflow_graph eropt = {0}; eropt.root = CMETA_INVALID_ID;
    cflow_opt_stats erstats = {0};
    if (!cflow_graph_optimize(&eropt, &erg,
                              (cflow_opt_options){ CMETA_OPT_DEFAULT }, &erstats)) return 30;
    const cflow_node *erel = find_op(&eropt, eropt.root, CFLOW_OP_RELATION);
    if (!erel || erel->relation.coordination != CFLOW_REL_COORD_SEQUENCE ||
        erstats.effect_blocked_relation_simplifications != 1u) return 31;
    printf("effect-aware relation simplify: IO branch preserves SEQUENCE policy\n");


    /* Property-aware rewrite: duplicate calls to the exact same PURE,
     * DETERMINISTIC, TOTAL, IDEMPOTENT T->T map may be collapsed. */
    cflow_stream idem;
    cflow_stream_init(&idem, &cmeta_type_int);
    idem.map(&idem, clamp_nonnegative)->map(&idem, clamp_nonnegative);
    cflow_graph iopt = {0}; iopt.root = CMETA_INVALID_ID;
    cflow_opt_stats istats = {0};
    if (!cflow_graph_optimize(&iopt, &idem.graph,
                              (cflow_opt_options){ CMETA_OPT_DEFAULT }, &istats)) return 32;
    const cflow_node *imap = find_op(&iopt, iopt.root, CFLOW_OP_MAP);
    if (!imap || imap->fn_chain_count != 1u || istats.idempotent_maps_eliminated != 1u ||
        !cmeta_properties_include(cflow_node_properties(&iopt, imap),
                                  CMETA_PROP_STABLE | CMETA_PROP_IDEMPOTENT)) return 33;
    int signed_inputs[] = {-3, 0, 4};
    if (!cflow_eval_array(&idem.graph, signed_inputs, 3, &before) ||
        !cflow_eval_array(&iopt, signed_inputs, 3, &after)) return 34;
    if (before.count != 3u || after.count != 3u) return 35;
    for (size_t i = 0; i < 3; ++i)
        if (((int *)before.data)[i] != ((int *)after.data)[i]) return 36;
    printf("property rewrite: IDEMPOTENT clamp o clamp -> one call\n");
    cflow_result_destroy(&before); cflow_result_destroy(&after);

    /* Same implementation shape without an IDEMPOTENT contract is not a
     * proof. Fusion may make one MAP node, but both calls must remain. */
    cflow_stream unproven;
    cflow_stream_init(&unproven, &cmeta_type_int);
    unproven.map(&unproven, clamp_unproven)->map(&unproven, clamp_unproven);
    cflow_graph uopt = {0}; uopt.root = CMETA_INVALID_ID;
    cflow_opt_stats ustats = {0};
    if (!cflow_graph_optimize(&uopt, &unproven.graph,
                              (cflow_opt_options){ CMETA_OPT_DEFAULT }, &ustats)) return 37;
    const cflow_node *umap = find_op(&uopt, uopt.root, CFLOW_OP_MAP);
    if (!umap || umap->fn_chain_count != 2u || ustats.idempotent_maps_eliminated != 0u ||
        ustats.property_blocked_idempotent_eliminations == 0u) return 38;
    printf("property conservatism: undeclared idempotence preserves both calls\n");

    /* PURE alone is insufficient for relation timing canonicalization. */
    cflow_stream pbranch;
    cflow_stream_init(&pbranch, &cmeta_type_int);
    pbranch.map(&pbranch, unproven_square);
    cflow_graph prg; cflow_graph_init(&prg, &cmeta_type_int);
    const cflow_graph *pbranches[] = { &pbranch.graph };
    if (!cflow_graph_relation(&prg, pbranches, 1, rs, (cmeta_callable){0})) return 39;
    cflow_graph propt = {0}; propt.root = CMETA_INVALID_ID;
    cflow_opt_stats prstats = {0};
    if (!cflow_graph_optimize(&propt, &prg,
                              (cflow_opt_options){ CMETA_OPT_DEFAULT }, &prstats)) return 40;
    const cflow_node *prel = find_op(&propt, propt.root, CFLOW_OP_RELATION);
    if (!prel || prel->relation.coordination != CFLOW_REL_COORD_SEQUENCE ||
        prstats.property_blocked_relation_simplifications != 1u) return 41;
    printf("property-aware relation simplify: PURE without STABLE guarantee is preserved\n");

    /* Contract validation rejects an impossible IDEMPOTENT annotation on a
     * non-endomorphic int->long callback. */
    cmeta_callable invalid_prop = square.fn;
    invalid_prop.meta.properties |= CMETA_PROP_IDEMPOTENT;
    cflow_graph invalid_graph;
    cflow_graph_init(&invalid_graph, &cmeta_type_int);
    if (cflow_graph_map(&invalid_graph, invalid_prop)) return 42;
    printf("property contract validation: IDEMPOTENT requires T->T\n");
    cflow_graph_destroy(&invalid_graph);

    cflow_graph_destroy(&propt);
    cflow_graph_destroy(&prg);
    cflow_stream_destroy(&pbranch);
    cflow_graph_destroy(&uopt);
    cflow_stream_destroy(&unproven);
    cflow_graph_destroy(&iopt);
    cflow_stream_destroy(&idem);

    cflow_graph_destroy(&eropt);
    cflow_graph_destroy(&erg);
    cflow_stream_destroy(&ebranch);
    cflow_stream_destroy(&stateful);
    cflow_graph_destroy(&eopt);
    cflow_stream_destroy(&effectful);
    cflow_graph_destroy(&ropt);
    cflow_graph_destroy(&rg);
    cflow_stream_destroy(&branch);
    cflow_graph_destroy(&renorm);
    cflow_graph_destroy(&optimized2);
    cflow_graph_destroy(&optimized);
    cflow_stream_destroy(&s);
    return 0;
}
