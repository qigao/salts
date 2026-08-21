#include <cflow/cflow.h>
#include "tinytest.h"

#include "cflow_test_ops.h"

static bool cflow_test_build_edge_graph(cflow_graph *graph) {
    cflow_node_id half_id = CMETA_INVALID_ID;
    cflow_node_id square_id = CMETA_INVALID_ID;

    cflow_graph_init(graph, &cmeta_type_int);
    if (graph->error) {
        return false;
    }

    return cflow_graph_create_node(graph, graph->root, CFLOW_OP_MAP,
                                   cflow_test_half.fn, NULL, 0u, &half_id) &&
           cflow_graph_create_node(graph, graph->root, CFLOW_OP_MAP,
                                   cflow_test_square.fn, NULL, 0u, &square_id) &&
           cflow_graph_connect(graph, graph->root, 0u, 0u, square_id, 0u) &&
           cflow_graph_connect(graph, graph->root, square_id, 0u, half_id, 0u) &&
           cflow_graph_set_subgraph_exit(graph, graph->root, half_id);
}

suite("CFlow graph") {
    it("executes nodes according to edges rather than storage order") {
        cflow_graph graph = {0};
        cflow_result result = {0};
        const char *error = NULL;
        const int input[] = {2, 4, 6};
        const double *values;

        check_true(cflow_test_build_edge_graph(&graph));
        check_true(cflow_graph_validate(&graph, &error));
        check_null(error);
        check_true(cflow_eval_array(&graph, input, 3u, &result));
        check_equal(result.count, (size_t)3);
        check_true(cmeta_type_equal(result.type, &cmeta_type_double));

        values = result.data;
        check_not_null(values);
        check_equal(values[0], 2.0);
        check_equal(values[1], 8.0);
        check_equal(values[2], 18.0);

        cflow_result_destroy(&result);
        cflow_graph_destroy(&graph);
    }

    it("clones executable graph structure independently") {
        cflow_graph graph = {0};
        cflow_graph clone = {0};

        check_true(cflow_test_build_edge_graph(&graph));
        check_true(cflow_graph_clone(&clone, &graph));
        check_true(cflow_graph_structural_equal(&graph, &clone));
        check_true(graph.subgraphs != clone.subgraphs);
        check_true(graph.subgraphs[graph.root].nodes !=
                   clone.subgraphs[clone.root].nodes);

        cflow_graph_destroy(&clone);
        cflow_graph_destroy(&graph);
    }

    it("rejects invalid edge endpoints without changing topology") {
        cflow_graph graph = {0};
        const char *error = NULL;
        size_t edge_count;

        cflow_graph_init(&graph, &cmeta_type_int);
        check_null(graph.error);
        edge_count = graph.subgraphs[graph.root].edge_count;

        check_false(cflow_graph_connect(&graph, graph.root, 0u, 0u,
                                        CMETA_INVALID_ID, 0u));
        check_equal(graph.error, "invalid edge endpoint");
        check_equal(graph.subgraphs[graph.root].edge_count, edge_count);
        check_true(cflow_graph_validate(&graph, &error));
        check_null(error);

        cflow_graph_destroy(&graph);
    }
}
