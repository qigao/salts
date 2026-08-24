#include "tinytest.h"

#include "cflow_branching_views.h"

#include <stdint.h>
#include <stdlib.h>

typedef struct fail_allocator_state {
  size_t calls;
  size_t frees;
  size_t fail_call;
} fail_allocator_state;

static void *fail_allocate(size_t size, void *context) {
  fail_allocator_state *state = (fail_allocator_state *)context;
  ++state->calls;
  return state->calls == state->fail_call ? NULL : malloc(size);
}

static void fail_deallocate(void *pointer, void *context) {
  fail_allocator_state *state = (fail_allocator_state *)context;
  if (pointer) ++state->frees;
  free(pointer);
}

static cflow_subgraph interleaved_subgraph(void) {
  static cflow_edge edges[] = {
      {.from = 2u, .to = 3u},
      {.from = 0u, .to = 2u},
      {.from = 2u, .to = 1u},
      {.from = 0u, .to = 1u},
  };
  cflow_subgraph subgraph = {0};
  subgraph.node_count = 4u;
  subgraph.edges = edges;
  subgraph.edge_count = sizeof(edges) / sizeof(edges[0]);
  return subgraph;
}

suite("CFlow benchmark-only branching views") {
  it("CSR groups sources while preserving their flat-edge target order") {
    const cflow_subgraph subgraph = interleaved_subgraph();
    const size_t expected_offsets[] = {0u, 2u, 2u, 4u, 4u};
    const cflow_node_id expected_targets[] = {2u, 1u, 3u, 1u};
    cflow_branching_csr_view view = {0};

    check_equal(cflow_branching_csr_build(&view, &subgraph, NULL), CFLOW_BRANCHING_VIEW_OK);
    check_equal(view.offsets, expected_offsets, sizeof(expected_offsets));
    check_equal(view.targets, expected_targets, sizeof(expected_targets));
    check_equal(view.offsets[0], (size_t)0u);
    check_equal(view.offsets[view.node_count], view.edge_count);

    cflow_branching_csr_destroy(&view);
  }

  it("all derived traversals match the flat reference") {
    const cflow_subgraph subgraph = interleaved_subgraph();
    const cflow_branching_observation reference = cflow_branching_observe_flat_lookup(&subgraph);
    cflow_branching_flat_once_view flat_once = {0};
    cflow_branching_pointer_view pointer = {0};
    cflow_branching_hash_view hash = {0};
    cflow_branching_csr_view csr = {0};
    cflow_branching_observation flat_once_observation;
    cflow_branching_observation pointer_observation;
    cflow_branching_observation hash_observation;
    cflow_branching_observation csr_observation;

    check_true(reference.ok);
    check_equal(cflow_branching_flat_once_build(&flat_once, &subgraph, NULL),
                CFLOW_BRANCHING_VIEW_OK);
    check_equal(cflow_branching_pointer_build(&pointer, &subgraph, NULL), CFLOW_BRANCHING_VIEW_OK);
    check_equal(cflow_branching_hash_build(&hash, &subgraph, NULL), CFLOW_BRANCHING_VIEW_OK);
    check_equal(cflow_branching_csr_build(&csr, &subgraph, NULL), CFLOW_BRANCHING_VIEW_OK);

    flat_once_observation = cflow_branching_observe_flat_once(&flat_once, &subgraph);
    pointer_observation = cflow_branching_observe_pointer(&pointer);
    hash_observation = cflow_branching_observe_hash(&hash);
    csr_observation = cflow_branching_observe_csr(&csr);
    check_true(cflow_branching_observations_equal(&reference, &flat_once_observation));
    check_true(cflow_branching_observations_equal(&reference, &pointer_observation));
    check_true(cflow_branching_observations_equal(&reference, &hash_observation));
    check_true(cflow_branching_observations_equal(&reference, &csr_observation));

    cflow_branching_csr_destroy(&csr);
    cflow_branching_hash_destroy(&hash);
    cflow_branching_pointer_destroy(&pointer);
    cflow_branching_flat_once_destroy(&flat_once);
  }

  it("CSR accepts the empty-edge and single-edge boundaries") {
    cflow_edge edge = {.from = 0u, .to = 1u};
    cflow_subgraph empty = {.node_count = 1u};
    cflow_subgraph single = {.node_count = 2u, .edges = &edge, .edge_count = 1u};
    cflow_branching_csr_view view = {0};

    check_equal(cflow_branching_csr_build(&view, &empty, NULL), CFLOW_BRANCHING_VIEW_OK);
    check_equal(view.offsets[0], (size_t)0u);
    check_equal(view.offsets[1], (size_t)0u);
    check_null(view.targets);
    cflow_branching_csr_destroy(&view);

    check_equal(cflow_branching_csr_build(&view, &single, NULL), CFLOW_BRANCHING_VIEW_OK);
    check_equal(view.offsets[0], (size_t)0u);
    check_equal(view.offsets[1], (size_t)1u);
    check_equal(view.offsets[2], (size_t)1u);
    check_equal(view.targets[0], (cflow_node_id)1u);
    cflow_branching_csr_destroy(&view);
  }

  it("CSR rejects an invalid endpoint without publishing ownership") {
    cflow_edge edge = {.from = 0u, .to = 2u};
    cflow_subgraph subgraph = {.node_count = 2u, .edges = &edge, .edge_count = 1u};
    cflow_branching_csr_view view = {0};

    check_equal(cflow_branching_csr_build(&view, &subgraph, NULL),
                CFLOW_BRANCHING_VIEW_INVALID_EDGE);
    check_null(view.offsets);
    check_null(view.targets);
    check_equal(view.node_count, (size_t)0u);
  }

  it("all owned controls reject invalid endpoints without publishing ownership") {
    cflow_edge edge = {.from = 0u, .to = 2u};
    cflow_subgraph subgraph = {.node_count = 2u, .edges = &edge, .edge_count = 1u};
    cflow_branching_flat_once_view flat_once = {0};
    cflow_branching_pointer_view pointer = {0};
    cflow_branching_hash_view hash = {0};

    check_equal(cflow_branching_flat_once_build(&flat_once, &subgraph, NULL),
                CFLOW_BRANCHING_VIEW_INVALID_EDGE);
    check_null(flat_once.source_checksums);
    check_equal(cflow_branching_pointer_build(&pointer, &subgraph, NULL),
                CFLOW_BRANCHING_VIEW_INVALID_EDGE);
    check_null(pointer.nodes);
    check_null(pointer.links);
    check_equal(cflow_branching_hash_build(&hash, &subgraph, NULL),
                CFLOW_BRANCHING_VIEW_INVALID_EDGE);
    check_false(hash.initialized);
    check_null(hash.targets);
  }

  it("CSR rejects target-byte overflow before reading fabricated edges") {
    cflow_edge edge = {.from = 0u, .to = 0u};
    cflow_subgraph subgraph = {.node_count = 1u, .edges = &edge, .edge_count = SIZE_MAX};
    cflow_branching_csr_view view = {0};

    check_equal(cflow_branching_csr_build(&view, &subgraph, NULL), CFLOW_BRANCHING_VIEW_OVERFLOW);
    check_null(view.offsets);
    check_null(view.targets);
  }

  it("CSR frees its first allocation when the second allocation fails") {
    const cflow_subgraph subgraph = interleaved_subgraph();
    fail_allocator_state state = {.fail_call = 2u};
    const cflow_branching_allocator allocator = {fail_allocate, fail_deallocate, &state};
    cflow_branching_csr_view view = {0};

    check_equal(cflow_branching_csr_build(&view, &subgraph, &allocator),
                CFLOW_BRANCHING_VIEW_ALLOCATION_FAILED);
    check_equal(state.calls, (size_t)2u);
    check_equal(state.frees, (size_t)1u);
    check_null(view.offsets);
    check_null(view.targets);
  }

  it("pointer and HashMap controls release the first allocation on failure") {
    const cflow_subgraph subgraph = interleaved_subgraph();
    fail_allocator_state pointer_state = {.fail_call = 2u};
    fail_allocator_state hash_state = {.fail_call = 2u};
    const cflow_branching_allocator pointer_allocator = {fail_allocate, fail_deallocate,
                                                         &pointer_state};
    const cflow_branching_allocator hash_allocator = {fail_allocate, fail_deallocate, &hash_state};
    cflow_branching_pointer_view pointer = {0};
    cflow_branching_hash_view hash = {0};

    check_equal(cflow_branching_pointer_build(&pointer, &subgraph, &pointer_allocator),
                CFLOW_BRANCHING_VIEW_ALLOCATION_FAILED);
    check_equal(pointer_state.calls, (size_t)2u);
    check_equal(pointer_state.frees, (size_t)1u);
    check_null(pointer.nodes);
    check_null(pointer.links);

    check_equal(cflow_branching_hash_build(&hash, &subgraph, &hash_allocator),
                CFLOW_BRANCHING_VIEW_ALLOCATION_FAILED);
    check_equal(hash_state.calls, (size_t)2u);
    check_equal(hash_state.frees, (size_t)1u);
    check_false(hash.initialized);
    check_null(hash.targets);
  }

  it("control views report exact allocator-request formulas") {
    const cflow_subgraph subgraph = interleaved_subgraph();
    const size_t flat_bytes = subgraph.node_count * sizeof(uint64_t);
    const size_t pointer_bytes = subgraph.node_count * sizeof(cflow_branching_pointer_node) +
                                 subgraph.edge_count * sizeof(cflow_branching_pointer_link);
    cflow_branching_flat_once_view flat_once = {0};
    cflow_branching_pointer_view pointer = {0};
    cflow_branching_hash_view hash = {0};
    size_t hash_map_bytes;
    size_t hash_map_alignment;
    size_t hash_key_bytes;
    size_t hash_value_bytes;
    size_t hash_transient_bytes;
    size_t hash_allocated_bytes;
    size_t hash_retained_bytes;

    check_equal(cflow_branching_flat_once_build(&flat_once, &subgraph, NULL),
                CFLOW_BRANCHING_VIEW_OK);
    check_equal(flat_once.memory.allocation_count, (size_t)1u);
    check_equal(flat_once.memory.allocated_bytes, flat_bytes);
    check_equal(flat_once.memory.retained_bytes, flat_bytes);
    check_equal(flat_once.memory.peak_bytes, flat_bytes);

    check_equal(cflow_branching_pointer_build(&pointer, &subgraph, NULL), CFLOW_BRANCHING_VIEW_OK);
    check_equal(pointer.memory.allocation_count, (size_t)2u);
    check_equal(pointer.memory.allocated_bytes, pointer_bytes);
    check_equal(pointer.memory.retained_bytes, pointer_bytes);
    check_equal(pointer.memory.peak_bytes, pointer_bytes);

    check_equal(cflow_branching_hash_build(&hash, &subgraph, NULL), CFLOW_BRANCHING_VIEW_OK);
    hash_map_alignment = _Alignof(size_t);
    if (hash.spans.key_align > hash_map_alignment) hash_map_alignment = hash.spans.key_align;
    if (hash.spans.value_align > hash_map_alignment) hash_map_alignment = hash.spans.value_align;
    hash_map_bytes = (size_t)(hash.spans.values - hash.spans.states) +
                     hash.spans.capacity * hash.spans.value_stride + sizeof(void *) +
                     hash_map_alignment - 1u;
    hash_key_bytes = hash.spans.key_stride + sizeof(void *) + hash.spans.key_align - 1u;
    hash_value_bytes = hash.spans.value_stride + sizeof(void *) + hash.spans.value_align - 1u;
    hash_transient_bytes = hash_key_bytes + hash_value_bytes;
    hash_retained_bytes = subgraph.edge_count * sizeof(cflow_node_id) + hash_map_bytes;
    hash_allocated_bytes = (subgraph.node_count + 1u) * sizeof(size_t) + hash_retained_bytes +
                           2u * hash_transient_bytes;
    check_equal(hash.memory.allocation_count, (size_t)7u);
    check_equal(hash.memory.allocated_bytes, hash_allocated_bytes);
    check_equal(hash.memory.retained_bytes, hash_retained_bytes);
    check_equal(hash.memory.peak_bytes, (subgraph.node_count + 1u) * sizeof(size_t) +
                                            hash_retained_bytes + hash_transient_bytes);

    cflow_branching_hash_destroy(&hash);
    cflow_branching_pointer_destroy(&pointer);
    cflow_branching_flat_once_destroy(&flat_once);
  }

  it("CSR reports exact requested payload and returns to zero on destroy") {
    const cflow_subgraph subgraph = interleaved_subgraph();
    const size_t expected_bytes =
        (subgraph.node_count + 1u) * sizeof(size_t) + subgraph.edge_count * sizeof(cflow_node_id);
    cflow_branching_csr_view view = {0};

    check_equal(cflow_branching_csr_build(&view, &subgraph, NULL), CFLOW_BRANCHING_VIEW_OK);
    check_equal(view.memory.allocation_count, (size_t)2u);
    check_equal(view.memory.allocated_bytes, expected_bytes);
    check_equal(view.memory.retained_bytes, expected_bytes);
    check_equal(view.memory.peak_bytes, expected_bytes);
    cflow_branching_csr_destroy(&view);
    check_null(view.offsets);
    check_null(view.targets);
    check_equal(view.memory.retained_bytes, (size_t)0u);

    check_equal(cflow_branching_csr_build(&view, &subgraph, NULL), CFLOW_BRANCHING_VIEW_OK);
    cflow_branching_csr_destroy(&view);
  }
}
