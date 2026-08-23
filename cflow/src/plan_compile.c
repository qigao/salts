#include <cflow/plan_internal.h>
#include <cflow/lower.h>
#include <cflow/opt.h>
#include <cflow/property.h>
#include "dense_successor_index.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static cflow_plan_impl *plan_impl(cflow_plan *p) {
    return p ? (cflow_plan_impl *)p->impl : NULL;
}

static void inst_destroy(cflow_plan_inst *i) {
    if (!i) return;
    free(i->fn_chain);
    memset(i, 0, sizeof(*i));
}

void cflow_plan_destroy(cflow_plan *plan) {
    if (!plan) return;
    cflow_plan_impl *impl = plan_impl(plan);
    if (impl) {
        for (size_t i = 0; i < impl->count; ++i) inst_destroy(&impl->code[i]);
        free(impl->code);
        free(impl);
    }
    memset(plan, 0, sizeof(*plan));
}

static bool plan_fail(cflow_plan *p, const char *msg) {
    if (p) p->error = msg;
    return false;
}

static bool prepare_unary_call(cflow_plan_call *out, cmeta_callable fn) {
    cmeta_callable bound;
    cflow_plan_call prepared = {0};
    const cmeta_sig_desc *sig;
    if (!out || !fn.invoke) return false;
    if (!cmeta_callable_bind(fn, &bound)) return false;
    sig = cmeta_fn_signature(bound.meta);
    if (!sig || sig->protocol != CMETA_FN_PROTOCOL_VALUE || sig->param_count != 1u ||
        !sig->params[0] || !sig->return_type)
        return false;
    prepared.fn = bound;
    prepared.invoke = bound.invoke;
    if (cmeta_callable_can_dispatch_canonical_raw(bound)) {
        prepared.raw_batch = cflow_plan_unary_batch_for_signature(bound.meta.sig);
        if (!prepared.raw_batch) return false;
    }
    prepared.input_type = sig->params[0];
    prepared.output_type = sig->return_type;
    *out = prepared;
    return true;
}

static bool checked_add(size_t left, size_t right, size_t *sum) {
    if (!sum || left > SIZE_MAX - right) return false;
    *sum = left + right;
    return true;
}

static bool call_is_fusible_value(const cflow_plan_call *call) {
    const cmeta_properties required = CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS;
    return call && call->invoke && cmeta_type_desc_valid(call->input_type) &&
           call->input_type->size && cmeta_type_desc_valid(call->output_type) &&
           call->output_type->size && cmeta_callable_contract_valid(call->fn) &&
           cmeta_effects_are_pure(call->fn.meta.effects) &&
           cmeta_properties_include(call->fn.meta.properties, required);
}

static void prepare_fused_value(cflow_plan_impl *impl) {
    bool saw_map = false;
    size_t filter_count = 0u;
    size_t map_call_count = 0u;

    if (!impl || !impl->count) return;
    for (size_t pc = 0u; pc < impl->count; ++pc) {
        const cflow_plan_inst *inst = &impl->code[pc];
        if (inst->opcode == CMETA_PLAN_FILTER) {
            if (saw_map || !call_is_fusible_value(&inst->call) ||
                filter_count == SIZE_MAX)
                return;
            ++filter_count;
            continue;
        }
        if (inst->opcode != CMETA_PLAN_MAP || !inst->fn_chain_count) return;
        saw_map = true;
        for (size_t k = 0u; k < inst->fn_chain_count; ++k) {
            if (!call_is_fusible_value(&inst->fn_chain[k]) ||
                !checked_add(map_call_count, 1u, &map_call_count))
                return;
        }
    }

    impl->fused_filter_count = filter_count;
    impl->fused_map_call_count = map_call_count;
    impl->fused_value = true;
}

static bool call_is_parallel_prefix(const cflow_plan_call *call) {
    const cmeta_properties required = CMETA_PROP_DETERMINISTIC |
        CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS;
    return call && cmeta_callable_contract_valid(call->fn) &&
        cmeta_effects_are_pure(call->fn.meta.effects) &&
        cmeta_properties_include(call->fn.meta.properties, required);
}

static void prepare_parallel_reduce(cflow_plan_impl *impl) {
    const cflow_plan_inst *reduce;

    if (!impl) return;
    impl->terminal_reduce_index = SIZE_MAX;
    if (!impl->count) return;
    reduce = &impl->code[impl->count - 1u];
    if (reduce->opcode != CMETA_PLAN_REDUCE ||
        !cmeta_type_equal(reduce->input_type, reduce->output_type) ||
        !cflow_callable_declares_associative_endomap(reduce->call.fn))
        return;

    for (size_t pc = 0u; pc + 1u < impl->count; ++pc) {
        const cflow_plan_inst *inst = &impl->code[pc];
        if (inst->opcode == CMETA_PLAN_FILTER) {
            if (!call_is_parallel_prefix(&inst->call)) return;
        } else if (inst->opcode == CMETA_PLAN_MAP) {
            if (!inst->fn_chain_count) return;
            for (size_t k = 0u; k < inst->fn_chain_count; ++k)
                if (!call_is_parallel_prefix(&inst->fn_chain[k])) return;
        } else if (inst->opcode == CMETA_PLAN_FLAT_MAP) {
            if (!call_is_parallel_prefix(&inst->call)) return;
        } else {
            return;
        }
    }

    impl->terminal_reduce_index = impl->count - 1u;
    impl->parallel_reduce_supported = true;
}

static cflow_plan_opcode opcode_for(const cflow_node *n, bool *ok) {
    *ok = true;
    switch (n->op) {
        case CFLOW_OP_FILTER: return CMETA_PLAN_FILTER;
        case CFLOW_OP_MAP:
        case CFLOW_OP_TRANSFORM: return CMETA_PLAN_MAP;
        case CFLOW_OP_FLAT_MAP: return CMETA_PLAN_FLAT_MAP;
        case CFLOW_OP_REDUCE: return CMETA_PLAN_REDUCE;
        default: *ok = false; return CMETA_PLAN_MAP;
    }
}

static bool plan_path_supported(const cflow_subgraph *subgraph,
                                const cflow_dense_successor_index *index,
                                size_t *instruction_count) {
    cflow_node_id id;
    size_t instructions = 0u;
    size_t visited = 0u;

    if (!subgraph || !index || !subgraph->node_count ||
        subgraph->entry >= subgraph->node_count || index->has_fanout)
        return false;
    id = subgraph->entry;
    for (;;) {
        cflow_node_id successor;
        const cflow_node *node;
        if (++visited > subgraph->node_count) return false;
        node = cflow_subgraph_node(subgraph, id);
        if (!node) return false;
        if (node->op != CFLOW_OP_SOURCE) {
            bool supported = false;
            (void)opcode_for(node, &supported);
            if (!supported || node->op == CFLOW_OP_RELATION || node->subgraph_count)
                return false;
            ++instructions;
        }
        if (!cflow_dense_successor_index_successor(index, id, &successor)) break;
        id = successor;
    }
    if (instruction_count) *instruction_count = instructions;
    return true;
}

static bool plan_compile_fail(cflow_plan *plan,
                              cflow_dense_successor_index *index,
                              const char *message) {
    cflow_dense_successor_index_destroy(index);
    cflow_plan_destroy(plan);
    return plan_fail(plan, message);
}

bool cflow_plan_graph_supported(const cflow_graph *graph) {
    cflow_dense_successor_index index = {0};
    bool supported;

    if (!graph || !cflow_graph_is_normalized(graph) || graph->root >= graph->subgraph_count)
        return false;
    const cflow_subgraph *sg = &graph->subgraphs[graph->root];
    if (cflow_dense_successor_index_build(&index, sg) != CFLOW_DENSE_SUCCESSOR_INDEX_OK)
        return false;
    supported = plan_path_supported(sg, &index, NULL);
    cflow_dense_successor_index_destroy(&index);
    return supported;
}

bool cflow_plan_compile(cflow_plan *plan,
                        const cflow_graph *graph,
                        cflow_plan_compile_stats *stats) {
    if (!plan || !graph) return false;
    cflow_plan_destroy(plan);
    if (stats) memset(stats, 0, sizeof(*stats));
    if (!cflow_graph_is_normalized(graph)) return plan_fail(plan, "plan requires normalized Graph");
    const char *err = NULL;
    if (!cflow_graph_validate(graph, &err)) return plan_fail(plan, err ? err : "invalid Graph");
    const cflow_subgraph *sg = &graph->subgraphs[graph->root];
    cflow_dense_successor_index index = {0};
    cflow_dense_successor_index_status index_status =
        cflow_dense_successor_index_build(&index, sg);
    if (index_status != CFLOW_DENSE_SUCCESSOR_INDEX_OK)
        return plan_fail(plan, index_status == CFLOW_DENSE_SUCCESSOR_INDEX_ALLOCATION_FAILED
                                   ? "allocation failed"
                                   : "Graph contains invalid topology");
    size_t instruction_count = 0u;
    if (!plan_path_supported(sg, &index, &instruction_count)) {
        cflow_dense_successor_index_destroy(&index);
        return plan_fail(plan, "Graph contains a semantic not supported by direct plan");
    }

    cflow_plan_impl *impl = calloc(1, sizeof(*impl));
    if (!impl) {
        cflow_dense_successor_index_destroy(&index);
        return plan_fail(plan, "allocation failed");
    }
    impl->terminal_reduce_index = SIZE_MAX;
    if (instruction_count) {
        if (instruction_count > SIZE_MAX / sizeof(*impl->code)) {
            free(impl);
            cflow_dense_successor_index_destroy(&index);
            return plan_fail(plan, "allocation failed");
        }
        impl->code = calloc(instruction_count, sizeof(*impl->code));
        if (!impl->code) {
            free(impl);
            cflow_dense_successor_index_destroy(&index);
            return plan_fail(plan, "allocation failed");
        }
    }
    plan->impl = impl;
    plan->input_type = sg->input_type;
    plan->output_type = sg->output_type;

    cflow_node_id id = sg->entry;
    size_t visited = 0;
    for (;;) {
        if (++visited > sg->node_count)
            return plan_compile_fail(plan, &index, "cycle while compiling plan");
        const cflow_node *n = cflow_subgraph_node(sg, id);
        if (!n) return plan_compile_fail(plan, &index, "invalid node id while compiling plan");
        if (stats) ++stats->graph_nodes;
        if (n->op != CFLOW_OP_SOURCE) {
            bool ok = false;
            cflow_plan_opcode op = opcode_for(n, &ok);
            cflow_plan_step_fn step = ok ? cflow_plan_step_for_opcode(op) : NULL;
            if (!ok || !step) return plan_compile_fail(plan, &index, "plan step is unavailable");
            cflow_plan_inst inst;
            memset(&inst, 0, sizeof(inst));
            inst.opcode = op;
            inst.step = step;
            inst.input_type = n->input_type;
            inst.output_type = n->output_type;
            if (op == CMETA_PLAN_FILTER) {
                if (!prepare_unary_call(&inst.call, n->fn) ||
                    !cmeta_type_equal(inst.call.input_type, n->input_type) ||
                    !cmeta_type_equal(inst.call.output_type, &cmeta_type_bool)) {
                    return plan_compile_fail(plan, &index, "filter callable predecode failed");
                }
            } else if (op == CMETA_PLAN_MAP) {
                const cmeta_callable *src = n->fn_chain_count ? n->fn_chain : &n->fn;
                size_t count = n->fn_chain_count ? n->fn_chain_count : 1u;
                inst.fn_chain = malloc(count * sizeof(*inst.fn_chain));
                if (!inst.fn_chain) return plan_compile_fail(plan, &index, "allocation failed");
                const cmeta_type_desc *expected_input = n->input_type;
                for (size_t k = 0; k < count; ++k) {
                    if (!prepare_unary_call(&inst.fn_chain[k], src[k]) ||
                        !cmeta_type_equal(inst.fn_chain[k].input_type, expected_input)) {
                        inst_destroy(&inst);
                        return plan_compile_fail(plan, &index, "map callable predecode failed");
                    }
                    expected_input = inst.fn_chain[k].output_type;
                }
                if (!cmeta_type_equal(expected_input, n->output_type)) {
                    inst_destroy(&inst);
                    return plan_compile_fail(plan, &index, "map callable output type mismatch");
                }
                inst.fn_chain_count = count;
                if (stats) stats->map_callbacks += count;
            } else {
                inst.call.fn = n->fn;
                inst.call.invoke = n->fn.invoke;
            }
            if (impl->count >= instruction_count) {
                inst_destroy(&inst);
                return plan_compile_fail(plan, &index, "plan instruction count changed");
            }
            impl->code[impl->count++] = inst;
            if (stats) ++stats->instructions;
        }
        cflow_node_id successor;
        if (!cflow_dense_successor_index_successor(&index, id, &successor)) break;
        id = successor;
    }
    cflow_dense_successor_index_destroy(&index);
    prepare_parallel_reduce(impl);
    prepare_fused_value(impl);
    plan->error = NULL;
    return true;
}

bool cflow_plan_parallel_reduce_supported(const cflow_plan *plan) {
    const cflow_plan_impl *impl = plan ? (const cflow_plan_impl *)plan->impl : NULL;
    return impl && impl->parallel_reduce_supported &&
        impl->terminal_reduce_index < impl->count;
}

bool cflow_plan_compile_surface(cflow_plan *plan,
                                const cflow_graph *surface,
                                cflow_plan_compile_stats *stats) {
    if (!plan || !surface) return false;
    cflow_graph normalized = {0}, optimized = {0};
    normalized.root = optimized.root = CMETA_INVALID_ID;
    bool ok = false;
    if (!cflow_graph_normalize(&normalized, surface)) {
        plan_fail(plan, normalized.error ? normalized.error : "normalization failed");
        goto done;
    }
    if (!cflow_graph_optimize(&optimized, &normalized,
                              (cflow_opt_options){ CMETA_OPT_DEFAULT }, NULL)) {
        plan_fail(plan, optimized.error ? optimized.error : "optimization failed");
        goto done;
    }
    ok = cflow_plan_compile(plan, &optimized, stats);
done:
    cflow_graph_destroy(&optimized);
    cflow_graph_destroy(&normalized);
    return ok;
}
