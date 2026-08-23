#ifndef CFLOW_DENSE_SUCCESSOR_INDEX_H
#define CFLOW_DENSE_SUCCESSOR_INDEX_H

#include <cflow/graph.h>

typedef enum cflow_dense_successor_index_status {
    CFLOW_DENSE_SUCCESSOR_INDEX_OK = 0,
    CFLOW_DENSE_SUCCESSOR_INDEX_INVALID_ARGUMENT,
    CFLOW_DENSE_SUCCESSOR_INDEX_INVALID_EDGE,
    CFLOW_DENSE_SUCCESSOR_INDEX_ALLOCATION_FAILED
} cflow_dense_successor_index_status;

/* Private immutable view derived from one subgraph. Callers own successors
 * until destroy and must not retain the view across Graph mutation. */
typedef struct cflow_dense_successor_index {
    cflow_node_id *successors;
    size_t node_count;
    size_t first_fanout;
    bool has_fanout;
} cflow_dense_successor_index;

/* out must be zero-initialized or previously destroyed. Failure leaves it in
 * the zero state. */
cflow_dense_successor_index_status cflow_dense_successor_index_build(
    cflow_dense_successor_index *out,
    const cflow_subgraph *subgraph);

void cflow_dense_successor_index_destroy(cflow_dense_successor_index *index);

bool cflow_dense_successor_index_has_successor(
    const cflow_dense_successor_index *index,
    cflow_node_id node);

bool cflow_dense_successor_index_successor(
    const cflow_dense_successor_index *index,
    cflow_node_id node,
    cflow_node_id *successor);

#endif
