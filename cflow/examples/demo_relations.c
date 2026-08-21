#include <cflow/adapters.h>
#include <cflow/graph.h>
#include <cflow/stream.h>
#include "ops.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int near(double a, double b) { return fabs(a - b) < 1e-9; }

static int eval_longs(const cflow_graph *g, const int *input, size_t n,
                      const long *expect, size_t expect_n) {
    cflow_result out = {0};
    if (!cflow_eval_array(g, input, n, &out)) return 0;
    int ok = out.type == &cmeta_type_long && out.count == expect_n;
    if (ok) {
        long *v = (long *)out.data;
        for (size_t i = 0; i < expect_n; ++i) if (v[i] != expect[i]) ok = 0;
    }
    cflow_result_destroy(&out);
    return ok;
}

int main(void) {
    int input[] = {1,2,3};

    cflow_stream b0, b1, b2;
    cflow_stream_init(&b0, &cmeta_type_int);
    cflow_stream_init(&b1, &cmeta_type_int);
    cflow_stream_init(&b2, &cmeta_type_int);
    b0.map(&b0, square);
    b1.map(&b1, times_ten);
    b2.map(&b2, plus_hundred);
    const cflow_graph *three[] = { &b0.graph, &b1.graph, &b2.graph };

    /* ALL + FOLD replaces the v24 FORK/JOIN pair with one relation node. */
    cflow_graph all;
    cflow_graph_init(&all, &cmeta_type_int);
    if (!cflow_graph_relation(&all, three, 3, cflow_relation_all_fold(), add_long.fn)) return 1;
    if (!cflow_graph_map(&all, half.fn)) return 2;
    cflow_result out = {0};
    if (!cflow_eval_array(&all, input, 3, &out)) return 3;
    double *dv = (double *)out.data;
    if (out.count != 3 || !near(dv[0],56.0) || !near(dv[1],63.0) || !near(dv[2],71.0)) return 4;
    printf("relation ALL/FOLD: %.0f %.0f %.0f\n", dv[0], dv[1], dv[2]);
    cflow_result_destroy(&out);

    const cflow_subgraph *root = cflow_graph_subgraph(&all, all.root);
    size_t relation_count = 0;
    for (size_t i = 0; i < root->node_count; ++i) {
        const cflow_node *n = cflow_subgraph_node(root, (cflow_node_id)i);
        if (n && n->op == CFLOW_OP_RELATION) {
            ++relation_count;
            if (!n->has_relation || n->subgraph_count != 3 ||
                n->relation.coordination != CFLOW_REL_COORD_ALL ||
                n->relation.result != CFLOW_REL_RESULT_FOLD) return 5;
        }
    }
    if (relation_count != 1) return 6;
    printf("static relation IR: one RELATION node + 3 Subgraph IDs\n");

    /* ANY + SELECT: exactly one branch result, no reducer. */
    const cflow_graph *two[] = { &b0.graph, &b1.graph };
    cflow_graph any;
    cflow_graph_init(&any, &cmeta_type_int);
    if (!cflow_graph_relation(&any, two, 2, cflow_relation_any_select(), (cmeta_callable){0})) return 7;
    int one[] = {3};
    if (!cflow_eval_array(&any, one, 1, &out)) return 8;
    if (out.count != 1 || out.type != &cmeta_type_long) return 9;
    long winner = ((long *)out.data)[0];
    if (winner != 9 && winner != 30) return 10;
    printf("relation ANY/SELECT: winner=%ld\n", winner);
    cflow_result_destroy(&out);

    /* SEQUENCE + SELECT streams one result from each branch in branch order. */
    cflow_graph seq;
    cflow_graph_init(&seq, &cmeta_type_int);
    if (!cflow_graph_relation(&seq, two, 2, cflow_relation_sequence_select(), (cmeta_callable){0})) return 11;
    long seq_expect[] = {4,20};
    int x2[] = {2};
    if (!eval_longs(&seq, x2, 1, seq_expect, 2)) return 12;
    printf("relation SEQUENCE/SELECT: 4 20\n");

    /* completion is orthogonal: same SEQUENCE relation, but first result only. */
    cflow_relation_schema first = cflow_relation_sequence_select();
    first.completion = CFLOW_REL_COMPLETE_FIRST_RESULT;
    cflow_graph seq_first;
    cflow_graph_init(&seq_first, &cmeta_type_int);
    if (!cflow_graph_relation(&seq_first, two, 2, first, (cmeta_callable){0})) return 13;
    long first_expect[] = {4};
    if (!eval_longs(&seq_first, x2, 1, first_expect, 1)) return 14;
    printf("relation completion FIRST_RESULT: 4\n");

    /* LATEST + FOLD: one branch updates twice; fold retained latest values. */
    cflow_stream expanding, stable;
    cflow_stream_init(&expanding, &cmeta_type_int);
    cflow_stream_init(&stable, &cmeta_type_int);
    expanding.flatMap(&expanding, expand_long); /* x, 10*x */
    stable.map(&stable, times_ten);              /* 10*x */
    const cflow_graph *latest_branches[] = { &expanding.graph, &stable.graph };
    cflow_graph latest;
    cflow_graph_init(&latest, &cmeta_type_int);
    if (!cflow_graph_relation(&latest, latest_branches, 2,
                              cflow_relation_latest_fold(), add_long.fn)) return 15;
    long latest_expect[] = {22,40};
    if (!eval_longs(&latest, x2, 1, latest_expect, 2)) return 16;
    printf("relation LATEST/FOLD: 22 40\n");

    /* Low-level structured IR: relation node can be placed explicitly from
     * Subgraph IDs; the high-level relation() builder is only a snapshot façade. */
    cflow_graph low;
    cflow_graph_init(&low, &cmeta_type_int);
    cflow_subgraph_id sg_square = cflow_graph_create_subgraph(&low, &cmeta_type_int);
    cflow_subgraph_id sg_ten = cflow_graph_create_subgraph(&low, &cmeta_type_int);
    if (sg_square == CMETA_INVALID_ID || sg_ten == CMETA_INVALID_ID) return 17;
    cflow_node_id nsq = CMETA_INVALID_ID, nten = CMETA_INVALID_ID;
    if (!cflow_graph_create_node(&low, sg_square, CFLOW_OP_MAP, square.fn, NULL, 0, &nsq) ||
        !cflow_graph_connect(&low, sg_square, 0, 0, nsq, 0) ||
        !cflow_graph_set_subgraph_exit(&low, sg_square, nsq)) return 18;
    if (!cflow_graph_create_node(&low, sg_ten, CFLOW_OP_MAP, times_ten.fn, NULL, 0, &nten) ||
        !cflow_graph_connect(&low, sg_ten, 0, 0, nten, 0) ||
        !cflow_graph_set_subgraph_exit(&low, sg_ten, nten)) return 19;
    cflow_subgraph_id branch_ids[] = { sg_square, sg_ten };
    cflow_node_id relnode = CMETA_INVALID_ID;
    if (!cflow_graph_create_relation_node(&low, low.root, &cmeta_type_int,
                                          branch_ids, 2, cflow_relation_all_fold(),
                                          add_long.fn, &relnode) ||
        !cflow_graph_connect(&low, low.root, 0, 0, relnode, 0) ||
        !cflow_graph_set_subgraph_exit(&low, low.root, relnode)) return 20;
    const char *low_err = NULL;
    if (!cflow_graph_validate(&low, &low_err)) return 21;
    long low_expect[] = {24};
    if (!eval_longs(&low, x2, 1, low_expect, 1)) return 22;
    printf("low-level relation IR builder: explicit Subgraph IDs -> RELATION -> 24\n");

    /* Invalid policy combinations are rejected transactionally. */
    cflow_graph tx;
    cflow_graph_init(&tx, &cmeta_type_int);
    size_t before_nodes = cflow_graph_subgraph(&tx, tx.root)->node_count;
    cflow_relation_schema invalid = { CFLOW_REL_COORD_ANY,
        CFLOW_REL_COMPLETE_COORDINATOR, CFLOW_REL_RESULT_FOLD,
        CFLOW_REL_ERROR_FAIL_FAST };
    if (cflow_graph_relation(&tx, two, 2, invalid, add_long.fn)) return 17;
    if (cflow_graph_subgraph(&tx, tx.root)->node_count != before_nodes) return 18;
    printf("relation schema validation: invalid ANY/FOLD rejected transactionally\n");

    /* ALL_DONE is forkJoin-style completion: retain the last value from every
     * branch and emit one folded result only after every child finishes. */
    const cflow_graph *fork_join_branches[] = { &expanding.graph, &stable.graph };
    cflow_graph fork_join;
    cflow_graph_init(&fork_join, &cmeta_type_int);
    if (!cflow_graph_relation(&fork_join, fork_join_branches, 2,
                              cflow_relation_fork_join_fold(), add_long.fn)) return 23;
    long fork_join_expect[] = {40}; /* expanding last=20, stable last=20 */
    if (!eval_longs(&fork_join, x2, 1, fork_join_expect, 1)) return 24;
    printf("relation ALL_DONE/FOLD (forkJoin): 40\n");

    /* Runtime error branch used to prove error policy is orthogonal. */
    cflow_stream failing;
    cflow_stream_init(&failing, &cmeta_type_int);
    failing.flatMap(&failing, fail_long);
    const cflow_graph *fallback_branches[] = { &failing.graph, &b0.graph };

    cflow_graph fail_fast;
    cflow_graph_init(&fail_fast, &cmeta_type_int);
    if (!cflow_graph_relation(&fail_fast, fallback_branches, 2,
                              cflow_relation_sequence_select(), (cmeta_callable){0})) return 25;
    cflow_result failed = {0};
    if (cflow_eval_array(&fail_fast, x2, 1, &failed)) {
        cflow_result_destroy(&failed);
        return 26;
    }
    printf("relation FAIL_FAST: child error propagated\n");

    cflow_graph fallback;
    cflow_graph_init(&fallback, &cmeta_type_int);
    if (!cflow_graph_relation(&fallback, fallback_branches, 2,
                              cflow_relation_fallback(), (cmeta_callable){0})) return 27;
    long fallback_expect[] = {4};
    if (!eval_longs(&fallback, x2, 1, fallback_expect, 1)) return 28;
    printf("relation TRY_NEXT fallback: failed branch skipped -> 4\n");

    cflow_relation_schema ignore_any = cflow_relation_any_select();
    ignore_any.error = CFLOW_REL_ERROR_IGNORE;
    cflow_graph ignore;
    cflow_graph_init(&ignore, &cmeta_type_int);
    if (!cflow_graph_relation(&ignore, fallback_branches, 2, ignore_any, (cmeta_callable){0})) return 29;
    if (!eval_longs(&ignore, x2, 1, fallback_expect, 1)) return 30;
    printf("relation IGNORE + ANY: errored child ignored -> 4\n");

    /* Invalid policy ownership is rejected at IR construction time. */
    cflow_relation_schema bad_try = cflow_relation_any_select();
    bad_try.error = CFLOW_REL_ERROR_TRY_NEXT;
    cflow_graph bad_policy;
    cflow_graph_init(&bad_policy, &cmeta_type_int);
    if (cflow_graph_relation(&bad_policy, fallback_branches, 2, bad_try, (cmeta_callable){0})) return 31;
    printf("relation error schema validation: TRY_NEXT outside SEQUENCE rejected\n");

    cflow_graph_destroy(&bad_policy);
    cflow_graph_destroy(&ignore);
    cflow_graph_destroy(&fallback);
    cflow_graph_destroy(&fail_fast);
    cflow_stream_destroy(&failing);
    cflow_graph_destroy(&fork_join);
    cflow_graph_destroy(&tx);
    cflow_graph_destroy(&low);
    cflow_graph_destroy(&latest);
    cflow_stream_destroy(&expanding); cflow_stream_destroy(&stable);
    cflow_graph_destroy(&seq_first);
    cflow_graph_destroy(&seq);
    cflow_graph_destroy(&any);
    cflow_graph_destroy(&all);
    cflow_stream_destroy(&b0); cflow_stream_destroy(&b1); cflow_stream_destroy(&b2);
    return 0;
}
