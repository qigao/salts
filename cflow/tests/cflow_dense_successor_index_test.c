#include "tinytest.h"

#include "dense_successor_index.h"

suite("CFlow dense successor index") {
    it("derives a linear path independently of edge storage order") {
        const cflow_edge edges[] = {
            {.from = 2u, .to = 3u},
            {.from = 0u, .to = 2u},
            {.from = 3u, .to = 1u},
        };
        const cflow_subgraph subgraph = {
            .node_count = 4u,
            .edges = (cflow_edge *)edges,
            .edge_count = sizeof(edges) / sizeof(edges[0]),
        };
        cflow_dense_successor_index index = {0};
        cflow_node_id successor = CMETA_INVALID_ID;

        check_equal(cflow_dense_successor_index_build(&index, &subgraph),
                    CFLOW_DENSE_SUCCESSOR_INDEX_OK);
        check_false(index.has_fanout);
        check_true(cflow_dense_successor_index_successor(&index, 0u, &successor));
        check_equal(successor, (cflow_node_id)2u);
        check_true(cflow_dense_successor_index_successor(&index, 2u, &successor));
        check_equal(successor, (cflow_node_id)3u);
        check_true(cflow_dense_successor_index_successor(&index, 3u, &successor));
        check_equal(successor, (cflow_node_id)1u);
        check_false(cflow_dense_successor_index_has_successor(&index, 1u));
        check_false(cflow_dense_successor_index_successor(&index, 1u, &successor));

        cflow_dense_successor_index_destroy(&index);
    }

    it("records the lowest fan-out node independent of edge storage order") {
        const cflow_edge edges[] = {
            {.from = 2u, .to = 3u},
            {.from = 0u, .to = 1u},
            {.from = 2u, .to = 1u},
            {.from = 0u, .to = 2u},
        };
        const cflow_subgraph subgraph = {
            .node_count = 4u,
            .edges = (cflow_edge *)edges,
            .edge_count = sizeof(edges) / sizeof(edges[0]),
        };
        cflow_dense_successor_index index = {0};

        check_equal(cflow_dense_successor_index_build(&index, &subgraph),
                    CFLOW_DENSE_SUCCESSOR_INDEX_OK);
        check_true(index.has_fanout);
        check_equal(index.first_fanout, (size_t)0u);

        cflow_dense_successor_index_destroy(&index);
    }

    it("rejects an invalid edge transactionally") {
        const cflow_edge edge = {.from = 0u, .to = 3u};
        const cflow_subgraph subgraph = {
            .node_count = 3u,
            .edges = (cflow_edge *)&edge,
            .edge_count = 1u,
        };
        cflow_dense_successor_index index = {0};

        check_equal(cflow_dense_successor_index_build(&index, &subgraph),
                    CFLOW_DENSE_SUCCESSOR_INDEX_INVALID_EDGE);
        check_null(index.successors);
        check_equal(index.node_count, (size_t)0u);
        check_false(index.has_fanout);
    }

    it("returns to a reusable zero state after destroy") {
        const cflow_edge edge = {.from = 0u, .to = 1u};
        const cflow_subgraph subgraph = {
            .node_count = 2u,
            .edges = (cflow_edge *)&edge,
            .edge_count = 1u,
        };
        cflow_dense_successor_index index = {0};

        check_equal(cflow_dense_successor_index_build(&index, &subgraph),
                    CFLOW_DENSE_SUCCESSOR_INDEX_OK);
        cflow_dense_successor_index_destroy(&index);
        check_null(index.successors);
        check_equal(index.node_count, (size_t)0u);
        check_false(index.has_fanout);
        check_equal(cflow_dense_successor_index_build(&index, &subgraph),
                    CFLOW_DENSE_SUCCESSOR_INDEX_OK);

        cflow_dense_successor_index_destroy(&index);
    }
}
