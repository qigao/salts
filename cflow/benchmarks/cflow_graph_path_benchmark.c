#define TINYTEST_NO_MAIN
#include "tinytest.h"

#include <cflow/cflow.h>
#include <cflow/plan_internal.h>
#include <turbostl/hash_map.h>

#include <stdint.h>
#include <string.h>

enum {
  CFLOW_GRAPH_PATH_BOUNDARY_OPERATORS = 1,
  CFLOW_GRAPH_PATH_TYPICAL_OPERATORS = 16,
  CFLOW_GRAPH_PATH_MEDIUM_OPERATORS = 256,
  CFLOW_GRAPH_PATH_PEAK_OPERATORS = 4096,
  CFLOW_GRAPH_PATH_MAX_NODES = CFLOW_GRAPH_PATH_PEAK_OPERATORS + 1,
  CFLOW_GRAPH_PATH_TYPICAL_SAMPLES = 1000,
  CFLOW_GRAPH_PATH_TYPICAL_REPETITIONS = 64,
  CFLOW_GRAPH_PATH_MEDIUM_SAMPLES = 64,
  CFLOW_GRAPH_PATH_MEDIUM_REPETITIONS = 4,
  CFLOW_GRAPH_PATH_PEAK_SAMPLES = 4,
  CFLOW_GRAPH_PATH_PEAK_REPETITIONS = 1,
  CFLOW_GRAPH_PATH_BOUNDARY_COMPILE_SAMPLES = 5000,
  CFLOW_GRAPH_PATH_TYPICAL_COMPILE_SAMPLES = 1000,
  CFLOW_GRAPH_PATH_MEDIUM_COMPILE_SAMPLES = 64,
  CFLOW_GRAPH_PATH_PEAK_COMPILE_SAMPLES = 4,
  CFLOW_GRAPH_PATH_TAG_MAP = 1,
  CFLOW_GRAPH_PATH_TAG_FILTER = 2,
  CFLOW_GRAPH_PATH_TAG_BITS = 2
};

typedef struct cflow_graph_path_observation {
  size_t operators;
  uint64_t checksum;
  bool ok;
} cflow_graph_path_observation;

typedef struct cflow_graph_path_tree_node {
  cflow_node_id id;
  const struct cflow_graph_path_tree_node *next;
} cflow_graph_path_tree_node;

typedef struct cflow_graph_path_fixture {
  cflow_graph surface;
  cflow_graph normalized;
  cflow_plan plan;
  turbo_hash_map_t successor_map;
  cflow_node_id dense_successors[CFLOW_GRAPH_PATH_MAX_NODES];
  cflow_graph_path_tree_node tree_nodes[CFLOW_GRAPH_PATH_MAX_NODES];
  const cflow_subgraph *subgraph;
  size_t operator_count;
  bool successor_map_initialized;
} cflow_graph_path_fixture;

static cflow_graph_path_fixture cflow_graph_path_fixture_state;
static volatile uint64_t cflow_graph_path_benchmark_sink;

typed(map, value, int, cflow_graph_path_identity, (int value)) { return value; }
typed(filter, value, bool, cflow_graph_path_keep, (int value)) {
  (void)value;
  return true;
}

static void cflow_graph_path_fixture_destroy(cflow_graph_path_fixture *fixture) {
  if (!fixture) return;
  if (fixture->successor_map_initialized) turbo_hash_map_destroy(&fixture->successor_map);
  cflow_plan_destroy(&fixture->plan);
  cflow_graph_destroy(&fixture->normalized);
  cflow_graph_destroy(&fixture->surface);
  memset(fixture, 0, sizeof(*fixture));
}

static bool cflow_graph_path_fixture_init(cflow_graph_path_fixture *fixture,
                                          size_t operator_count) {
  const cflow_subgraph *subgraph;
  const char *error = NULL;
  size_t index;

  if (!fixture || operator_count == 0u || operator_count > CFLOW_GRAPH_PATH_PEAK_OPERATORS)
    return false;
  memset(fixture, 0, sizeof(*fixture));

  cflow_graph_init(&fixture->surface, &cmeta_type_int);
  if (fixture->surface.error) goto fail;
  for (index = 0u; index < operator_count; ++index) {
    const bool added = index % 2u == 0u
                           ? cflow_graph_map(&fixture->surface, cflow_graph_path_identity.fn)
                           : cflow_graph_filter(&fixture->surface, cflow_graph_path_keep.fn);
    if (!added) goto fail;
  }
  if (!cflow_graph_validate(&fixture->surface, &error) || error) goto fail;
  if (!cflow_graph_normalize(&fixture->normalized, &fixture->surface)) goto fail;
  if (!cflow_graph_validate(&fixture->normalized, &error) || error) goto fail;
  if (!cflow_plan_compile(&fixture->plan, &fixture->normalized, NULL)) goto fail;

  subgraph = cflow_graph_subgraph(&fixture->normalized, fixture->normalized.root);
  if (!subgraph || subgraph->node_count != operator_count + 1u ||
      subgraph->edge_count != operator_count || subgraph->node_count > CFLOW_GRAPH_PATH_MAX_NODES)
    goto fail;

  for (index = 0u; index < subgraph->node_count; ++index) {
    fixture->dense_successors[index] = CMETA_INVALID_ID;
    fixture->tree_nodes[index].id = (cflow_node_id)index;
    fixture->tree_nodes[index].next = NULL;
  }

  if (turbo_hash_map_init_bytes(&fixture->successor_map, sizeof(cflow_node_id),
                                _Alignof(cflow_node_id), sizeof(cflow_node_id),
                                _Alignof(cflow_node_id), subgraph->edge_count, turbo_hash_bytes,
                                turbo_hash_key_equal, NULL) != TURBO_STL_OK)
    goto fail;
  fixture->successor_map_initialized = true;
  if (turbo_hash_map_reserve(&fixture->successor_map, subgraph->edge_count) != TURBO_STL_OK)
    goto fail;

  for (index = 0u; index < subgraph->edge_count; ++index) {
    const cflow_edge *edge = cflow_subgraph_edge(subgraph, (cflow_edge_id)index);
    if (!edge || edge->from >= subgraph->node_count || edge->to >= subgraph->node_count ||
        fixture->dense_successors[edge->from] != CMETA_INVALID_ID)
      goto fail;
    fixture->dense_successors[edge->from] = edge->to;
    if (turbo_hash_map_put(&fixture->successor_map, &edge->from, &edge->to) != TURBO_STL_OK)
      goto fail;
  }
  if (turbo_hash_map_size(&fixture->successor_map) != subgraph->edge_count) goto fail;

  for (index = 0u; index < subgraph->node_count; ++index) {
    const cflow_node_id successor = fixture->dense_successors[index];
    if (successor != CMETA_INVALID_ID)
      fixture->tree_nodes[index].next = &fixture->tree_nodes[successor];
  }

  fixture->subgraph = subgraph;
  fixture->operator_count = operator_count;
  return true;

fail:
  cflow_graph_path_fixture_destroy(fixture);
  return false;
}

static uint64_t cflow_graph_path_mix(uint64_t checksum, uint64_t token) {
  return (checksum ^ (token + UINT64_C(0x9e3779b97f4a7c15))) * UINT64_C(0x100000001b3);
}

static cflow_graph_path_observation cflow_graph_path_observation_begin(void) {
  const cflow_graph_path_observation observation = {0u, UINT64_C(0xcbf29ce484222325), true};
  return observation;
}

static bool cflow_graph_path_observe_node(const cflow_graph_path_fixture *fixture, cflow_node_id id,
                                          cflow_graph_path_observation *observation) {
  const cflow_node *node;
  uint64_t tag;

  if (!fixture || !fixture->subgraph || !observation || id >= fixture->subgraph->node_count)
    return false;
  node = cflow_subgraph_node(fixture->subgraph, id);
  if (!node) return false;
  if (node->op == CFLOW_OP_SOURCE)
    return observation->operators == 0u && id == fixture->subgraph->entry;
  if (node->op == CFLOW_OP_MAP) tag = CFLOW_GRAPH_PATH_TAG_MAP;
  else if (node->op == CFLOW_OP_FILTER) tag = CFLOW_GRAPH_PATH_TAG_FILTER;
  else return false;
  if (observation->operators >= fixture->operator_count) return false;

  ++observation->operators;
  observation->checksum = cflow_graph_path_mix(
      observation->checksum, ((uint64_t)observation->operators << CFLOW_GRAPH_PATH_TAG_BITS) | tag);
  return true;
}

static bool cflow_graph_path_flat_successor(const cflow_subgraph *subgraph, cflow_node_id node,
                                            cflow_node_id *successor, bool *found) {
  size_t edge_index;

  if (!subgraph || !successor || !found) return false;
  *successor = CMETA_INVALID_ID;
  *found = false;
  for (edge_index = 0u; edge_index < subgraph->edge_count; ++edge_index) {
    const cflow_edge *edge = &subgraph->edges[edge_index];
    if (edge->from != node) continue;
    if (*found) return false;
    *successor = edge->to;
    *found = true;
  }
  return true;
}

/* Each timed loop stays explicit so a shared adapter cannot replace the
 * representation-specific lookup cost being measured. */
static cflow_graph_path_observation
cflow_graph_path_traverse_graph_apis(const cflow_graph_path_fixture *fixture) {
  cflow_graph_path_observation observation = cflow_graph_path_observation_begin();
  cflow_node_id current;
  size_t visited = 0u;

  if (!fixture || !fixture->subgraph) {
    observation.ok = false;
    return observation;
  }
  current = fixture->subgraph->entry;
  for (;;) {
    const size_t degree = cflow_subgraph_out_degree(fixture->subgraph, current);
    cflow_node_id successor = CMETA_INVALID_ID;

    if (++visited > fixture->subgraph->node_count ||
        !cflow_graph_path_observe_node(fixture, current, &observation)) {
      observation.ok = false;
      return observation;
    }
    if (degree == 0u) break;
    if (degree != 1u || !cflow_subgraph_single_successor(fixture->subgraph, current, &successor)) {
      observation.ok = false;
      return observation;
    }
    current = successor;
  }
  observation.ok = observation.operators == fixture->operator_count;
  return observation;
}

static cflow_graph_path_observation
cflow_graph_path_traverse_flat_once(const cflow_graph_path_fixture *fixture) {
  cflow_graph_path_observation observation = cflow_graph_path_observation_begin();
  cflow_node_id current;
  size_t visited = 0u;

  if (!fixture || !fixture->subgraph) {
    observation.ok = false;
    return observation;
  }
  current = fixture->subgraph->entry;
  for (;;) {
    cflow_node_id successor = CMETA_INVALID_ID;
    bool found = false;
    if (++visited > fixture->subgraph->node_count ||
        !cflow_graph_path_observe_node(fixture, current, &observation) ||
        !cflow_graph_path_flat_successor(fixture->subgraph, current, &successor, &found)) {
      observation.ok = false;
      return observation;
    }
    if (!found) break;
    current = successor;
  }
  observation.ok = observation.operators == fixture->operator_count;
  return observation;
}

static cflow_graph_path_observation
cflow_graph_path_traverse_dense(const cflow_graph_path_fixture *fixture) {
  cflow_graph_path_observation observation = cflow_graph_path_observation_begin();
  cflow_node_id current;
  size_t visited = 0u;

  if (!fixture || !fixture->subgraph) {
    observation.ok = false;
    return observation;
  }
  current = fixture->subgraph->entry;
  for (;;) {
    cflow_node_id successor;
    if (++visited > fixture->subgraph->node_count ||
        !cflow_graph_path_observe_node(fixture, current, &observation)) {
      observation.ok = false;
      return observation;
    }
    successor = fixture->dense_successors[current];
    if (successor == CMETA_INVALID_ID) break;
    current = successor;
  }
  observation.ok = observation.operators == fixture->operator_count;
  return observation;
}

static cflow_graph_path_observation
cflow_graph_path_traverse_tree(const cflow_graph_path_fixture *fixture) {
  cflow_graph_path_observation observation = cflow_graph_path_observation_begin();
  const cflow_graph_path_tree_node *node;
  size_t visited = 0u;

  if (!fixture || !fixture->subgraph || fixture->subgraph->entry >= fixture->subgraph->node_count) {
    observation.ok = false;
    return observation;
  }
  node = &fixture->tree_nodes[fixture->subgraph->entry];
  while (node) {
    if (++visited > fixture->subgraph->node_count ||
        !cflow_graph_path_observe_node(fixture, node->id, &observation)) {
      observation.ok = false;
      return observation;
    }
    node = node->next;
  }
  observation.ok = observation.operators == fixture->operator_count;
  return observation;
}

static cflow_graph_path_observation
cflow_graph_path_traverse_hash_map(const cflow_graph_path_fixture *fixture) {
  cflow_graph_path_observation observation = cflow_graph_path_observation_begin();
  cflow_node_id current;
  size_t visited = 0u;

  if (!fixture || !fixture->subgraph || !fixture->successor_map_initialized) {
    observation.ok = false;
    return observation;
  }
  current = fixture->subgraph->entry;
  for (;;) {
    const cflow_node_id *successor;
    if (++visited > fixture->subgraph->node_count ||
        !cflow_graph_path_observe_node(fixture, current, &observation)) {
      observation.ok = false;
      return observation;
    }
    successor = (const cflow_node_id *)turbo_hash_map_get_const(&fixture->successor_map, &current);
    if (!successor) break;
    current = *successor;
  }
  observation.ok = observation.operators == fixture->operator_count;
  return observation;
}

static cflow_graph_path_observation
cflow_graph_path_traverse_plan(const cflow_graph_path_fixture *fixture) {
  cflow_graph_path_observation observation = cflow_graph_path_observation_begin();
  const cflow_plan_impl *impl;
  size_t pc;

  if (!fixture || !fixture->plan.impl) {
    observation.ok = false;
    return observation;
  }
  impl = (const cflow_plan_impl *)fixture->plan.impl;
  for (pc = 0u; pc < impl->count; ++pc) {
    uint64_t tag;
    if (impl->code[pc].opcode == CMETA_PLAN_MAP) tag = CFLOW_GRAPH_PATH_TAG_MAP;
    else if (impl->code[pc].opcode == CMETA_PLAN_FILTER) tag = CFLOW_GRAPH_PATH_TAG_FILTER;
    else {
      observation.ok = false;
      return observation;
    }
    ++observation.operators;
    observation.checksum = cflow_graph_path_mix(
        observation.checksum, ((uint64_t)observation.operators << CFLOW_GRAPH_PATH_TAG_BITS) | tag);
  }
  observation.ok = observation.operators == fixture->operator_count;
  return observation;
}

static bool cflow_graph_path_observations_equal(cflow_graph_path_observation left,
                                                cflow_graph_path_observation right) {
  return left.ok && right.ok && left.operators == right.operators &&
         left.checksum == right.checksum;
}

static bool cflow_graph_path_fixture_equivalent(const cflow_graph_path_fixture *fixture,
                                                cflow_graph_path_observation *reference) {
  const cflow_graph_path_observation graph_apis = cflow_graph_path_traverse_graph_apis(fixture);
  const cflow_graph_path_observation flat_once = cflow_graph_path_traverse_flat_once(fixture);
  const cflow_graph_path_observation dense = cflow_graph_path_traverse_dense(fixture);
  const cflow_graph_path_observation tree = cflow_graph_path_traverse_tree(fixture);
  const cflow_graph_path_observation hash_map = cflow_graph_path_traverse_hash_map(fixture);
  const cflow_graph_path_observation plan = cflow_graph_path_traverse_plan(fixture);

  if (!cflow_graph_path_observations_equal(graph_apis, flat_once) ||
      !cflow_graph_path_observations_equal(graph_apis, dense) ||
      !cflow_graph_path_observations_equal(graph_apis, tree) ||
      !cflow_graph_path_observations_equal(graph_apis, hash_map) ||
      !cflow_graph_path_observations_equal(graph_apis, plan))
    return false;
  if (reference) *reference = graph_apis;
  return true;
}

static bool cflow_graph_path_representations_equivalent(size_t operator_count) {
  cflow_graph_path_fixture *fixture = &cflow_graph_path_fixture_state;
  bool equivalent;

  if (!cflow_graph_path_fixture_init(fixture, operator_count)) return false;
  equivalent = cflow_graph_path_fixture_equivalent(fixture, NULL);
  cflow_graph_path_fixture_destroy(fixture);
  return equivalent;
}

static bool cflow_graph_path_compile_plan_once(const cflow_graph_path_fixture *fixture) {
  cflow_plan plan = {0};
  cflow_plan_compile_stats stats = {0};
  const bool compiled = fixture &&
                        cflow_plan_compile(&plan, &fixture->normalized, &stats) &&
                        !plan.error && stats.graph_nodes == fixture->operator_count + 1u &&
                        stats.instructions == fixture->operator_count;

  if (compiled) cflow_graph_path_benchmark_sink ^= stats.instructions;
  cflow_plan_destroy(&plan);
  return compiled;
}

#define CFLOW_GRAPH_PATH_BENCHMARK_ROW(title, samples, repetitions, operators, expression,         \
                                       reference)                                                  \
  do {                                                                                             \
    cflow_graph_path_observation measured = {0};                                                   \
    size_t repetition;                                                                             \
    benchmark_ops(title, samples, (operators) * (repetitions)) {                                   \
      for (repetition = 0u; repetition < (repetitions); ++repetition) {                            \
        measured = (expression);                                                                   \
        cflow_graph_path_benchmark_sink ^= measured.checksum;                                      \
      }                                                                                            \
    }                                                                                              \
    check_true(cflow_graph_path_observations_equal(measured, reference));                          \
  } while (0)

#define CFLOW_GRAPH_PATH_BENCHMARK_CASE(label, operators, samples, repetitions)                    \
  do {                                                                                             \
    cflow_graph_path_fixture *fixture = &cflow_graph_path_fixture_state;                           \
    cflow_graph_path_observation reference = {0};                                                  \
    const bool initialized = cflow_graph_path_fixture_init(fixture, operators);                    \
    check_true(initialized);                                                                       \
    if (initialized) {                                                                             \
      check_true(cflow_graph_path_fixture_equivalent(fixture, &reference));                        \
      CFLOW_GRAPH_PATH_BENCHMARK_ROW(label " / Graph APIs flat edges", samples, repetitions,       \
                                     operators, cflow_graph_path_traverse_graph_apis(fixture),     \
                                     reference);                                                   \
      CFLOW_GRAPH_PATH_BENCHMARK_ROW(label " / one-pass flat edges", samples, repetitions,         \
                                     operators, cflow_graph_path_traverse_flat_once(fixture),      \
                                     reference);                                                   \
      CFLOW_GRAPH_PATH_BENCHMARK_ROW(label " / dense successor array", samples, repetitions,       \
                                     operators, cflow_graph_path_traverse_dense(fixture),          \
                                     reference);                                                   \
      CFLOW_GRAPH_PATH_BENCHMARK_ROW(label " / linked path tree", samples, repetitions, operators, \
                                     cflow_graph_path_traverse_tree(fixture), reference);          \
      CFLOW_GRAPH_PATH_BENCHMARK_ROW(label " / TurboSTL HashMap", samples, repetitions, operators, \
                                     cflow_graph_path_traverse_hash_map(fixture), reference);      \
      CFLOW_GRAPH_PATH_BENCHMARK_ROW(label " / compiled Plan tape", samples, repetitions,          \
                                     operators, cflow_graph_path_traverse_plan(fixture),           \
                                     reference);                                                   \
    }                                                                                              \
    cflow_graph_path_fixture_destroy(fixture);                                                     \
  } while (0)

#define CFLOW_GRAPH_PATH_COMPILE_BENCHMARK_CASE(label, operators, samples)                        \
  do {                                                                                             \
    cflow_graph_path_fixture *fixture = &cflow_graph_path_fixture_state;                           \
    bool all_compiled = true;                                                                      \
    const bool initialized = cflow_graph_path_fixture_init(fixture, operators);                    \
    check_true(initialized);                                                                       \
    if (initialized) {                                                                             \
      check_true(cflow_graph_path_compile_plan_once(fixture));                                    \
      benchmark_batch(label " / normalized Graph -> Plan compile + destroy", samples) {          \
        if (!cflow_graph_path_compile_plan_once(fixture)) all_compiled = false;                    \
      }                                                                                            \
      check_true(all_compiled);                                                                    \
    }                                                                                              \
    cflow_graph_path_fixture_destroy(fixture);                                                     \
  } while (0)

suite("CFlow Graph path representation benchmarks") {
  it("derives equivalent traversal observations from one Graph") {
    check_true(cflow_graph_path_representations_equivalent(CFLOW_GRAPH_PATH_BOUNDARY_OPERATORS));
    check_true(cflow_graph_path_representations_equivalent(CFLOW_GRAPH_PATH_TYPICAL_OPERATORS));
    check_true(cflow_graph_path_representations_equivalent(CFLOW_GRAPH_PATH_PEAK_OPERATORS));
  }

  bench("immutable linear Graph traversal (logical-operator throughput)") {
    CFLOW_GRAPH_PATH_BENCHMARK_CASE("16 operators", CFLOW_GRAPH_PATH_TYPICAL_OPERATORS,
                                    CFLOW_GRAPH_PATH_TYPICAL_SAMPLES,
                                    CFLOW_GRAPH_PATH_TYPICAL_REPETITIONS);
    CFLOW_GRAPH_PATH_BENCHMARK_CASE("256 operators", CFLOW_GRAPH_PATH_MEDIUM_OPERATORS,
                                    CFLOW_GRAPH_PATH_MEDIUM_SAMPLES,
                                    CFLOW_GRAPH_PATH_MEDIUM_REPETITIONS);
    CFLOW_GRAPH_PATH_BENCHMARK_CASE("4096 operators", CFLOW_GRAPH_PATH_PEAK_OPERATORS,
                                    CFLOW_GRAPH_PATH_PEAK_SAMPLES,
                                    CFLOW_GRAPH_PATH_PEAK_REPETITIONS);
  }

  bench("normalized linear Graph compilation (complete Plan lifecycles)") {
    CFLOW_GRAPH_PATH_COMPILE_BENCHMARK_CASE(
        "1 operator", CFLOW_GRAPH_PATH_BOUNDARY_OPERATORS,
        CFLOW_GRAPH_PATH_BOUNDARY_COMPILE_SAMPLES);
    CFLOW_GRAPH_PATH_COMPILE_BENCHMARK_CASE(
        "16 operators", CFLOW_GRAPH_PATH_TYPICAL_OPERATORS,
        CFLOW_GRAPH_PATH_TYPICAL_COMPILE_SAMPLES);
    CFLOW_GRAPH_PATH_COMPILE_BENCHMARK_CASE(
        "256 operators", CFLOW_GRAPH_PATH_MEDIUM_OPERATORS,
        CFLOW_GRAPH_PATH_MEDIUM_COMPILE_SAMPLES);
    CFLOW_GRAPH_PATH_COMPILE_BENCHMARK_CASE(
        "4096 operators", CFLOW_GRAPH_PATH_PEAK_OPERATORS,
        CFLOW_GRAPH_PATH_PEAK_COMPILE_SAMPLES);
  }
}
