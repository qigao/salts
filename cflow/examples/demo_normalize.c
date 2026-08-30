#include <cflow/adapters.h>
#include <cflow/lower.h>
#include <cflow/reactive.h>
#include <cflow/publishers.h>
#include <cflow/stream.h>
#include "ops.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int near(double a, double b) { return fabs(a - b) < 1e-9; }

typedef struct one_value {
    double value;
    size_t count;
    int done;
    int errors;
} one_value;

static bool on_value(void *user, const cmeta_type_desc *type, const void *value) {
    one_value *o = (one_value *)user;
    if (!cmeta_type_equal(type, &cmeta_type_double)) return false;
    o->value = *(const double *)value;
    ++o->count;
    return true;
}
static void on_error(void *user, const char *message) {
    (void)message; ++((one_value *)user)->errors;
}
static void on_done(void *user) { ((one_value *)user)->done = 1; }

static size_t count_op(const cflow_graph *g, cflow_op op) {
    size_t count = 0;
    for (size_t s = 0; s < g->subgraph_count; ++s)
        for (size_t n = 0; n < g->subgraphs[s].node_count; ++n)
            if (g->subgraphs[s].nodes[n].op == op) ++count;
    return count;
}

int main(void) {
    cflow_stream left, right;
    cflow_stream_init(&left, &cmeta_type_int);
    cflow_stream_init(&right, &cmeta_type_int);
    left.map(&left, square);       /* left prefix: int -> long */
    right.map(&right, as_double);  /* root branch: int -> double */
    left.zip(&left, &right, merge_long_double);
    if (!cflow_stream_ok(&left) || !cflow_stream_ok(&right)) return 1;
    if (cflow_graph_is_normalized(&left.graph) || count_op(&left.graph, CFLOW_OP_ZIP) != 1u) return 2;

    /* Subscription is deliberately a normalized-IR interpreter, not a surface-IR
     * interpreter. It must reject the high-level ZIP graph. */
    cflow_scheduler loop = {0};
    cflow_publisher publisher = {0};
    cflow_subscription subscription = {0};
    one_value state = {0};
    cflow_subscriber_callbacks observer_cb = { on_value, on_error, on_done, &state };
    cflow_subscriber observer = cflow_subscriber_from_callbacks(&observer_cb);
    int x = 2;
    if (!cflow_scheduler_test_init(&loop)) return 3;
    if (!cflow_publisher_from_array(&publisher, &cmeta_type_int, &x, 1)) return 4;
    if (cflow_subscribe(&subscription, &left.graph, &publisher, &loop, &observer)) return 5;
    cflow_publisher_destroy(&publisher);
    cflow_scheduler_destroy(&loop);
    printf("surface IR: ZIP present; generic Subscription rejects unnormalized graph\n");

    cflow_graph normalized = {0};
    normalized.root = CMETA_INVALID_ID;
    if (!cflow_graph_normalize(&normalized, &left.graph)) return 6;
    if (!cflow_graph_is_normalized(&normalized) || count_op(&normalized, CFLOW_OP_ZIP) != 0u) return 7;
    if (count_op(&normalized, CFLOW_OP_RELATION) == 0u) return 8;

    const cflow_subgraph *root = cflow_graph_subgraph(&normalized, normalized.root);
    const cflow_node *rel = NULL;
    for (size_t i = 0; i < root->node_count; ++i) {
        if (root->nodes[i].op == CFLOW_OP_RELATION) { rel = &root->nodes[i]; break; }
    }
    if (!rel || rel->relation.result != CFLOW_REL_RESULT_INVOKE || rel->subgraph_count != 2u ||
        !rel->has_fn || !cmeta_type_equal(rel->output_type, &cmeta_type_double)) return 9;
    if (!cmeta_type_equal(cflow_subgraph_output_type(&normalized, rel->subgraphs[0]), &cmeta_type_long) ||
        !cmeta_type_equal(cflow_subgraph_output_type(&normalized, rel->subgraphs[1]), &cmeta_type_double)) return 10;
    printf("normalized IR: ZIP -> RELATION(ALL+INVOKE), branches long + double -> double\n");

    memset(&state, 0, sizeof(state));
    if (!cflow_scheduler_test_init(&loop)) return 11;
    if (!cflow_publisher_from_array(&publisher, &cmeta_type_int, &x, 1)) return 12;
    if (!cflow_subscribe(&subscription, &normalized, &publisher, &loop, &observer)) return 13;
    if (!cflow_subscription_request(&subscription, 1)) return 14;
    (void)cflow_scheduler_run_until_idle(&loop, 0);
    if (state.errors || !state.done || state.count != 1u || !near(state.value, 6.25)) return 15;
    printf("normalized Subscription: value=%.2f\n", state.value);
    cflow_subscription_close(&subscription);
    cflow_scheduler_destroy(&loop);

    /* Collection is a façade and may normalize surface IR for convenience. */
    int inputs[] = {1,2,3};
    cflow_result result = {0};
    if (!cflow_eval_array(&left.graph, inputs, 3, &result)) return 16;
    if (result.count != 3 || result.type != &cmeta_type_double) return 17;
    double *v = (double *)result.data;
    if (!near(v[0],2.25) || !near(v[1],6.25) || !near(v[2],12.25)) return 18;
    printf("collection facade: surface Graph auto-normalizes -> 2.25 6.25 12.25\n");
    cflow_result_destroy(&result);

    /* Normalization is stable: a normalized Graph can be normalized again and
     * remains free of high-level ZIP nodes. */
    cflow_graph normalized2 = {0}; normalized2.root = CMETA_INVALID_ID;
    if (!cflow_graph_normalize(&normalized2, &normalized) ||
        !cflow_graph_is_normalized(&normalized2) || count_op(&normalized2, CFLOW_OP_ZIP)) return 19;
    if (!cflow_eval_array(&normalized2, inputs, 3, &result)) return 20;
    v = (double *)result.data;
    if (result.count != 3 || !near(v[2],12.25)) return 21;
    printf("normalization idempotence: normalized Graph lowers to the same primitive semantics\n");
    cflow_result_destroy(&result);



    /* Graph-level lowering regression: the outer ZIP references a right
     * Subgraph that itself still contains a surface ZIP.  Normalization must
     * recurse through Graph/Subgraph topology and eliminate both high-level
     * ZIP nodes, not just rewrite one local node. */
    cflow_stream outer, nested_right, nested_leaf;
    cflow_stream_init(&outer, &cmeta_type_int);
    cflow_stream_init(&nested_right, &cmeta_type_int);
    cflow_stream_init(&nested_leaf, &cmeta_type_int);
    outer.map(&outer, square);               /* int -> long */
    nested_right.map(&nested_right, square); /* int -> long */
    nested_leaf.map(&nested_leaf, as_double);/* int -> double */
    nested_right.zip(&nested_right, &nested_leaf, merge_long_double); /* -> double */
    outer.zip(&outer, &nested_right, merge_long_double);              /* -> double */
    if (!cflow_stream_ok(&outer) || !cflow_stream_ok(&nested_right) ||
        !cflow_stream_ok(&nested_leaf)) return 22;
    if (count_op(&outer.graph, CFLOW_OP_ZIP) < 2u) return 23;

    cflow_graph nested_norm = {0}; nested_norm.root = CMETA_INVALID_ID;
    if (!cflow_graph_normalize(&nested_norm, &outer.graph)) return 24;
    if (!cflow_graph_is_normalized(&nested_norm) || count_op(&nested_norm, CFLOW_OP_ZIP) != 0u)
        return 25;
    if (count_op(&nested_norm, CFLOW_OP_RELATION) < 2u) return 26;
    if (!cflow_eval_array(&outer.graph, inputs, 3, &result)) return 27;
    v = (double *)result.data;
    if (result.count != 3 || !near(v[0],3.25) || !near(v[1],10.25) || !near(v[2],21.25))
        return 28;
    cflow_result_destroy(&result);
    if (!cflow_eval_array(&nested_norm, inputs, 3, &result)) return 29;
    v = (double *)result.data;
    if (result.count != 3 || !near(v[0],3.25) || !near(v[1],10.25) || !near(v[2],21.25))
        return 30;
    printf("graph-level lowering: nested ZIPs -> nested RELATIONs, trace preserved 3.25 10.25 21.25\n");
    cflow_result_destroy(&result);
    cflow_graph_destroy(&nested_norm);
    cflow_stream_destroy(&outer);
    cflow_stream_destroy(&nested_right);
    cflow_stream_destroy(&nested_leaf);

    cflow_graph_destroy(&normalized2);
    cflow_graph_destroy(&normalized);
    cflow_stream_destroy(&left);
    cflow_stream_destroy(&right);
    return 0;
}
