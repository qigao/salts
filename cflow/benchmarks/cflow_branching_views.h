#ifndef CFLOW_BRANCHING_VIEWS_H
#define CFLOW_BRANCHING_VIEWS_H

#include <cflow/graph.h>
#include <rocida/stl/hash_map.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum cflow_branching_view_status {
  CFLOW_BRANCHING_VIEW_OK = 0,
  CFLOW_BRANCHING_VIEW_INVALID_ARGUMENT,
  CFLOW_BRANCHING_VIEW_INVALID_EDGE,
  CFLOW_BRANCHING_VIEW_OVERFLOW,
  CFLOW_BRANCHING_VIEW_ALLOCATION_FAILED
} cflow_branching_view_status;

typedef struct cflow_branching_allocator {
  void *(*allocate)(size_t size, void *context);
  void (*deallocate)(void *pointer, void *context);
  void *context;
} cflow_branching_allocator;

typedef struct cflow_branching_memory {
  size_t allocation_count;
  size_t allocated_bytes;
  size_t retained_bytes;
  size_t peak_bytes;
} cflow_branching_memory;

typedef struct cflow_branching_observation {
  size_t node_count;
  size_t edge_count;
  uint64_t checksum;
  bool ok;
} cflow_branching_observation;

typedef struct cflow_branching_flat_once_view {
  uint64_t *source_checksums;
  size_t node_count;
  cflow_branching_allocator allocator;
  cflow_branching_memory memory;
} cflow_branching_flat_once_view;

typedef struct cflow_branching_pointer_link {
  cflow_node_id target;
  struct cflow_branching_pointer_link *next;
} cflow_branching_pointer_link;

typedef struct cflow_branching_pointer_node {
  cflow_branching_pointer_link *first;
  cflow_branching_pointer_link *last;
} cflow_branching_pointer_node;

typedef struct cflow_branching_pointer_view {
  cflow_branching_pointer_node *nodes;
  cflow_branching_pointer_link *links;
  size_t node_count;
  size_t edge_count;
  cflow_branching_allocator allocator;
  cflow_branching_memory memory;
} cflow_branching_pointer_view;

typedef struct cflow_branching_hash_span {
  size_t offset;
  size_t count;
} cflow_branching_hash_span;

typedef struct cflow_branching_hash_view {
  hash_map_t spans;
  cflow_node_id *targets;
  size_t node_count;
  size_t edge_count;
  cflow_branching_allocator allocator;
  cflow_branching_memory memory;
  bool initialized;
} cflow_branching_hash_view;

typedef struct cflow_branching_csr_view {
  size_t *offsets;
  cflow_node_id *targets;
  size_t node_count;
  size_t edge_count;
  cflow_branching_allocator allocator;
  cflow_branching_memory memory;
} cflow_branching_csr_view;

bool cflow_branching_observations_equal(const cflow_branching_observation *left,
                                        const cflow_branching_observation *right);
cflow_branching_observation cflow_branching_observe_flat_lookup(const cflow_subgraph *subgraph);
cflow_branching_observation cflow_branching_observe_flat_once(cflow_branching_flat_once_view *view,
                                                              const cflow_subgraph *subgraph);
cflow_branching_observation
cflow_branching_observe_pointer(const cflow_branching_pointer_view *view);
cflow_branching_observation cflow_branching_observe_hash(const cflow_branching_hash_view *view);
cflow_branching_observation cflow_branching_observe_csr(const cflow_branching_csr_view *view);

cflow_branching_view_status
cflow_branching_flat_once_build(cflow_branching_flat_once_view *out, const cflow_subgraph *subgraph,
                                const cflow_branching_allocator *allocator);
void cflow_branching_flat_once_destroy(cflow_branching_flat_once_view *view);

cflow_branching_view_status
cflow_branching_pointer_build(cflow_branching_pointer_view *out, const cflow_subgraph *subgraph,
                              const cflow_branching_allocator *allocator);
void cflow_branching_pointer_destroy(cflow_branching_pointer_view *view);

cflow_branching_view_status cflow_branching_hash_build(cflow_branching_hash_view *out,
                                                       const cflow_subgraph *subgraph,
                                                       const cflow_branching_allocator *allocator);
void cflow_branching_hash_destroy(cflow_branching_hash_view *view);

cflow_branching_view_status cflow_branching_csr_build(cflow_branching_csr_view *out,
                                                      const cflow_subgraph *subgraph,
                                                      const cflow_branching_allocator *allocator);
void cflow_branching_csr_destroy(cflow_branching_csr_view *view);

#endif
