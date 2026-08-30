#include <cflow/adapters.h>
#include <cflow/stream.h>
#include "ops.h"

#include <math.h>
#include <stdio.h>

static int near(double a, double b) { return fabs(a - b) < 1e-9; }

int main(void) {
    /* Build a graph in a physical node order that is intentionally different
     * from execution order. Only explicit edges define control/data flow. */
    cflow_graph g;
    cflow_graph_init(&g, &cmeta_type_int);
    if (g.error) return 1;
    cflow_subgraph_id root_id = g.root;
    const cflow_subgraph *root0 = cflow_graph_subgraph(&g, root_id);
    if (!root0 || root0->entry != 0) return 2;

    cflow_node_id half_id = CMETA_INVALID_ID;
    cflow_node_id square_id = CMETA_INVALID_ID;
    if (!cflow_graph_create_node(&g, root_id, CFLOW_OP_MAP, half.fn,
                                 NULL, 0, &half_id)) return 3;
    if (!cflow_graph_create_node(&g, root_id, CFLOW_OP_MAP, square.fn,
                                 NULL, 0, &square_id)) return 4;
    if (half_id != 1 || square_id != 2) return 5;
    if (!cflow_graph_connect(&g, root_id, 0, 0, square_id, 0)) return 6;
    if (!cflow_graph_connect(&g, root_id, square_id, 0, half_id, 0)) return 7;
    if (!cflow_graph_set_subgraph_exit(&g, root_id, half_id)) return 8;

    const char *err = NULL;
    if (!cflow_graph_validate(&g, &err)) {
        fprintf(stderr, "graph validation failed: %s\n", err ? err : "?");
        return 9;
    }

    int input[] = {2, 4, 6};
    cflow_result out = {0};
    if (!cflow_eval_array(&g, input, 3, &out)) return 10;
    if (out.count != 3 || !cmeta_type_equal(out.type, &cmeta_type_double)) return 11;
    double *v = (double *)out.data;
    if (!near(v[0], 2.0) || !near(v[1], 8.0) || !near(v[2], 18.0)) return 12;
    cflow_result_destroy(&out);

    printf("edge-driven Subscription: physical nodes INPUT,half,square; edges execute INPUT->square->half\n");
    cflow_graph_destroy(&g);

    /* The fluent façade composes nested branches by importing them into one
     * Graph-wide Subgraph table. Nodes only retain stable Subgraph IDs. */
    cflow_stream left, right;
    cflow_stream_init(&left, &cmeta_type_int);
    cflow_stream_init(&right, &cmeta_type_int);
    left.map(&left, square);
    right.map(&right, as_double);
    left.zip(&left, &right, merge_long_double);
    if (!cflow_stream_ok(&left)) return 13;

    const cflow_subgraph *lr = cflow_graph_subgraph(&left.graph, left.graph.root);
    const cflow_node *zip = cflow_subgraph_node(lr, lr->tail);
    if (!zip || zip->op != CFLOW_OP_ZIP || zip->subgraph_count != 1) return 14;
    cflow_subgraph_id child_id = zip->subgraphs[0];
    if (child_id == left.graph.root || !cflow_graph_subgraph(&left.graph, child_id)) return 15;
    if (left.graph.subgraph_count < 2) return 16;

    printf("subgraph composition: ZIP node references SG%u in one Graph-wide table (%zu subgraphs)\n",
           (unsigned)child_id, left.graph.subgraph_count);

    cflow_stream_destroy(&left);
    cflow_stream_destroy(&right);
    return 0;
}
