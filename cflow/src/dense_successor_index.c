#include "dense_successor_index.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void cflow_dense_successor_index_release(cflow_dense_successor_index *index) {
    free(index->successors);
    memset(index, 0, sizeof(*index));
}

cflow_dense_successor_index_status cflow_dense_successor_index_build(
    cflow_dense_successor_index *out,
    const cflow_subgraph *subgraph) {
    cflow_dense_successor_index built = {0};
    size_t bytes;

    if (!out) return CFLOW_DENSE_SUCCESSOR_INDEX_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!subgraph || (subgraph->edge_count && !subgraph->edges) ||
        subgraph->node_count > (size_t)CMETA_INVALID_ID)
        return CFLOW_DENSE_SUCCESSOR_INDEX_INVALID_ARGUMENT;

    built.node_count = subgraph->node_count;
    built.first_fanout = SIZE_MAX;
    if (built.node_count) {
        if (built.node_count > SIZE_MAX / sizeof(*built.successors))
            return CFLOW_DENSE_SUCCESSOR_INDEX_ALLOCATION_FAILED;
        bytes = built.node_count * sizeof(*built.successors);
        built.successors = malloc(bytes);
        if (!built.successors) return CFLOW_DENSE_SUCCESSOR_INDEX_ALLOCATION_FAILED;
        for (size_t node = 0u; node < built.node_count; ++node)
            built.successors[node] = CMETA_INVALID_ID;
    }

    for (size_t edge_id = 0u; edge_id < subgraph->edge_count; ++edge_id) {
        const cflow_edge *edge = &subgraph->edges[edge_id];
        if (edge->from >= built.node_count || edge->to >= built.node_count) {
            cflow_dense_successor_index_release(&built);
            return CFLOW_DENSE_SUCCESSOR_INDEX_INVALID_EDGE;
        }
        if (built.successors[edge->from] == CMETA_INVALID_ID) {
            built.successors[edge->from] = edge->to;
        } else if (!built.has_fanout || edge->from < built.first_fanout) {
            built.has_fanout = true;
            built.first_fanout = edge->from;
        }
    }

    *out = built;
    return CFLOW_DENSE_SUCCESSOR_INDEX_OK;
}

void cflow_dense_successor_index_destroy(cflow_dense_successor_index *index) {
    if (!index) return;
    cflow_dense_successor_index_release(index);
}

bool cflow_dense_successor_index_has_successor(
    const cflow_dense_successor_index *index,
    cflow_node_id node) {
    return index && node < index->node_count && index->successors &&
           index->successors[node] != CMETA_INVALID_ID;
}

bool cflow_dense_successor_index_successor(
    const cflow_dense_successor_index *index,
    cflow_node_id node,
    cflow_node_id *successor) {
    if (!successor || !cflow_dense_successor_index_has_successor(index, node)) return false;
    *successor = index->successors[node];
    return true;
}
