#include <cflow/verify.h>
#include <cflow/lower.h>
#include <cflow/plan.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t graph_node_count(const cflow_graph *g) {
    size_t n = 0;
    if (!g) return 0;
    for (size_t i = 0; i < g->subgraph_count; ++i) n += g->subgraphs[i].node_count;
    return n;
}

static bool fn_equal(cmeta_callable a, cmeta_callable b) {
    return a.meta.sig == b.meta.sig && a.meta.effects == b.meta.effects &&
           a.meta.properties == b.meta.properties && cmeta_callable_same(a, b);
}

static bool node_shallow_equal(const cflow_node *a, const cflow_node *b) {
    if (!a || !b) return a == b;
    if (a->op != b->op || a->has_fn != b->has_fn ||
        a->fn_chain_count != b->fn_chain_count ||
        a->has_relation != b->has_relation ||
        a->has_size_parameter != b->has_size_parameter ||
        a->size_parameter != b->size_parameter ||
        a->param_kind != b->param_kind ||
        !cmeta_type_equal(a->input_type, b->input_type) ||
        !cmeta_type_equal(a->output_type, b->output_type) ||
        a->subgraph_count != b->subgraph_count) return false;
    switch (a->param_kind) {
        case CFLOW_NODE_PARAM_NONE:
            break;
        case CFLOW_NODE_PARAM_TAKE:
            if (a->params.take.count != b->params.take.count) return false;
            break;
        case CFLOW_NODE_PARAM_SKIP:
            if (a->params.skip.count != b->params.skip.count) return false;
            break;
        case CFLOW_NODE_PARAM_DISTINCT:
            if (a->params.distinct.max_unique !=
                b->params.distinct.max_unique) return false;
            break;
        case CFLOW_NODE_PARAM_SORTED:
            if (a->params.sorted.max_elements !=
                b->params.sorted.max_elements) return false;
            break;
        default:
            return false;
    }
    if (a->has_fn && !fn_equal(a->fn, b->fn)) return false;
    if (a->has_relation && memcmp(&a->relation, &b->relation, sizeof(a->relation)) != 0)
        return false;
    for (size_t i = 0; i < a->fn_chain_count; ++i)
        if (!fn_equal(a->fn_chain[i], b->fn_chain[i])) return false;
    return true;
}

typedef struct graph_eq_ctx {
    const cflow_graph *a;
    const cflow_graph *b;
    cflow_subgraph_id *a_to_b;
    cflow_subgraph_id *b_to_a;
} graph_eq_ctx;

static bool subgraph_equal_rec(graph_eq_ctx *ctx,
                               cflow_subgraph_id aid,
                               cflow_subgraph_id bid) {
    if (!ctx || aid >= ctx->a->subgraph_count || bid >= ctx->b->subgraph_count) return false;
    if (ctx->a_to_b[aid] != CMETA_INVALID_ID) return ctx->a_to_b[aid] == bid;
    if (ctx->b_to_a[bid] != CMETA_INVALID_ID) return false;
    ctx->a_to_b[aid] = bid;
    ctx->b_to_a[bid] = aid;

    const cflow_subgraph *a = &ctx->a->subgraphs[aid];
    const cflow_subgraph *b = &ctx->b->subgraphs[bid];
    if (a->node_count != b->node_count || a->edge_count != b->edge_count ||
        a->entry != b->entry || a->tail != b->tail ||
        !cmeta_type_equal(a->input_type, b->input_type) ||
        !cmeta_type_equal(a->output_type, b->output_type)) return false;
    for (size_t i = 0; i < a->node_count; ++i) {
        const cflow_node *an = &a->nodes[i], *bn = &b->nodes[i];
        if (!node_shallow_equal(an, bn)) return false;
        for (size_t j = 0; j < an->subgraph_count; ++j)
            if (!subgraph_equal_rec(ctx, an->subgraphs[j], bn->subgraphs[j])) return false;
    }
    for (size_t i = 0; i < a->edge_count; ++i) {
        const cflow_edge *x = &a->edges[i], *y = &b->edges[i];
        if (x->from != y->from || x->from_port != y->from_port ||
            x->to != y->to || x->to_port != y->to_port) return false;
    }
    return true;
}

bool cflow_graph_structural_equal(const cflow_graph *a, const cflow_graph *b) {
    if (!a || !b || a->root >= a->subgraph_count || b->root >= b->subgraph_count)
        return false;
    cflow_subgraph_id *a_to_b = malloc(a->subgraph_count * sizeof(*a_to_b));
    cflow_subgraph_id *b_to_a = malloc(b->subgraph_count * sizeof(*b_to_a));
    if (!a_to_b || !b_to_a) { free(a_to_b); free(b_to_a); return false; }
    for (size_t i = 0; i < a->subgraph_count; ++i) a_to_b[i] = CMETA_INVALID_ID;
    for (size_t i = 0; i < b->subgraph_count; ++i) b_to_a[i] = CMETA_INVALID_ID;
    graph_eq_ctx ctx = { a, b, a_to_b, b_to_a };
    bool ok = subgraph_equal_rec(&ctx, a->root, b->root);
    free(a_to_b); free(b_to_a);
    return ok;
}

bool cflow_result_equal(const cflow_result *a, const cflow_result *b) {
    const cmeta_type_traits *traits;
    const unsigned char *left;
    const unsigned char *right;

    if (!a || !b || a->count != b->count || !cmeta_type_equal(a->type, b->type))
        return false;
    if (a->count == 0u) return true;
    if (!a->type || a->type->size == 0u || !a->data || !b->data ||
        a->count > SIZE_MAX / a->type->size)
        return false;

    traits = a->type->traits;
    left = (const unsigned char *)a->data;
    right = (const unsigned char *)b->data;
    if (traits && (traits->flags & CMETA_TRAIT_EQUAL) != 0u && traits->equal) {
        for (size_t i = 0; i < a->count; ++i) {
            if (!traits->equal(left + i * a->type->size,
                               right + i * a->type->size))
                return false;
        }
        return true;
    }
    return memcmp(a->data, b->data, a->count * a->type->size) == 0;
}

static bool fail(cflow_verify_report *r, const char *msg) {
    if (r) r->error = msg;
    return false;
}

bool cflow_verify_pipeline(const cflow_graph *surface,
                           const void *inputs,
                           size_t input_count,
                           cflow_verify_report *report) {
    cflow_verify_report local = {0};
    if (!report) report = &local;
    memset(report, 0, sizeof(*report));
    report->input_count = input_count;

    const char *err = NULL;
    if (!surface || !cflow_graph_validate(surface, &err))
        return fail(report, err ? err : "surface validation failed");

    cflow_graph n1 = {0}, n2 = {0}, o1 = {0}, o2 = {0};
    n1.root = n2.root = o1.root = o2.root = CMETA_INVALID_ID;
    cflow_result rs = {0}, rn = {0}, ro = {0}, rp = {0};
    cflow_plan plan = {0};
    bool ok = false;

    if (!cflow_graph_normalize(&n1, surface)) { fail(report, n1.error ? n1.error : "normalize failed"); goto done; }
    if (!cflow_graph_validate(&n1, &err) || !cflow_graph_is_normalized(&n1)) { fail(report, err ? err : "normalized invariant failed"); goto done; }
    if (!cflow_graph_normalize(&n2, &n1)) { fail(report, n2.error ? n2.error : "second normalize failed"); goto done; }
    if (!cflow_graph_structural_equal(&n1, &n2)) { fail(report, "normalization is not structurally idempotent"); goto done; }

    if (!cflow_graph_optimize(&o1, &n1, (cflow_opt_options){ CMETA_OPT_DEFAULT }, &report->opt_stats)) {
        fail(report, o1.error ? o1.error : "optimization failed"); goto done;
    }
    if (!cflow_graph_validate(&o1, &err) || !cflow_graph_is_normalized(&o1)) { fail(report, err ? err : "optimized invariant failed"); goto done; }
    if (!cflow_graph_optimize(&o2, &o1, (cflow_opt_options){ CMETA_OPT_DEFAULT }, NULL)) {
        fail(report, o2.error ? o2.error : "second optimization failed"); goto done;
    }
    if (!cflow_graph_structural_equal(&o1, &o2)) { fail(report, "optimization is not structurally idempotent"); goto done; }

    if (!cflow_eval_array(surface, inputs, input_count, &rs)) { fail(report, "surface execution failed"); goto done; }
    if (!cflow_eval_array(&n1, inputs, input_count, &rn)) { fail(report, "normalized execution failed"); goto done; }
    if (!cflow_eval_array(&o1, inputs, input_count, &ro)) { fail(report, "optimized execution failed"); goto done; }
    if (!cflow_result_equal(&rs, &rn)) { fail(report, "surface/normalized trace mismatch"); goto done; }
    if (!cflow_result_equal(&rn, &ro)) { fail(report, "normalized/optimized trace mismatch"); goto done; }

    if (cflow_plan_graph_supported(&o1)) {
        cflow_plan_compile_stats ps = {0};
        if (!cflow_plan_compile(&plan, &o1, &ps)) { fail(report, plan.error ? plan.error : "compiled plan failed"); goto done; }
        if (!cflow_plan_eval_array(&plan, inputs, input_count, &rp)) { fail(report, "compiled plan execution failed"); goto done; }
        if (!cflow_result_equal(&ro, &rp)) { fail(report, "optimized/compiled-plan trace mismatch"); goto done; }
        report->compiled_plan_checked = true;
        report->compiled_instructions = ps.instructions;
    }

    report->output_count = ro.count;
    report->normalized_subgraphs = n1.subgraph_count;
    report->optimized_subgraphs = o1.subgraph_count;
    report->normalized_nodes = graph_node_count(&n1);
    report->optimized_nodes = graph_node_count(&o1);
    report->error = NULL;
    ok = true;

done:
    cflow_plan_destroy(&plan);
    cflow_result_destroy(&rp);
    cflow_result_destroy(&ro);
    cflow_result_destroy(&rn);
    cflow_result_destroy(&rs);
    cflow_graph_destroy(&o2);
    cflow_graph_destroy(&o1);
    cflow_graph_destroy(&n2);
    cflow_graph_destroy(&n1);
    return ok;
}
