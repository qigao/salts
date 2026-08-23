#include <cflow/direct.h>
#include <cflow/lower.h>
#include <cflow/property.h>

#include <string.h>

static bool aot_fail(const char **error, const char *message) {
  if (error != NULL) *error = message;
  return false;
}

static bool aot_stage_contract_valid(const cflow_aot_stage_ir *stage,
                                     const cmeta_type_desc *flow_type,
                                     const char **error) {
  const cmeta_properties required =
      CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS;
  cmeta_callable bound;
  const cmeta_sig_desc *signature;

  if (stage == NULL || stage->callable == NULL ||
      (stage->kind != CFLOW_DIRECT_STAGE_FILTER &&
       stage->kind != CFLOW_DIRECT_STAGE_MAP))
    return aot_fail(error, "AOT Stage IR has an invalid stage");
  if (!cflow_direct_type_eligible(stage->input_type) ||
      !cflow_direct_type_eligible(stage->output_type) ||
      !cmeta_type_equal(stage->input_type, flow_type))
    return aot_fail(error, "AOT Stage IR type chain is invalid");
  if (!cmeta_callable_bind(*stage->callable, &bound) ||
      !cmeta_effects_are_pure(bound.meta.effects) ||
      !cmeta_properties_include(bound.meta.properties, required))
    return aot_fail(error, "AOT Stage IR callable contract is ineligible");

  signature = cmeta_fn_signature(bound.meta);
  if (signature == NULL || signature->protocol != CMETA_FN_PROTOCOL_VALUE ||
      signature->param_count != 1u ||
      !cmeta_type_equal(signature->params[0], stage->input_type))
    return aot_fail(error, "AOT Stage IR callable signature is invalid");
  if (stage->kind == CFLOW_DIRECT_STAGE_FILTER) {
    if (!cmeta_type_equal(signature->return_type, &cmeta_type_bool) ||
        !cmeta_type_equal(stage->output_type, stage->input_type))
      return aot_fail(error, "AOT Filter must return bool and preserve its input type");
  } else if (!cmeta_type_equal(signature->return_type, stage->output_type)) {
    return aot_fail(error, "AOT Map output type does not match its callable");
  }

  switch (stage->dispatch) {
    case CFLOW_AOT_DISPATCH_STATIC_TARGET:
      if (bound.capture_size != 0u || stage->target_name == NULL ||
          stage->target_name[0] == '\0')
        return aot_fail(error, "AOT StaticTarget requires zero capture and a target name");
      break;
    case CFLOW_AOT_DISPATCH_CANONICAL_RAW_BATCH:
      if (bound.capture_size != 0u ||
          !cmeta_callable_can_dispatch_canonical_raw(bound))
        return aot_fail(error, "AOT CanonicalRawBatch capability is contradictory");
      break;
    case CFLOW_AOT_DISPATCH_ADAPTER:
      if (bound.invoke == NULL)
        return aot_fail(error, "AOT Adapter dispatch requires an invocation adapter");
      break;
    default:
      return aot_fail(error, "AOT Stage IR dispatch is invalid");
  }
  return true;
}

/* Time O(stages), auxiliary space O(1); stage_count is bounded by 16. */
bool cflow_aot_pipeline_ir_validate(const cflow_aot_pipeline_ir *ir,
                                    const char **error) {
  const cmeta_type_desc *flow_type;

  if (error != NULL) *error = NULL;
  if (ir == NULL || ir->stages == NULL || ir->stage_count == 0u ||
      ir->stage_count > CFLOW_AOT_STAGE_LIMIT ||
      !cflow_direct_type_eligible(ir->input_type) ||
      !cflow_direct_type_eligible(ir->output_type))
    return aot_fail(error, "AOT pipeline IR representation is invalid");

  flow_type = ir->input_type;
  for (size_t index = 0u; index < ir->stage_count; ++index) {
    if (!aot_stage_contract_valid(&ir->stages[index], flow_type, error))
      return false;
    flow_type = ir->stages[index].output_type;
  }
  if (!cmeta_type_equal(flow_type, ir->output_type))
    return aot_fail(error, "AOT pipeline output type does not match its final stage");
  return true;
}

bool cflow_aot_pipeline_ir_inline_eligible(const cflow_aot_pipeline_ir *ir,
                                           const char **error) {
  if (!cflow_aot_pipeline_ir_validate(ir, error)) return false;
  for (size_t index = 0u; index < ir->stage_count; ++index)
    if (ir->stages[index].dispatch != CFLOW_AOT_DISPATCH_STATIC_TARGET)
      return aot_fail(error, "AOT pipeline contains a runtime dispatch stage");
  return true;
}

static bool aot_node_matches_stages(const cflow_node *node,
                                    const cflow_aot_pipeline_ir *ir,
                                    size_t *stage_index) {
  const cmeta_callable *calls;
  size_t call_count;
  size_t first;

  if (node == NULL || ir == NULL || stage_index == NULL || !node->has_fn ||
      node->has_relation || node->subgraph_count != 0u ||
      *stage_index >= ir->stage_count)
    return false;
  first = *stage_index;
  if (node->op == CFLOW_OP_FILTER) {
    if (node->fn_chain_count != 0u ||
        ir->stages[first].kind != CFLOW_DIRECT_STAGE_FILTER ||
        !cmeta_callable_same(node->fn, *ir->stages[first].callable))
      return false;
    ++*stage_index;
  } else if (node->op == CFLOW_OP_MAP) {
    calls = node->fn_chain_count != 0u ? node->fn_chain : &node->fn;
    call_count = node->fn_chain_count != 0u ? node->fn_chain_count : 1u;
    if (calls == NULL || call_count > ir->stage_count - first) return false;
    for (size_t index = 0u; index < call_count; ++index) {
      const cflow_aot_stage_ir *stage = &ir->stages[first + index];
      if (stage->kind != CFLOW_DIRECT_STAGE_MAP ||
          !cmeta_callable_same(calls[index], *stage->callable))
        return false;
    }
    *stage_index += call_count;
  } else {
    return false;
  }

  return cmeta_type_equal(node->input_type, ir->stages[first].input_type) &&
         cmeta_type_equal(node->output_type,
                          ir->stages[*stage_index - 1u].output_type);
}

/* Time O(Graph + stages), auxiliary space O(normalized Graph). The temporary
 * normalized representation is control-plane state and is always destroyed. */
bool cflow_aot_pipeline_ir_match_graph(const cflow_aot_pipeline_ir *ir,
                                       const cflow_graph *graph,
                                       cflow_aot_equivalence_witness *witness,
                                       const char **error) {
  cflow_graph normalized = {0};
  const cflow_subgraph *root;
  const cflow_node *source;
  cflow_node_id node_id;
  size_t stage_index = 0u;
  const char *failure = NULL;
  bool matched = false;

  normalized.root = CMETA_INVALID_ID;
  if (error != NULL) *error = NULL;
  if (witness != NULL) memset(witness, 0, sizeof(*witness));
  if (!cflow_aot_pipeline_ir_validate(ir, error)) return false;
  if (graph == NULL || graph->root >= graph->subgraph_count) {
    failure = "AOT equivalence requires a valid source Graph";
    goto done;
  }
  if (!cflow_graph_normalize(&normalized, graph)) {
    failure = "AOT equivalence could not normalize the source Graph";
    goto done;
  }
  if (normalized.subgraph_count != 1u || normalized.root >= normalized.subgraph_count) {
    failure = "AOT Stage IR requires exactly one root subgraph";
    goto done;
  }

  root = &normalized.subgraphs[normalized.root];
  if (root->node_count < 2u || root->edge_count != root->node_count - 1u ||
      root->entry >= root->node_count ||
      !cmeta_type_equal(root->input_type, ir->input_type) ||
      !cmeta_type_equal(root->output_type, ir->output_type)) {
    failure = "AOT Stage IR and Graph shape or boundary types differ";
    goto done;
  }
  source = cflow_subgraph_node(root, root->entry);
  if (source == NULL || source->op != CFLOW_OP_SOURCE || source->has_fn ||
      source->subgraph_count != 0u ||
      !cmeta_type_equal(source->output_type, ir->input_type)) {
    failure = "AOT Stage IR Graph source differs";
    goto done;
  }

  node_id = root->entry;
  while (stage_index < ir->stage_count) {
    cflow_node_id successor = CMETA_INVALID_ID;
    if (cflow_subgraph_out_degree(root, node_id) != 1u ||
        !cflow_subgraph_single_successor(root, node_id, &successor) ||
        !aot_node_matches_stages(cflow_subgraph_node(root, successor), ir,
                                 &stage_index)) {
      failure = "AOT Stage IR and Graph stage semantics differ";
      goto done;
    }
    node_id = successor;
  }
  if (cflow_subgraph_out_degree(root, node_id) != 0u || root->tail != node_id) {
    failure = "AOT Stage IR and Graph terminal topology differ";
    goto done;
  }

  if (witness != NULL) {
    witness->source_graph_version = graph->version;
    witness->matched_stage_count = ir->stage_count;
  }
  matched = true;

done:
  cflow_graph_destroy(&normalized);
  if (!matched) return aot_fail(error, failure != NULL ? failure : "AOT equivalence failed");
  if (error != NULL) *error = NULL;
  return true;
}

typedef struct aot_source_coordinate {
  cflow_node_id node;
  size_t callable_index;
} aot_source_coordinate;

static bool aot_collect_source_coordinates(
    const cflow_aot_pipeline_ir *ir,
    const cflow_graph *source,
    aot_source_coordinate coordinates[CFLOW_AOT_STAGE_LIMIT]) {
  const cflow_subgraph *root;
  cflow_node_id node_id;
  size_t stage_index = 0u;

  if (!ir || !source || source->subgraph_count != 1u ||
      source->root >= source->subgraph_count)
    return false;
  root = &source->subgraphs[source->root];
  node_id = root->entry;
  while (stage_index < ir->stage_count) {
    cflow_node_id successor = CMETA_INVALID_ID;
    size_t first = stage_index;
    const cflow_node *node;
    if (!cflow_subgraph_single_successor(root, node_id, &successor)) return false;
    node = cflow_subgraph_node(root, successor);
    if (!aot_node_matches_stages(node, ir, &stage_index)) return false;
    for (size_t index = first; index < stage_index; ++index) {
      coordinates[index].node = successor;
      coordinates[index].callable_index =
          node->op == CFLOW_OP_MAP && node->fn_chain_count ? index - first : 0u;
    }
    node_id = successor;
  }
  return node_id == root->tail && cflow_subgraph_out_degree(root, node_id) == 0u;
}

static bool aot_find_coordinate(
    const aot_source_coordinate coordinates[CFLOW_AOT_STAGE_LIMIT],
    size_t stage_count,
    cflow_node_id node,
    size_t callable_index,
    size_t *stage_index) {
  if (!stage_index) return false;
  for (size_t index = 0u; index < stage_count; ++index) {
    if (coordinates[index].node == node &&
        coordinates[index].callable_index == callable_index) {
      *stage_index = index;
      return true;
    }
  }
  return false;
}

/* Time O(stages * rewrites + Graph), auxiliary space O(stages + normalized
 * optimized Graph). Both stage arrays are hard-bounded by 16. */
bool cflow_aot_pipeline_ir_match_optimized_graph(
    const cflow_aot_pipeline_ir *ir,
    const cflow_graph *normalized_source,
    const cflow_graph *optimized,
    const cflow_opt_trace *trace,
    cflow_aot_optimized_equivalence_witness *witness,
    const char **error) {
  aot_source_coordinate coordinates[CFLOW_AOT_STAGE_LIMIT] = {{0}};
  cflow_aot_stage_ir remaining_stages[CFLOW_AOT_STAGE_LIMIT] = {{0}};
  bool removed[CFLOW_AOT_STAGE_LIMIT] = {false};
  cflow_aot_pipeline_ir remaining = {0};
  cflow_aot_equivalence_witness source_witness = {0};
  cflow_aot_equivalence_witness optimized_witness = {0};
  size_t rewrite_count;
  size_t remaining_count = 0u;

  if (error) *error = NULL;
  if (witness) memset(witness, 0, sizeof(*witness));
  if (!cflow_aot_pipeline_ir_validate(ir, error)) return false;
  if (!normalized_source || !optimized || !trace ||
      !cflow_graph_is_normalized(normalized_source))
    return aot_fail(error, "AOT certificate requires a normalized source Graph");
  if (!cflow_aot_pipeline_ir_match_graph(
          ir, normalized_source, &source_witness, error))
    return false;
  if (!cflow_opt_trace_bound_to(trace, normalized_source, optimized))
    return aot_fail(error, "AOT optimizer trace Graph binding is stale or different");
  if (!aot_collect_source_coordinates(ir, normalized_source, coordinates))
    return aot_fail(error, "AOT certificate could not index source stages");

  rewrite_count = cflow_opt_trace_count(trace);
  if (rewrite_count >= ir->stage_count)
    return aot_fail(error, "AOT optimizer trace removes too many stages");
  for (size_t index = 0u; index < rewrite_count; ++index) {
    cflow_opt_rewrite_event event = {0};
    size_t retained_index = 0u;
    size_t removed_index = 0u;
    size_t nearest;
    const cflow_aot_stage_ir *retained;
    const cflow_aot_stage_ir *deleted;

    if (!cflow_opt_trace_event_at(trace, index, &event) ||
        event.rule != CFLOW_OPT_RULE_IDEMPOTENT_MAP_ELIMINATION ||
        event.source_subgraph != normalized_source->root ||
        !aot_find_coordinate(coordinates, ir->stage_count,
                             event.retained_node,
                             event.retained_callable_index,
                             &retained_index) ||
        !aot_find_coordinate(coordinates, ir->stage_count,
                             event.removed_node,
                             event.removed_callable_index,
                             &removed_index) ||
        retained_index >= removed_index || removed[retained_index] ||
        removed[removed_index])
      return aot_fail(error, "AOT optimizer trace event coordinates are invalid");

    nearest = removed_index;
    do {
      --nearest;
    } while (removed[nearest] && nearest != 0u);
    if (nearest != retained_index)
      return aot_fail(error, "AOT optimizer trace deletion is not adjacent");

    retained = &ir->stages[retained_index];
    deleted = &ir->stages[removed_index];
    if (retained->kind != CFLOW_DIRECT_STAGE_MAP ||
        deleted->kind != CFLOW_DIRECT_STAGE_MAP ||
        !cmeta_callable_same(*retained->callable, *deleted->callable) ||
        !cflow_callable_declares_idempotent_endomap(*retained->callable) ||
        !cflow_callable_declares_idempotent_endomap(*deleted->callable))
      return aot_fail(error, "AOT optimizer trace lacks idempotent endomap premises");
    removed[removed_index] = true;
  }

  for (size_t index = 0u; index < ir->stage_count; ++index)
    if (!removed[index]) remaining_stages[remaining_count++] = ir->stages[index];
  if (remaining_count == 0u)
    return aot_fail(error, "AOT optimizer trace removed the complete pipeline");

  remaining.stages = remaining_stages;
  remaining.stage_count = remaining_count;
  remaining.input_type = ir->input_type;
  remaining.output_type = ir->output_type;
  if (!cflow_aot_pipeline_ir_match_graph(
          &remaining, optimized, &optimized_witness, error))
    return false;

  if (witness) {
    witness->source_graph_version = source_witness.source_graph_version;
    witness->optimized_graph_version = optimized_witness.source_graph_version;
    witness->matched_stage_count = ir->stage_count;
    witness->applied_rewrite_count = rewrite_count;
  }
  if (error) *error = NULL;
  return true;
}
