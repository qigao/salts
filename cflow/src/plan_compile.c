#include <cflow/plan_internal.h>
#include <cflow/lower.h>
#include <cflow/opt.h>

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

static bool append_inst(cflow_plan *p, cflow_plan_inst inst) {
    cflow_plan_impl *impl = plan_impl(p);
    if (!impl) return false;
    cflow_plan_inst *q = realloc(impl->code, (impl->count + 1u) * sizeof(*q));
    if (!q) return false;
    impl->code = q;
    impl->code[impl->count++] = inst;
    return true;
}

static bool prepare_unary_call(cflow_plan_call *out, cmeta_callable fn) {
    const cmeta_sig_desc *sig;
    if (!out || !fn.invoke) return false;
    sig = cmeta_callable_signature(fn);
    if (!sig || sig->protocol != CMETA_FN_PROTOCOL_VALUE || sig->param_count != 1u ||
        !sig->params[0] || !sig->return_type)
        return false;
    out->fn = fn;
    out->invoke = fn.invoke;
    out->input_type = sig->params[0];
    out->output_type = sig->return_type;
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

bool cflow_plan_graph_supported(const cflow_graph *graph) {
    if (!graph || !cflow_graph_is_normalized(graph) || graph->root >= graph->subgraph_count)
        return false;
    const cflow_subgraph *sg = &graph->subgraphs[graph->root];
    if (!sg->node_count || sg->entry >= sg->node_count) return false;
    cflow_node_id id = sg->entry;
    size_t visited = 0;
    while (id != CMETA_INVALID_ID) {
        if (++visited > sg->node_count) return false;
        const cflow_node *n = cflow_subgraph_node(sg, id);
        if (!n) return false;
        if (n->op != CFLOW_OP_SOURCE) {
            bool ok = false;
            (void)opcode_for(n, &ok);
            if (!ok || n->op == CFLOW_OP_RELATION || n->subgraph_count) return false;
        }
        size_t degree = cflow_subgraph_out_degree(sg, id);
        if (!degree) break;
        cflow_node_id next = CMETA_INVALID_ID;
        if (degree != 1u || !cflow_subgraph_single_successor(sg, id, &next)) return false;
        id = next;
    }
    return true;
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
    if (!cflow_plan_graph_supported(graph)) return plan_fail(plan, "Graph contains a semantic not supported by direct plan");

    cflow_plan_impl *impl = calloc(1, sizeof(*impl));
    if (!impl) return plan_fail(plan, "allocation failed");
    plan->impl = impl;
    const cflow_subgraph *sg = &graph->subgraphs[graph->root];
    plan->input_type = sg->input_type;
    plan->output_type = sg->output_type;

    cflow_node_id id = sg->entry;
    size_t visited = 0;
    while (id != CMETA_INVALID_ID) {
        if (++visited > sg->node_count) { cflow_plan_destroy(plan); return plan_fail(plan, "cycle while compiling plan"); }
        const cflow_node *n = cflow_subgraph_node(sg, id);
        if (!n) { cflow_plan_destroy(plan); return plan_fail(plan, "invalid node id while compiling plan"); }
        if (stats) ++stats->graph_nodes;
        if (n->op != CFLOW_OP_SOURCE) {
            bool ok = false;
            cflow_plan_opcode op = opcode_for(n, &ok);
            cflow_plan_step_fn step = ok ? cflow_plan_step_for_opcode(op) : NULL;
            if (!ok || !step) { cflow_plan_destroy(plan); return plan_fail(plan, "plan step is unavailable"); }
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
                    cflow_plan_destroy(plan);
                    return plan_fail(plan, "filter callable predecode failed");
                }
            } else if (op == CMETA_PLAN_MAP) {
                const cmeta_callable *src = n->fn_chain_count ? n->fn_chain : &n->fn;
                size_t count = n->fn_chain_count ? n->fn_chain_count : 1u;
                inst.fn_chain = malloc(count * sizeof(*inst.fn_chain));
                if (!inst.fn_chain) { cflow_plan_destroy(plan); return plan_fail(plan, "allocation failed"); }
                const cmeta_type_desc *expected_input = n->input_type;
                for (size_t k = 0; k < count; ++k) {
                    if (!prepare_unary_call(&inst.fn_chain[k], src[k]) ||
                        !cmeta_type_equal(inst.fn_chain[k].input_type, expected_input)) {
                        inst_destroy(&inst);
                        cflow_plan_destroy(plan);
                        return plan_fail(plan, "map callable predecode failed");
                    }
                    expected_input = inst.fn_chain[k].output_type;
                }
                if (!cmeta_type_equal(expected_input, n->output_type)) {
                    inst_destroy(&inst);
                    cflow_plan_destroy(plan);
                    return plan_fail(plan, "map callable output type mismatch");
                }
                inst.fn_chain_count = count;
                if (stats) stats->map_callbacks += count;
            } else {
                inst.call.fn = n->fn;
                inst.call.invoke = n->fn.invoke;
            }
            if (!append_inst(plan, inst)) {
                inst_destroy(&inst); cflow_plan_destroy(plan); return plan_fail(plan, "allocation failed");
            }
            if (stats) ++stats->instructions;
        }
        size_t degree = cflow_subgraph_out_degree(sg, id);
        if (!degree) break;
        cflow_node_id next = CMETA_INVALID_ID;
        if (degree != 1u || !cflow_subgraph_single_successor(sg, id, &next)) {
            cflow_plan_destroy(plan); return plan_fail(plan, "direct plan requires a single data path");
        }
        id = next;
    }
    prepare_fused_value(impl);
    plan->error = NULL;
    return true;
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
