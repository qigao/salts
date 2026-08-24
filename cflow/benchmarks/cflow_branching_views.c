#include "cflow_branching_views.h"

#include <stdlib.h>
#include <string.h>

#define CFLOW_BRANCHING_CHECKSUM_SEED UINT64_C(0xcbf29ce484222325)
#define CFLOW_BRANCHING_CHECKSUM_BIAS UINT64_C(0x9e3779b97f4a7c15)
#define CFLOW_BRANCHING_CHECKSUM_PRIME UINT64_C(0x100000001b3)

static void *system_allocate(size_t size, void *context) {
  (void)context;
  return malloc(size);
}

static void system_deallocate(void *pointer, void *context) {
  (void)context;
  free(pointer);
}

static bool resolve_allocator(const cflow_branching_allocator *requested,
                              cflow_branching_allocator *resolved) {
  if (!resolved) return false;
  if (!requested) {
    *resolved = (cflow_branching_allocator){system_allocate, system_deallocate, NULL};
    return true;
  }
  if (!requested->allocate || !requested->deallocate) return false;
  *resolved = *requested;
  return true;
}

static bool checked_add(size_t left, size_t right, size_t *out) {
  if (!out || right > SIZE_MAX - left) return false;
  *out = left + right;
  return true;
}

static bool checked_mul(size_t left, size_t right, size_t *out) {
  if (!out || (left && right > SIZE_MAX / left)) return false;
  *out = left * right;
  return true;
}

static cflow_branching_view_status validate_shape(const cflow_subgraph *subgraph,
                                                  size_t *offset_bytes, size_t *target_bytes) {
  size_t offset_count;
  size_t edge_id;

  if (!subgraph || (subgraph->edge_count && !subgraph->edges) ||
      subgraph->node_count > (size_t)CMETA_INVALID_ID)
    return CFLOW_BRANCHING_VIEW_INVALID_ARGUMENT;
  if (!checked_add(subgraph->node_count, 1u, &offset_count) ||
      !checked_mul(offset_count, sizeof(size_t), offset_bytes) ||
      !checked_mul(subgraph->edge_count, sizeof(cflow_node_id), target_bytes))
    return CFLOW_BRANCHING_VIEW_OVERFLOW;
  for (edge_id = 0u; edge_id < subgraph->edge_count; ++edge_id) {
    const cflow_edge *edge = &subgraph->edges[edge_id];
    if (edge->from >= subgraph->node_count || edge->to >= subgraph->node_count)
      return CFLOW_BRANCHING_VIEW_INVALID_EDGE;
  }
  return CFLOW_BRANCHING_VIEW_OK;
}

static uint64_t mix_checksum(uint64_t checksum, uint64_t token) {
  return (checksum ^ (token + CFLOW_BRANCHING_CHECKSUM_BIAS)) * CFLOW_BRANCHING_CHECKSUM_PRIME;
}

static uint64_t mix_target(uint64_t checksum, cflow_node_id target) {
  return mix_checksum(checksum, (uint64_t)target + UINT64_C(1));
}

static cflow_branching_observation observation_begin(size_t node_count) {
  cflow_branching_observation observation = {node_count, 0u, CFLOW_BRANCHING_CHECKSUM_SEED, true};
  return observation;
}

static void observation_commit_source(cflow_branching_observation *observation, size_t source,
                                      uint64_t source_checksum) {
  observation->checksum = mix_checksum(observation->checksum, (uint64_t)source);
  observation->checksum = mix_checksum(observation->checksum, source_checksum);
}

bool cflow_branching_observations_equal(const cflow_branching_observation *left,
                                        const cflow_branching_observation *right) {
  return left && right && left->ok && right->ok && left->node_count == right->node_count &&
         left->edge_count == right->edge_count && left->checksum == right->checksum;
}

cflow_branching_observation cflow_branching_observe_flat_lookup(const cflow_subgraph *subgraph) {
  cflow_branching_observation observation = observation_begin(0u);
  size_t source;

  if (!subgraph || (subgraph->edge_count && !subgraph->edges)) {
    observation.ok = false;
    return observation;
  }
  observation.node_count = subgraph->node_count;
  for (source = 0u; source < subgraph->node_count; ++source) {
    uint64_t source_checksum = CFLOW_BRANCHING_CHECKSUM_SEED;
    size_t expected = cflow_subgraph_out_degree(subgraph, (cflow_node_id)source);
    size_t observed = 0u;
    size_t edge_id;
    for (edge_id = 0u; edge_id < subgraph->edge_count; ++edge_id) {
      const cflow_edge *edge = cflow_subgraph_edge(subgraph, (cflow_edge_id)edge_id);
      if (!edge || edge->from >= subgraph->node_count || edge->to >= subgraph->node_count) {
        observation.ok = false;
        return observation;
      }
      if (edge->from != source) continue;
      source_checksum = mix_target(source_checksum, edge->to);
      ++observed;
    }
    if (observed != expected || observed > SIZE_MAX - observation.edge_count) {
      observation.ok = false;
      return observation;
    }
    observation.edge_count += observed;
    observation_commit_source(&observation, source, source_checksum);
  }
  observation.ok = observation.edge_count == subgraph->edge_count;
  return observation;
}

static bool memory_add(cflow_branching_memory *memory, size_t bytes, size_t *current) {
  if (!memory || !current || memory->allocation_count == SIZE_MAX ||
      bytes > SIZE_MAX - memory->allocated_bytes || bytes > SIZE_MAX - *current)
    return false;
  ++memory->allocation_count;
  memory->allocated_bytes += bytes;
  *current += bytes;
  if (*current > memory->peak_bytes) memory->peak_bytes = *current;
  return true;
}

static bool memory_add_transient(cflow_branching_memory *memory, size_t count, size_t bytes,
                                 size_t current) {
  size_t allocation_count;
  size_t allocated_bytes;
  size_t transient_peak;
  if (!memory || !checked_add(memory->allocation_count, count, &allocation_count) ||
      !checked_add(memory->allocated_bytes, bytes, &allocated_bytes) ||
      !checked_add(current, bytes, &transient_peak))
    return false;
  memory->allocation_count = allocation_count;
  memory->allocated_bytes = allocated_bytes;
  if (transient_peak > memory->peak_bytes) memory->peak_bytes = transient_peak;
  return true;
}

cflow_branching_view_status
cflow_branching_flat_once_build(cflow_branching_flat_once_view *out, const cflow_subgraph *subgraph,
                                const cflow_branching_allocator *allocator) {
  cflow_branching_flat_once_view built = {0};
  cflow_branching_view_status status;
  size_t ignored_offsets;
  size_t ignored_targets;
  size_t bytes;
  size_t current = 0u;

  if (!out) return CFLOW_BRANCHING_VIEW_INVALID_ARGUMENT;
  memset(out, 0, sizeof(*out));
  status = validate_shape(subgraph, &ignored_offsets, &ignored_targets);
  if (status != CFLOW_BRANCHING_VIEW_OK) return status;
  if (!resolve_allocator(allocator, &built.allocator)) return CFLOW_BRANCHING_VIEW_INVALID_ARGUMENT;
  if (!checked_mul(subgraph->node_count, sizeof(*built.source_checksums), &bytes))
    return CFLOW_BRANCHING_VIEW_OVERFLOW;
  if (bytes) {
    built.source_checksums = (uint64_t *)built.allocator.allocate(bytes, built.allocator.context);
    if (!built.source_checksums) return CFLOW_BRANCHING_VIEW_ALLOCATION_FAILED;
    if (!memory_add(&built.memory, bytes, &current)) {
      built.allocator.deallocate(built.source_checksums, built.allocator.context);
      return CFLOW_BRANCHING_VIEW_OVERFLOW;
    }
  }
  built.node_count = subgraph->node_count;
  built.memory.retained_bytes = current;
  *out = built;
  return CFLOW_BRANCHING_VIEW_OK;
}

void cflow_branching_flat_once_destroy(cflow_branching_flat_once_view *view) {
  if (!view) return;
  if (view->source_checksums && view->allocator.deallocate)
    view->allocator.deallocate(view->source_checksums, view->allocator.context);
  memset(view, 0, sizeof(*view));
}

cflow_branching_observation cflow_branching_observe_flat_once(cflow_branching_flat_once_view *view,
                                                              const cflow_subgraph *subgraph) {
  cflow_branching_observation observation = observation_begin(0u);
  size_t source;
  size_t edge_id;

  if (!view || !subgraph || view->node_count != subgraph->node_count ||
      (subgraph->edge_count && !subgraph->edges) || (view->node_count && !view->source_checksums)) {
    observation.ok = false;
    return observation;
  }
  observation.node_count = view->node_count;
  for (source = 0u; source < view->node_count; ++source)
    view->source_checksums[source] = CFLOW_BRANCHING_CHECKSUM_SEED;
  for (edge_id = 0u; edge_id < subgraph->edge_count; ++edge_id) {
    const cflow_edge *edge = &subgraph->edges[edge_id];
    if (edge->from >= view->node_count || edge->to >= view->node_count) {
      observation.ok = false;
      return observation;
    }
    view->source_checksums[edge->from] = mix_target(view->source_checksums[edge->from], edge->to);
    ++observation.edge_count;
  }
  for (source = 0u; source < view->node_count; ++source)
    observation_commit_source(&observation, source, view->source_checksums[source]);
  return observation;
}

cflow_branching_view_status
cflow_branching_pointer_build(cflow_branching_pointer_view *out, const cflow_subgraph *subgraph,
                              const cflow_branching_allocator *allocator) {
  cflow_branching_pointer_view built = {0};
  cflow_branching_view_status status;
  size_t ignored_offsets;
  size_t ignored_targets;
  size_t node_bytes;
  size_t link_bytes;
  size_t current = 0u;
  size_t edge_id;

  if (!out) return CFLOW_BRANCHING_VIEW_INVALID_ARGUMENT;
  memset(out, 0, sizeof(*out));
  status = validate_shape(subgraph, &ignored_offsets, &ignored_targets);
  if (status != CFLOW_BRANCHING_VIEW_OK) return status;
  if (!resolve_allocator(allocator, &built.allocator)) return CFLOW_BRANCHING_VIEW_INVALID_ARGUMENT;
  if (!checked_mul(subgraph->node_count, sizeof(*built.nodes), &node_bytes) ||
      !checked_mul(subgraph->edge_count, sizeof(*built.links), &link_bytes))
    return CFLOW_BRANCHING_VIEW_OVERFLOW;
  if (node_bytes) {
    built.nodes = (cflow_branching_pointer_node *)built.allocator.allocate(node_bytes,
                                                                           built.allocator.context);
    if (!built.nodes) return CFLOW_BRANCHING_VIEW_ALLOCATION_FAILED;
    memset(built.nodes, 0, node_bytes);
    if (!memory_add(&built.memory, node_bytes, &current)) goto overflow;
  }
  if (link_bytes) {
    built.links = (cflow_branching_pointer_link *)built.allocator.allocate(link_bytes,
                                                                           built.allocator.context);
    if (!built.links) goto allocation_failed;
    if (!memory_add(&built.memory, link_bytes, &current)) goto overflow;
  }
  for (edge_id = 0u; edge_id < subgraph->edge_count; ++edge_id) {
    const cflow_edge *edge = &subgraph->edges[edge_id];
    cflow_branching_pointer_node *node = &built.nodes[edge->from];
    cflow_branching_pointer_link *link = &built.links[edge_id];
    link->target = edge->to;
    link->next = NULL;
    if (node->last) node->last->next = link;
    else node->first = link;
    node->last = link;
  }
  built.node_count = subgraph->node_count;
  built.edge_count = subgraph->edge_count;
  built.memory.retained_bytes = current;
  *out = built;
  return CFLOW_BRANCHING_VIEW_OK;

overflow:
  status = CFLOW_BRANCHING_VIEW_OVERFLOW;
  goto fail;
allocation_failed:
  status = CFLOW_BRANCHING_VIEW_ALLOCATION_FAILED;
fail:
  if (built.links) built.allocator.deallocate(built.links, built.allocator.context);
  if (built.nodes) built.allocator.deallocate(built.nodes, built.allocator.context);
  return status;
}

void cflow_branching_pointer_destroy(cflow_branching_pointer_view *view) {
  if (!view) return;
  if (view->links && view->allocator.deallocate)
    view->allocator.deallocate(view->links, view->allocator.context);
  if (view->nodes && view->allocator.deallocate)
    view->allocator.deallocate(view->nodes, view->allocator.context);
  memset(view, 0, sizeof(*view));
}

cflow_branching_observation
cflow_branching_observe_pointer(const cflow_branching_pointer_view *view) {
  cflow_branching_observation observation = observation_begin(0u);
  size_t source;
  if (!view || (view->node_count && !view->nodes) || (view->edge_count && !view->links)) {
    observation.ok = false;
    return observation;
  }
  observation.node_count = view->node_count;
  for (source = 0u; source < view->node_count; ++source) {
    const cflow_branching_pointer_link *link = view->nodes[source].first;
    uint64_t source_checksum = CFLOW_BRANCHING_CHECKSUM_SEED;
    while (link) {
      if (link->target >= view->node_count || observation.edge_count == SIZE_MAX) {
        observation.ok = false;
        return observation;
      }
      source_checksum = mix_target(source_checksum, link->target);
      ++observation.edge_count;
      link = link->next;
    }
    observation_commit_source(&observation, source, source_checksum);
  }
  observation.ok = observation.edge_count == view->edge_count;
  return observation;
}

static void restore_offsets(size_t *offsets, size_t node_count) {
  size_t previous = 0u;
  size_t source;
  for (source = 0u; source <= node_count; ++source) {
    size_t current = offsets[source];
    offsets[source] = previous;
    previous = current;
  }
}

static void build_prefix_offsets(size_t *offsets, const cflow_subgraph *subgraph) {
  size_t edge_id;
  size_t source;
  memset(offsets, 0, (subgraph->node_count + 1u) * sizeof(*offsets));
  for (edge_id = 0u; edge_id < subgraph->edge_count; ++edge_id)
    ++offsets[subgraph->edges[edge_id].from + 1u];
  for (source = 1u; source <= subgraph->node_count; ++source)
    offsets[source] += offsets[source - 1u];
}

static void stable_group_targets(size_t *offsets, cflow_node_id *targets,
                                 const cflow_subgraph *subgraph) {
  size_t edge_id;
  for (edge_id = 0u; edge_id < subgraph->edge_count; ++edge_id) {
    const cflow_edge *edge = &subgraph->edges[edge_id];
    targets[offsets[edge->from]++] = edge->to;
  }
  restore_offsets(offsets, subgraph->node_count);
}

cflow_branching_view_status cflow_branching_csr_build(cflow_branching_csr_view *out,
                                                      const cflow_subgraph *subgraph,
                                                      const cflow_branching_allocator *allocator) {
  cflow_branching_csr_view built = {0};
  cflow_branching_view_status status;
  size_t offset_bytes;
  size_t target_bytes;
  size_t current = 0u;

  if (!out) return CFLOW_BRANCHING_VIEW_INVALID_ARGUMENT;
  memset(out, 0, sizeof(*out));
  status = validate_shape(subgraph, &offset_bytes, &target_bytes);
  if (status != CFLOW_BRANCHING_VIEW_OK) return status;
  if (!resolve_allocator(allocator, &built.allocator)) return CFLOW_BRANCHING_VIEW_INVALID_ARGUMENT;

  built.offsets = (size_t *)built.allocator.allocate(offset_bytes, built.allocator.context);
  if (!built.offsets) return CFLOW_BRANCHING_VIEW_ALLOCATION_FAILED;
  if (!memory_add(&built.memory, offset_bytes, &current)) goto overflow;
  if (target_bytes) {
    built.targets =
        (cflow_node_id *)built.allocator.allocate(target_bytes, built.allocator.context);
    if (!built.targets) goto allocation_failed;
    if (!memory_add(&built.memory, target_bytes, &current)) goto overflow;
  }
  build_prefix_offsets(built.offsets, subgraph);
  if (target_bytes) stable_group_targets(built.offsets, built.targets, subgraph);
  built.node_count = subgraph->node_count;
  built.edge_count = subgraph->edge_count;
  built.memory.retained_bytes = current;
  *out = built;
  return CFLOW_BRANCHING_VIEW_OK;

overflow:
  status = CFLOW_BRANCHING_VIEW_OVERFLOW;
  goto fail;
allocation_failed:
  status = CFLOW_BRANCHING_VIEW_ALLOCATION_FAILED;
fail:
  if (built.targets) built.allocator.deallocate(built.targets, built.allocator.context);
  built.allocator.deallocate(built.offsets, built.allocator.context);
  return status;
}

void cflow_branching_csr_destroy(cflow_branching_csr_view *view) {
  if (!view) return;
  if (view->targets && view->allocator.deallocate)
    view->allocator.deallocate(view->targets, view->allocator.context);
  if (view->offsets && view->allocator.deallocate)
    view->allocator.deallocate(view->offsets, view->allocator.context);
  memset(view, 0, sizeof(*view));
}

cflow_branching_observation cflow_branching_observe_csr(const cflow_branching_csr_view *view) {
  cflow_branching_observation observation = observation_begin(0u);
  size_t source;
  if (!view || !view->offsets || (view->edge_count && !view->targets) || view->offsets[0] != 0u ||
      view->offsets[view->node_count] != view->edge_count) {
    observation.ok = false;
    return observation;
  }
  observation.node_count = view->node_count;
  for (source = 0u; source < view->node_count; ++source) {
    size_t edge_id;
    uint64_t source_checksum = CFLOW_BRANCHING_CHECKSUM_SEED;
    if (view->offsets[source] > view->offsets[source + 1u] ||
        view->offsets[source + 1u] > view->edge_count) {
      observation.ok = false;
      return observation;
    }
    for (edge_id = view->offsets[source]; edge_id < view->offsets[source + 1u]; ++edge_id) {
      if (view->targets[edge_id] >= view->node_count) {
        observation.ok = false;
        return observation;
      }
      source_checksum = mix_target(source_checksum, view->targets[edge_id]);
      ++observation.edge_count;
    }
    observation_commit_source(&observation, source, source_checksum);
  }
  observation.ok = observation.edge_count == view->edge_count;
  return observation;
}

static bool sequence_request_bytes(size_t payload, size_t alignment, size_t *out) {
  size_t overhead;
  if (!out || !alignment || (alignment & (alignment - 1u))) return false;
  if (!payload) {
    *out = 0u;
    return true;
  }
  return checked_add(sizeof(void *), alignment - 1u, &overhead) &&
         checked_add(payload, overhead, out);
}

static bool hash_storage_bytes(const hash_map_t *map, size_t *out) {
  size_t values_offset;
  size_t values_bytes;
  size_t payload_bytes;
  size_t alignment;
  if (!map || !out) return false;
  if (!map->capacity) {
    *out = 0u;
    return true;
  }
  if (!map->states || !map->values || map->values < map->states ||
      !checked_mul(map->capacity, map->value_stride, &values_bytes))
    return false;
  values_offset = (size_t)(map->values - map->states);
  if (!checked_add(values_offset, values_bytes, &payload_bytes)) return false;
  alignment = _Alignof(size_t);
  if (map->key_align > alignment) alignment = map->key_align;
  if (map->value_align > alignment) alignment = map->value_align;
  return sequence_request_bytes(payload_bytes, alignment, out);
}

cflow_branching_view_status cflow_branching_hash_build(cflow_branching_hash_view *out,
                                                       const cflow_subgraph *subgraph,
                                                       const cflow_branching_allocator *allocator) {
  cflow_branching_hash_view built = {0};
  cflow_branching_view_status status;
  size_t offset_bytes;
  size_t target_bytes;
  size_t *offsets = NULL;
  size_t source_count = 0u;
  size_t map_bytes = 0u;
  size_t transient_bytes = 0u;
  size_t key_request_bytes;
  size_t value_request_bytes;
  size_t current = 0u;
  size_t source;

  if (!out) return CFLOW_BRANCHING_VIEW_INVALID_ARGUMENT;
  memset(out, 0, sizeof(*out));
  status = validate_shape(subgraph, &offset_bytes, &target_bytes);
  if (status != CFLOW_BRANCHING_VIEW_OK) return status;
  if (!resolve_allocator(allocator, &built.allocator)) return CFLOW_BRANCHING_VIEW_INVALID_ARGUMENT;

  offsets = (size_t *)built.allocator.allocate(offset_bytes, built.allocator.context);
  if (!offsets) return CFLOW_BRANCHING_VIEW_ALLOCATION_FAILED;
  if (!memory_add(&built.memory, offset_bytes, &current)) goto overflow;
  if (target_bytes) {
    built.targets =
        (cflow_node_id *)built.allocator.allocate(target_bytes, built.allocator.context);
    if (!built.targets) goto allocation_failed;
    if (!memory_add(&built.memory, target_bytes, &current)) goto overflow;
  }
  build_prefix_offsets(offsets, subgraph);
  if (target_bytes) stable_group_targets(offsets, built.targets, subgraph);
  for (source = 0u; source < subgraph->node_count; ++source)
    if (offsets[source] != offsets[source + 1u]) ++source_count;

  if (hash_map_init_bytes(&built.spans, sizeof(cflow_node_id), _Alignof(cflow_node_id),
                          sizeof(cflow_branching_hash_span), _Alignof(cflow_branching_hash_span),
                          source_count, hash_bytes, hash_key_equal, NULL) != STL_OK)
    goto allocation_failed;
  built.initialized = true;
  if (hash_map_reserve(&built.spans, source_count) != STL_OK) goto allocation_failed;
  if (!hash_storage_bytes(&built.spans, &map_bytes)) goto overflow;
  if (map_bytes && !memory_add(&built.memory, map_bytes, &current)) goto overflow;
  if (!sequence_request_bytes(built.spans.key_stride, built.spans.key_align, &key_request_bytes) ||
      !sequence_request_bytes(built.spans.value_stride, built.spans.value_align,
                              &value_request_bytes) ||
      !checked_add(key_request_bytes, value_request_bytes, &transient_bytes))
    goto overflow;

  for (source = 0u; source < subgraph->node_count; ++source) {
    cflow_node_id key;
    cflow_branching_hash_span span;
    if (offsets[source] == offsets[source + 1u]) continue;
    key = (cflow_node_id)source;
    span.offset = offsets[source];
    span.count = offsets[source + 1u] - offsets[source];
    if (hash_map_put(&built.spans, &key, &span) != STL_OK) goto allocation_failed;
    if (!memory_add_transient(&built.memory, 2u, transient_bytes, current)) goto overflow;
  }
  built.allocator.deallocate(offsets, built.allocator.context);
  offsets = NULL;
  current -= offset_bytes;
  built.node_count = subgraph->node_count;
  built.edge_count = subgraph->edge_count;
  built.memory.retained_bytes = current;
  *out = built;
  return CFLOW_BRANCHING_VIEW_OK;

overflow:
  status = CFLOW_BRANCHING_VIEW_OVERFLOW;
  goto fail;
allocation_failed:
  status = CFLOW_BRANCHING_VIEW_ALLOCATION_FAILED;
fail:
  if (built.initialized) hash_map_destroy(&built.spans);
  if (built.targets) built.allocator.deallocate(built.targets, built.allocator.context);
  if (offsets) built.allocator.deallocate(offsets, built.allocator.context);
  return status;
}

void cflow_branching_hash_destroy(cflow_branching_hash_view *view) {
  if (!view) return;
  if (view->initialized) hash_map_destroy(&view->spans);
  if (view->targets && view->allocator.deallocate)
    view->allocator.deallocate(view->targets, view->allocator.context);
  memset(view, 0, sizeof(*view));
}

cflow_branching_observation cflow_branching_observe_hash(const cflow_branching_hash_view *view) {
  cflow_branching_observation observation = observation_begin(0u);
  size_t source;
  if (!view || !view->initialized || (view->edge_count && !view->targets)) {
    observation.ok = false;
    return observation;
  }
  observation.node_count = view->node_count;
  for (source = 0u; source < view->node_count; ++source) {
    const cflow_node_id key = (cflow_node_id)source;
    const cflow_branching_hash_span *span =
        (const cflow_branching_hash_span *)hash_map_get_const(&view->spans, &key);
    uint64_t source_checksum = CFLOW_BRANCHING_CHECKSUM_SEED;
    if (span) {
      size_t end;
      size_t edge_id;
      if (!checked_add(span->offset, span->count, &end) || end > view->edge_count) {
        observation.ok = false;
        return observation;
      }
      for (edge_id = span->offset; edge_id < end; ++edge_id) {
        if (view->targets[edge_id] >= view->node_count) {
          observation.ok = false;
          return observation;
        }
        source_checksum = mix_target(source_checksum, view->targets[edge_id]);
        ++observation.edge_count;
      }
    }
    observation_commit_source(&observation, source, source_checksum);
  }
  observation.ok = observation.edge_count == view->edge_count;
  return observation;
}
