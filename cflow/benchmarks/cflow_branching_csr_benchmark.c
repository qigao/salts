#define TINYTEST_NO_MAIN
#include "tinytest.h"

#include "cflow_branching_views.h"
#include "dense_successor_index.h"

#include <cflow/cflow.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  CFLOW_BRANCHING_MAX_VERTICES = 4096,
  CFLOW_BRANCHING_MAX_EDGES = 8190,
  CFLOW_BRANCHING_TYPICAL_VERTICES = 256,
  CFLOW_BRANCHING_TYPICAL_EDGES = 384,
  CFLOW_BRANCHING_FANOUT_VERTICES = 257,
  CFLOW_BRANCHING_FANOUT_EDGES = 256,
  CFLOW_BRANCHING_SKEWED_VERTICES = 1024,
  CFLOW_BRANCHING_SKEWED_EDGES = 2046,
  CFLOW_BRANCHING_NESTED_LEAVES = 8,
  CFLOW_BRANCHING_NESTED_INNERS = 2,
  CFLOW_BRANCHING_NESTED_LEAF_OPERATORS = 32,
  CFLOW_BRANCHING_BOUNDARY_SETUP_SAMPLES = 1000,
  CFLOW_BRANCHING_BOUNDARY_TRAVERSAL_SAMPLES = 32,
  CFLOW_BRANCHING_BOUNDARY_TRAVERSAL_REPETITIONS = 1024,
  CFLOW_BRANCHING_TYPICAL_SETUP_SAMPLES = 100,
  CFLOW_BRANCHING_LARGE_SETUP_SAMPLES = 10,
  CFLOW_BRANCHING_PEAK_SETUP_SAMPLES = 3,
  CFLOW_BRANCHING_TYPICAL_TRAVERSAL_SAMPLES = 16,
  CFLOW_BRANCHING_TYPICAL_TRAVERSAL_REPETITIONS = 4,
  CFLOW_BRANCHING_LARGE_TRAVERSAL_SAMPLES = 4,
  CFLOW_BRANCHING_PEAK_TRAVERSAL_SAMPLES = 1,
  CFLOW_BRANCHING_NESTED_SAMPLES = 200,
  CFLOW_BRANCHING_NESTED_REPETITIONS = 64
};

typedef enum cflow_branching_shape {
  CFLOW_BRANCHING_EMPTY,
  CFLOW_BRANCHING_SINGLE,
  CFLOW_BRANCHING_TYPICAL,
  CFLOW_BRANCHING_HIGH_FANOUT,
  CFLOW_BRANCHING_SKEWED,
  CFLOW_BRANCHING_PEAK
} cflow_branching_shape;

typedef struct cflow_branching_fixture {
  cflow_edge edges[CFLOW_BRANCHING_MAX_EDGES];
  cflow_subgraph subgraph;
} cflow_branching_fixture;

typedef struct cflow_branching_case_views {
  cflow_branching_flat_once_view flat_once;
  cflow_branching_pointer_view pointer;
  cflow_branching_hash_view hash;
  cflow_branching_csr_view csr;
} cflow_branching_case_views;

typedef struct cflow_branching_nested_fixture {
  cflow_graph leaves[CFLOW_BRANCHING_NESTED_LEAVES];
  cflow_graph inners[CFLOW_BRANCHING_NESTED_INNERS];
  cflow_graph root;
} cflow_branching_nested_fixture;

static cflow_branching_fixture cflow_branching_fixture_state;
static cflow_branching_nested_fixture cflow_branching_nested_state;
static volatile uint64_t cflow_branching_benchmark_sink;

static bool benchmark_checked_add(size_t left, size_t right, size_t *out) {
  if (!out || right > SIZE_MAX - left) return false;
  *out = left + right;
  return true;
}

static bool benchmark_checked_mul(size_t left, size_t right, size_t *out) {
  if (!out || (left && right > SIZE_MAX / left)) return false;
  *out = left * right;
  return true;
}

typed_decl(map, cflow_graph_path_identity);
typed_decl(filter, cflow_graph_path_keep);

static bool fixture_add_edge(cflow_branching_fixture *fixture, cflow_node_id from,
                             cflow_node_id to) {
  size_t edge_count;
  if (!fixture || fixture->subgraph.edge_count >= CFLOW_BRANCHING_MAX_EDGES ||
      from >= fixture->subgraph.node_count || to >= fixture->subgraph.node_count)
    return false;
  edge_count = fixture->subgraph.edge_count;
  fixture->edges[edge_count] = (cflow_edge){.from = from, .to = to};
  fixture->subgraph.edge_count = edge_count + 1u;
  return true;
}

static bool build_typical_fixture(cflow_branching_fixture *fixture) {
  size_t source;
  if (!fixture || fixture->subgraph.node_count != CFLOW_BRANCHING_TYPICAL_VERTICES) return false;
  for (source = 0u; source + 1u < fixture->subgraph.node_count; ++source) {
    if (!fixture_add_edge(fixture, (cflow_node_id)source, (cflow_node_id)(source + 1u)))
      return false;
    if (source < CFLOW_BRANCHING_TYPICAL_EDGES - (CFLOW_BRANCHING_TYPICAL_VERTICES - 1u) &&
        source + 2u < fixture->subgraph.node_count &&
        !fixture_add_edge(fixture, (cflow_node_id)source, (cflow_node_id)(source + 2u)))
      return false;
  }
  return fixture->subgraph.edge_count == CFLOW_BRANCHING_TYPICAL_EDGES;
}

static bool cflow_branching_fixture_init(cflow_branching_fixture *fixture,
                                         cflow_branching_shape shape) {
  size_t target;
  size_t source;
  if (!fixture) return false;
  memset(fixture, 0, sizeof(*fixture));
  fixture->subgraph.edges = fixture->edges;

  switch (shape) {
  case CFLOW_BRANCHING_EMPTY:
    fixture->subgraph.node_count = 1u;
    return true;
  case CFLOW_BRANCHING_SINGLE:
    fixture->subgraph.node_count = 2u;
    return fixture_add_edge(fixture, 0u, 1u);
  case CFLOW_BRANCHING_TYPICAL:
    fixture->subgraph.node_count = CFLOW_BRANCHING_TYPICAL_VERTICES;
    return build_typical_fixture(fixture);
  case CFLOW_BRANCHING_HIGH_FANOUT:
    fixture->subgraph.node_count = CFLOW_BRANCHING_FANOUT_VERTICES;
    for (target = 1u; target < fixture->subgraph.node_count; ++target)
      if (!fixture_add_edge(fixture, 0u, (cflow_node_id)target)) return false;
    return fixture->subgraph.edge_count == CFLOW_BRANCHING_FANOUT_EDGES;
  case CFLOW_BRANCHING_SKEWED:
    fixture->subgraph.node_count = CFLOW_BRANCHING_SKEWED_VERTICES;
    for (target = 1u; target < fixture->subgraph.node_count; ++target)
      if (!fixture_add_edge(fixture, 0u, (cflow_node_id)target)) return false;
    if (!fixture_add_edge(fixture, 1u, 2u) || !fixture_add_edge(fixture, 1u, 3u)) return false;
    for (source = 2u; source + 1u < fixture->subgraph.node_count; ++source)
      if (!fixture_add_edge(fixture, (cflow_node_id)source, (cflow_node_id)(source + 1u)))
        return false;
    return fixture->subgraph.edge_count == CFLOW_BRANCHING_SKEWED_EDGES;
  case CFLOW_BRANCHING_PEAK:
    fixture->subgraph.node_count = CFLOW_BRANCHING_MAX_VERTICES;
    if (!fixture_add_edge(fixture, 0u, 1u) || !fixture_add_edge(fixture, 0u, 2u) ||
        !fixture_add_edge(fixture, 0u, 3u))
      return false;
    for (source = 1u; source + 2u < fixture->subgraph.node_count; ++source) {
      if (!fixture_add_edge(fixture, (cflow_node_id)source, (cflow_node_id)(source + 1u)) ||
          !fixture_add_edge(fixture, (cflow_node_id)source, (cflow_node_id)(source + 2u)))
        return false;
    }
    return fixture_add_edge(fixture, (cflow_node_id)(fixture->subgraph.node_count - 2u),
                            (cflow_node_id)(fixture->subgraph.node_count - 1u)) &&
           fixture->subgraph.edge_count == CFLOW_BRANCHING_MAX_EDGES;
  }
  return false;
}

static void case_views_destroy(cflow_branching_case_views *views) {
  if (!views) return;
  cflow_branching_csr_destroy(&views->csr);
  cflow_branching_hash_destroy(&views->hash);
  cflow_branching_pointer_destroy(&views->pointer);
  cflow_branching_flat_once_destroy(&views->flat_once);
}

static bool case_views_build(cflow_branching_case_views *views, const cflow_subgraph *subgraph) {
  if (!views || !subgraph) return false;
  memset(views, 0, sizeof(*views));
  if (cflow_branching_flat_once_build(&views->flat_once, subgraph, NULL) !=
          CFLOW_BRANCHING_VIEW_OK ||
      cflow_branching_pointer_build(&views->pointer, subgraph, NULL) != CFLOW_BRANCHING_VIEW_OK ||
      cflow_branching_hash_build(&views->hash, subgraph, NULL) != CFLOW_BRANCHING_VIEW_OK ||
      cflow_branching_csr_build(&views->csr, subgraph, NULL) != CFLOW_BRANCHING_VIEW_OK) {
    case_views_destroy(views);
    return false;
  }
  return true;
}

static bool case_views_equivalent(cflow_branching_case_views *views, const cflow_subgraph *subgraph,
                                  cflow_branching_observation *reference) {
  cflow_branching_observation flat_once;
  cflow_branching_observation pointer;
  cflow_branching_observation hash;
  cflow_branching_observation csr;
  if (!views || !subgraph || !reference) return false;
  *reference = cflow_branching_observe_flat_lookup(subgraph);
  flat_once = cflow_branching_observe_flat_once(&views->flat_once, subgraph);
  pointer = cflow_branching_observe_pointer(&views->pointer);
  hash = cflow_branching_observe_hash(&views->hash);
  csr = cflow_branching_observe_csr(&views->csr);
  return cflow_branching_observations_equal(reference, &flat_once) &&
         cflow_branching_observations_equal(reference, &pointer) &&
         cflow_branching_observations_equal(reference, &hash) &&
         cflow_branching_observations_equal(reference, &csr);
}

static void print_memory_row(const char *case_name, const char *view_name,
                             const cflow_branching_memory *memory) {
  printf("CSR_MEMORY|%s|%s|allocations=%zu|allocated=%zu|retained=%zu|peak=%zu\n", case_name,
         view_name, memory->allocation_count, memory->allocated_bytes, memory->retained_bytes,
         memory->peak_bytes);
}

static void print_case_memory(const char *case_name, const cflow_branching_case_views *views) {
  const cflow_branching_memory flat = {0};
  print_memory_row(case_name, "flat-lookup", &flat);
  print_memory_row(case_name, "one-pass-flat-workspace", &views->flat_once.memory);
  print_memory_row(case_name, "pointer-adjacency", &views->pointer.memory);
  print_memory_row(case_name, "CSTL-HashMap", &views->hash.memory);
  print_memory_row(case_name, "CSR", &views->csr.memory);
}

static bool build_flat_once_once(const cflow_subgraph *subgraph) {
  cflow_branching_flat_once_view view = {0};
  if (cflow_branching_flat_once_build(&view, subgraph, NULL) != CFLOW_BRANCHING_VIEW_OK)
    return false;
  cflow_branching_flat_once_destroy(&view);
  return true;
}

static bool build_pointer_once(const cflow_subgraph *subgraph) {
  cflow_branching_pointer_view view = {0};
  if (cflow_branching_pointer_build(&view, subgraph, NULL) != CFLOW_BRANCHING_VIEW_OK) return false;
  cflow_branching_pointer_destroy(&view);
  return true;
}

static bool build_hash_once(const cflow_subgraph *subgraph) {
  cflow_branching_hash_view view = {0};
  if (cflow_branching_hash_build(&view, subgraph, NULL) != CFLOW_BRANCHING_VIEW_OK) return false;
  cflow_branching_hash_destroy(&view);
  return true;
}

static bool build_csr_once(const cflow_subgraph *subgraph) {
  cflow_branching_csr_view view = {0};
  if (cflow_branching_csr_build(&view, subgraph, NULL) != CFLOW_BRANCHING_VIEW_OK) return false;
  cflow_branching_csr_destroy(&view);
  return true;
}

#define CFLOW_BRANCHING_SETUP_CASE(label, samples, subgraph)                                       \
  do {                                                                                             \
    bool flat_once_ok = true;                                                                      \
    bool pointer_ok = true;                                                                        \
    bool hash_ok = true;                                                                           \
    bool csr_ok = true;                                                                            \
    benchmark_batch(label " / one-pass flat workspace build + destroy", samples) {                 \
      if (!build_flat_once_once(subgraph)) flat_once_ok = false;                                   \
    }                                                                                              \
    benchmark_batch(label " / pointer adjacency build + destroy", samples) {                       \
      if (!build_pointer_once(subgraph)) pointer_ok = false;                                       \
    }                                                                                              \
    benchmark_batch(label " / CSTL HashMap build + destroy", samples) {                        \
      if (!build_hash_once(subgraph)) hash_ok = false;                                             \
    }                                                                                              \
    benchmark_batch(label " / CSR build + destroy", samples) {                                     \
      if (!build_csr_once(subgraph)) csr_ok = false;                                               \
    }                                                                                              \
    check_true(flat_once_ok);                                                                      \
    check_true(pointer_ok);                                                                        \
    check_true(hash_ok);                                                                           \
    check_true(csr_ok);                                                                            \
  } while (0)

#define CFLOW_BRANCHING_TRAVERSAL_ROW(title, samples, repetitions, edges, expression, expected)    \
  do {                                                                                             \
    cflow_branching_observation measured = {0};                                                    \
    size_t repetition;                                                                             \
    benchmark_ops(title, samples, (edges) * (repetitions)) {                                       \
      for (repetition = 0u; repetition < (repetitions); ++repetition) {                            \
        measured = (expression);                                                                   \
        cflow_branching_benchmark_sink ^= measured.checksum;                                       \
      }                                                                                            \
    }                                                                                              \
    check_true(cflow_branching_observations_equal(&measured, &(expected)));                        \
  } while (0)

#define CFLOW_BRANCHING_TRAVERSAL_CASE(label, samples, repetitions, fixture, views, reference)     \
  do {                                                                                             \
    const size_t edge_count_ = (fixture)->subgraph.edge_count;                                     \
    const size_t work_units_ = edge_count_ ? edge_count_ : 1u;                                     \
    CFLOW_BRANCHING_TRAVERSAL_ROW(                                                                 \
        label " / current Graph flat APIs", samples, repetitions, work_units_,                     \
        cflow_branching_observe_flat_lookup(&(fixture)->subgraph), reference);                     \
    CFLOW_BRANCHING_TRAVERSAL_ROW(                                                                 \
        label " / one-pass flat edges + V scratch", samples, repetitions, work_units_,             \
        cflow_branching_observe_flat_once(&(views)->flat_once, &(fixture)->subgraph), reference);  \
    CFLOW_BRANCHING_TRAVERSAL_ROW(label " / pointer adjacency", samples, repetitions, work_units_, \
                                  cflow_branching_observe_pointer(&(views)->pointer), reference);  \
    CFLOW_BRANCHING_TRAVERSAL_ROW(label " / CSTL HashMap adjacency", samples, repetitions,     \
                                  work_units_, cflow_branching_observe_hash(&(views)->hash),       \
                                  reference);                                                      \
    CFLOW_BRANCHING_TRAVERSAL_ROW(label " / CSR adjacency", samples, repetitions, work_units_,     \
                                  cflow_branching_observe_csr(&(views)->csr), reference);          \
  } while (0)

#define CFLOW_BRANCHING_BENCHMARK_CASE(label, shape, setup_samples, traversal_samples,             \
                                       repetitions)                                                \
  do {                                                                                             \
    cflow_branching_fixture *fixture = &cflow_branching_fixture_state;                             \
    cflow_branching_case_views views = {0};                                                        \
    cflow_branching_observation reference = {0};                                                   \
    const bool fixture_ok = cflow_branching_fixture_init(fixture, shape);                          \
    check_true(fixture_ok);                                                                        \
    if (fixture_ok) {                                                                              \
      const bool views_ok = case_views_build(&views, &fixture->subgraph);                          \
      check_true(views_ok);                                                                        \
      if (views_ok) {                                                                              \
        check_true(case_views_equivalent(&views, &fixture->subgraph, &reference));                 \
        print_case_memory(label, &views);                                                          \
        CFLOW_BRANCHING_SETUP_CASE(label, setup_samples, &fixture->subgraph);                      \
        CFLOW_BRANCHING_TRAVERSAL_CASE(label, traversal_samples, repetitions, fixture, &views,     \
                                       reference);                                                 \
      }                                                                                            \
    }                                                                                              \
    case_views_destroy(&views);                                                                    \
  } while (0)

static void nested_fixture_destroy(cflow_branching_nested_fixture *fixture) {
  size_t index;
  if (!fixture) return;
  cflow_graph_destroy(&fixture->root);
  for (index = 0u; index < CFLOW_BRANCHING_NESTED_INNERS; ++index)
    cflow_graph_destroy(&fixture->inners[index]);
  for (index = 0u; index < CFLOW_BRANCHING_NESTED_LEAVES; ++index)
    cflow_graph_destroy(&fixture->leaves[index]);
  memset(fixture, 0, sizeof(*fixture));
}

static bool nested_fixture_init(cflow_branching_nested_fixture *fixture) {
  size_t leaf;
  size_t inner;
  const char *error = NULL;
  const cflow_graph *inner_branches[CFLOW_BRANCHING_NESTED_INNERS];
  if (!fixture) return false;
  memset(fixture, 0, sizeof(*fixture));

  for (leaf = 0u; leaf < CFLOW_BRANCHING_NESTED_LEAVES; ++leaf) {
    size_t operation;
    cflow_graph_init(&fixture->leaves[leaf], &cmeta_type_int);
    if (fixture->leaves[leaf].error) goto fail;
    for (operation = 0u; operation < CFLOW_BRANCHING_NESTED_LEAF_OPERATORS; ++operation) {
      bool added = operation % 2u == 0u
                       ? cflow_graph_map(&fixture->leaves[leaf], cflow_graph_path_identity.fn)
                       : cflow_graph_filter(&fixture->leaves[leaf], cflow_graph_path_keep.fn);
      if (!added) goto fail;
    }
  }
  for (inner = 0u; inner < CFLOW_BRANCHING_NESTED_INNERS; ++inner) {
    const cflow_graph *leaf_branches[CFLOW_BRANCHING_NESTED_LEAVES / CFLOW_BRANCHING_NESTED_INNERS];
    size_t branch;
    cflow_graph_init(&fixture->inners[inner], &cmeta_type_int);
    if (fixture->inners[inner].error) goto fail;
    for (branch = 0u; branch < CFLOW_BRANCHING_NESTED_LEAVES / CFLOW_BRANCHING_NESTED_INNERS;
         ++branch)
      leaf_branches[branch] =
          &fixture->leaves[inner * (CFLOW_BRANCHING_NESTED_LEAVES / CFLOW_BRANCHING_NESTED_INNERS) +
                           branch];
    if (!cflow_graph_relation(&fixture->inners[inner], leaf_branches,
                              CFLOW_BRANCHING_NESTED_LEAVES / CFLOW_BRANCHING_NESTED_INNERS,
                              cflow_relation_sequence_select(), (cmeta_callable){0}) ||
        !cflow_graph_map(&fixture->inners[inner], cflow_graph_path_identity.fn))
      goto fail;
    inner_branches[inner] = &fixture->inners[inner];
  }
  cflow_graph_init(&fixture->root, &cmeta_type_int);
  if (fixture->root.error ||
      !cflow_graph_relation(&fixture->root, inner_branches, CFLOW_BRANCHING_NESTED_INNERS,
                            cflow_relation_sequence_select(), (cmeta_callable){0}) ||
      !cflow_graph_map(&fixture->root, cflow_graph_path_identity.fn) ||
      !cflow_graph_validate(&fixture->root, &error) || error)
    goto fail;
  return true;

fail:
  nested_fixture_destroy(fixture);
  return false;
}

static bool nested_counts(const cflow_graph *graph, size_t *nodes, size_t *edges,
                          size_t *successor_lookups) {
  size_t node_count = 0u;
  size_t edge_count = 0u;
  size_t subgraph;
  if (!graph || !nodes || !edges || !successor_lookups) return false;
  for (subgraph = 0u; subgraph < graph->subgraph_count; ++subgraph) {
    const cflow_subgraph *current = &graph->subgraphs[subgraph];
    if (current->node_count > SIZE_MAX - node_count || current->edge_count > SIZE_MAX - edge_count)
      return false;
    node_count += current->node_count;
    edge_count += current->edge_count;
  }
  *nodes = node_count;
  *edges = edge_count;
  *successor_lookups = node_count;
  return true;
}

static uint64_t nested_topology_mix(uint64_t checksum, uint64_t token) {
  /* Both candidates use this consumer so timings isolate representation cost. */
  return (checksum ^ (token + UINT64_C(0x9e3779b97f4a7c15))) * UINT64_C(0x100000001b3);
}

static bool nested_dense_topology_once(const cflow_graph *graph, uint64_t *checksum) {
  uint64_t value = UINT64_C(0xcbf29ce484222325);
  size_t subgraph;
  if (!graph || !checksum) return false;
  for (subgraph = 0u; subgraph < graph->subgraph_count; ++subgraph) {
    const cflow_subgraph *current = &graph->subgraphs[subgraph];
    cflow_dense_successor_index index = {0};
    size_t observed = 0u;
    size_t source;
    if (cflow_dense_successor_index_build(&index, current) != CFLOW_DENSE_SUCCESSOR_INDEX_OK ||
        index.has_fanout) {
      cflow_dense_successor_index_destroy(&index);
      return false;
    }
    value = nested_topology_mix(value, (uint64_t)subgraph);
    for (source = 0u; source < current->node_count; ++source) {
      cflow_node_id successor;
      value = nested_topology_mix(value, (uint64_t)source);
      if (cflow_dense_successor_index_successor(&index, (cflow_node_id)source, &successor)) {
        value = nested_topology_mix(value, (uint64_t)successor + UINT64_C(1));
        ++observed;
      }
    }
    cflow_dense_successor_index_destroy(&index);
    if (observed != current->edge_count) return false;
  }
  *checksum = value;
  return true;
}

static bool nested_csr_topology_once(const cflow_graph *graph, uint64_t *checksum) {
  uint64_t value = UINT64_C(0xcbf29ce484222325);
  size_t subgraph;
  if (!graph || !checksum) return false;
  for (subgraph = 0u; subgraph < graph->subgraph_count; ++subgraph) {
    cflow_branching_csr_view view = {0};
    size_t observed = 0u;
    size_t source;
    if (cflow_branching_csr_build(&view, &graph->subgraphs[subgraph], NULL) !=
        CFLOW_BRANCHING_VIEW_OK)
      return false;
    value = nested_topology_mix(value, (uint64_t)subgraph);
    for (source = 0u; source < view.node_count; ++source) {
      size_t edge_id;
      value = nested_topology_mix(value, (uint64_t)source);
      for (edge_id = view.offsets[source]; edge_id < view.offsets[source + 1u]; ++edge_id) {
        value = nested_topology_mix(value, (uint64_t)view.targets[edge_id] + UINT64_C(1));
        ++observed;
      }
    }
    cflow_branching_csr_destroy(&view);
    if (observed != graph->subgraphs[subgraph].edge_count) return false;
  }
  *checksum = value;
  return true;
}

static bool print_nested_memory(const cflow_graph *graph) {
  cflow_branching_memory dense = {0};
  cflow_branching_memory csr = {0};
  size_t subgraph;
  if (!graph) return false;
  for (subgraph = 0u; subgraph < graph->subgraph_count; ++subgraph) {
    const cflow_subgraph *current = &graph->subgraphs[subgraph];
    size_t dense_bytes;
    size_t offset_count;
    size_t offset_bytes;
    size_t target_bytes;
    size_t csr_bytes;
    size_t csr_allocations;
    if (!benchmark_checked_mul(current->node_count, sizeof(cflow_node_id), &dense_bytes) ||
        !benchmark_checked_add(current->node_count, 1u, &offset_count) ||
        !benchmark_checked_mul(offset_count, sizeof(size_t), &offset_bytes) ||
        !benchmark_checked_mul(current->edge_count, sizeof(cflow_node_id), &target_bytes) ||
        !benchmark_checked_add(offset_bytes, target_bytes, &csr_bytes) ||
        !benchmark_checked_add(dense.allocation_count, 1u, &dense.allocation_count) ||
        !benchmark_checked_add(dense.allocated_bytes, dense_bytes, &dense.allocated_bytes) ||
        !benchmark_checked_add(csr.allocation_count, 1u + (current->edge_count ? 1u : 0u),
                               &csr_allocations) ||
        !benchmark_checked_add(csr.allocated_bytes, csr_bytes, &csr.allocated_bytes))
      return false;
    csr.allocation_count = csr_allocations;
    if (dense_bytes > dense.peak_bytes) dense.peak_bytes = dense_bytes;
    if (csr_bytes > csr.peak_bytes) csr.peak_bytes = csr_bytes;
  }
  dense.retained_bytes = dense.peak_bytes;
  csr.retained_bytes = csr.peak_bytes;
  print_memory_row("valid-nested-RELATION", "production-dense-successor", &dense);
  print_memory_row("valid-nested-RELATION", "candidate-CSR", &csr);
  return true;
}

suite("CFlow branching CSR representation benchmarks") {
  it("covers all required shapes with equivalent observations") {
    const cflow_branching_shape shapes[] = {CFLOW_BRANCHING_EMPTY,   CFLOW_BRANCHING_SINGLE,
                                            CFLOW_BRANCHING_TYPICAL, CFLOW_BRANCHING_HIGH_FANOUT,
                                            CFLOW_BRANCHING_SKEWED,  CFLOW_BRANCHING_PEAK};
    size_t shape;
    for (shape = 0u; shape < sizeof(shapes) / sizeof(shapes[0]); ++shape) {
      cflow_branching_case_views views = {0};
      cflow_branching_observation reference = {0};
      check_true(cflow_branching_fixture_init(&cflow_branching_fixture_state, shapes[shape]));
      check_true(case_views_build(&views, &cflow_branching_fixture_state.subgraph));
      check_true(
          case_views_equivalent(&views, &cflow_branching_fixture_state.subgraph, &reference));
      case_views_destroy(&views);
    }
  }

  bench("branching adjacency setup (complete view lifecycles)") {
    CFLOW_BRANCHING_BENCHMARK_CASE(
        "empty V=1 E=0", CFLOW_BRANCHING_EMPTY, CFLOW_BRANCHING_BOUNDARY_SETUP_SAMPLES,
        CFLOW_BRANCHING_BOUNDARY_TRAVERSAL_SAMPLES, CFLOW_BRANCHING_BOUNDARY_TRAVERSAL_REPETITIONS);
    CFLOW_BRANCHING_BENCHMARK_CASE(
        "single edge V=2 E=1", CFLOW_BRANCHING_SINGLE, CFLOW_BRANCHING_BOUNDARY_SETUP_SAMPLES,
        CFLOW_BRANCHING_BOUNDARY_TRAVERSAL_SAMPLES, CFLOW_BRANCHING_BOUNDARY_TRAVERSAL_REPETITIONS);
    CFLOW_BRANCHING_BENCHMARK_CASE("typical sparse V=256 E=384", CFLOW_BRANCHING_TYPICAL,
                                   CFLOW_BRANCHING_TYPICAL_SETUP_SAMPLES,
                                   CFLOW_BRANCHING_TYPICAL_TRAVERSAL_SAMPLES,
                                   CFLOW_BRANCHING_TYPICAL_TRAVERSAL_REPETITIONS);
    CFLOW_BRANCHING_BENCHMARK_CASE("high fan-out V=257 E=256", CFLOW_BRANCHING_HIGH_FANOUT,
                                   CFLOW_BRANCHING_TYPICAL_SETUP_SAMPLES,
                                   CFLOW_BRANCHING_TYPICAL_TRAVERSAL_SAMPLES,
                                   CFLOW_BRANCHING_TYPICAL_TRAVERSAL_REPETITIONS);
    CFLOW_BRANCHING_BENCHMARK_CASE("skewed degree V=1024 E=2046", CFLOW_BRANCHING_SKEWED,
                                   CFLOW_BRANCHING_LARGE_SETUP_SAMPLES,
                                   CFLOW_BRANCHING_LARGE_TRAVERSAL_SAMPLES, 1u);
    CFLOW_BRANCHING_BENCHMARK_CASE("peak sparse V=4096 E=8190", CFLOW_BRANCHING_PEAK,
                                   CFLOW_BRANCHING_PEAK_SETUP_SAMPLES,
                                   CFLOW_BRANCHING_PEAK_TRAVERSAL_SAMPLES, 1u);
  }

  bench("valid nested RELATION validation and topology controls") {
    cflow_branching_nested_fixture *fixture = &cflow_branching_nested_state;
    size_t nodes = 0u;
    size_t edges = 0u;
    size_t successor_lookups = 0u;
    size_t timed_ops = 0u;
    size_t repetition;
    uint64_t dense_checksum = 0u;
    uint64_t csr_checksum = 0u;
    bool all_valid = true;
    bool all_dense = true;
    bool all_csr = true;
    const bool initialized = nested_fixture_init(fixture);
    check_true(initialized);
    if (initialized) {
      const char *error = NULL;
      check_true(nested_counts(&fixture->root, &nodes, &edges, &successor_lookups));
      check_true(
          benchmark_checked_mul(successor_lookups, CFLOW_BRANCHING_NESTED_REPETITIONS, &timed_ops));
      check_true(nested_dense_topology_once(&fixture->root, &dense_checksum));
      check_true(nested_csr_topology_once(&fixture->root, &csr_checksum));
      check_equal(dense_checksum, csr_checksum);
      printf("CSR_PROFILE|valid-nested-RELATION|subgraphs=%zu|nodes=%zu|edges=%zu|successor_"
             "lookups_per_lifecycle=%zu\n",
             fixture->root.subgraph_count, nodes, edges, successor_lookups);
      check_true(print_nested_memory(&fixture->root));
      benchmark_ops("valid nested RELATION / complete Graph validation",
                    CFLOW_BRANCHING_NESTED_SAMPLES, timed_ops) {
        for (repetition = 0u; repetition < CFLOW_BRANCHING_NESTED_REPETITIONS; ++repetition) {
          error = NULL;
          if (!cflow_graph_validate(&fixture->root, &error) || error) all_valid = false;
        }
      }
      benchmark_ops("valid nested RELATION / production dense topology lifecycle",
                    CFLOW_BRANCHING_NESTED_SAMPLES, timed_ops) {
        for (repetition = 0u; repetition < CFLOW_BRANCHING_NESTED_REPETITIONS; ++repetition) {
          if (!nested_dense_topology_once(&fixture->root, &dense_checksum)) all_dense = false;
          cflow_branching_benchmark_sink ^= dense_checksum;
        }
      }
      benchmark_ops("valid nested RELATION / candidate CSR topology lifecycle",
                    CFLOW_BRANCHING_NESTED_SAMPLES, timed_ops) {
        for (repetition = 0u; repetition < CFLOW_BRANCHING_NESTED_REPETITIONS; ++repetition) {
          if (!nested_csr_topology_once(&fixture->root, &csr_checksum)) all_csr = false;
          cflow_branching_benchmark_sink ^= csr_checksum;
        }
      }
      check_true(all_valid);
      check_true(all_dense);
      check_true(all_csr);
    }
    nested_fixture_destroy(fixture);
  }
}
