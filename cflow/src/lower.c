#include <cflow/lower.h>
#include "dense_successor_index.h"
#include "graph_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct lower_ctx {
    const cflow_graph *src;
    cflow_graph *dst;
    cflow_subgraph_id *map;
    unsigned char *state; /* 0 unseen, 1 visiting, 2 done */
    const char *error;
} lower_ctx;

static bool node_is_high_level(const cflow_node *node) {
    if (!node || node->op == CFLOW_OP_SOURCE || node->op == CFLOW_OP_RELATION) return false;
    const cflow_op_schema *schema = cflow_op_schema_get(node->op);
    return schema && schema->semantic && strcmp(schema->semantic, "high_level") == 0;
}

static bool lower_subgraph(lower_ctx *ctx, cflow_subgraph_id src_id,
                           cflow_subgraph_id *out_id);

static bool lower_nested_ids(lower_ctx *ctx, const cflow_node *node,
                             cflow_subgraph_id **ids_out) {
    *ids_out = NULL;
    if (!node->subgraph_count) return true;
    cflow_subgraph_id *ids = calloc(node->subgraph_count, sizeof(*ids));
    if (!ids) { ctx->error = "normalization nested-id allocation failed"; return false; }
    for (size_t i = 0; i < node->subgraph_count; ++i) {
        if (!lower_subgraph(ctx, node->subgraphs[i], &ids[i])) {
            free(ids); return false;
        }
    }
    *ids_out = ids;
    return true;
}

static bool append_typed_node(lower_ctx *ctx, cflow_subgraph_id sgid,
                              const cflow_node *node) {
    cflow_subgraph_id *nested = NULL;
    if (!lower_nested_ids(ctx, node, &nested)) return false;
    cflow_subgraph *sg = &ctx->dst->subgraphs[sgid];
    cflow_node_id prev = sg->tail, id = CMETA_INVALID_ID;
    bool ok = cflow_graph_create_node(ctx->dst, sgid, node->op, node->fn,
                                     nested, node->subgraph_count, &id);
    free(nested);
    if (!ok || id == CMETA_INVALID_ID) {
        ctx->error = ctx->dst->error ? ctx->dst->error : "normalization node creation failed";
        return false;
    }
    if (prev != CMETA_INVALID_ID && !cflow_graph_connect(ctx->dst, sgid, prev, 0, id, 0)) {
        ctx->error = ctx->dst->error ? ctx->dst->error : "normalization edge creation failed";
        return false;
    }
    /* Callable signatures establish semantic compatibility, while the source
     * Graph descriptor carries the concrete lifecycle traits used at runtime. */
    cflow_node *dstn = &ctx->dst->subgraphs[sgid].nodes[id];
    dstn->input_type = node->input_type;
    dstn->output_type = node->output_type;
    if (node->fn_chain_count) {
        dstn->fn_chain = malloc(node->fn_chain_count * sizeof(*dstn->fn_chain));
        if (!dstn->fn_chain) { ctx->error = "normalization map-chain allocation failed"; return false; }
        memcpy(dstn->fn_chain, node->fn_chain, node->fn_chain_count * sizeof(*dstn->fn_chain));
        dstn->fn_chain_count = node->fn_chain_count;
        dstn->output_type = node->output_type;
    }
    return cflow_graph_set_subgraph_exit(ctx->dst, sgid, id);
}

static bool append_relation_node(lower_ctx *ctx, cflow_subgraph_id sgid,
                                 const cflow_node *node) {
    cflow_subgraph_id *branches = NULL;
    if (!lower_nested_ids(ctx, node, &branches)) return false;
    cflow_subgraph *sg = &ctx->dst->subgraphs[sgid];
    cflow_node_id prev = sg->tail, id = CMETA_INVALID_ID;
    cmeta_callable fn = node->has_fn ? node->fn : (cmeta_callable){0};
    bool ok = cflow_graph_create_relation_node(ctx->dst, sgid, node->input_type,
                                                branches, node->subgraph_count,
                                                node->relation, fn, &id);
    free(branches);
    if (!ok || id == CMETA_INVALID_ID) {
        ctx->error = ctx->dst->error ? ctx->dst->error : "normalization relation creation failed";
        return false;
    }
    if (prev != CMETA_INVALID_ID && !cflow_graph_connect(ctx->dst, sgid, prev, 0, id, 0)) {
        ctx->error = ctx->dst->error ? ctx->dst->error : "normalization relation edge failed";
        return false;
    }
    ctx->dst->subgraphs[sgid].nodes[id].input_type = node->input_type;
    ctx->dst->subgraphs[sgid].nodes[id].output_type = node->output_type;
    return cflow_graph_set_subgraph_exit(ctx->dst, sgid, id);
}

/* ZIP is a surface operator over two computations that share the same root
 * source. Lower it to:
 *
 *   source -> RELATION(ALL + INVOKE, [left-prefix, right-branch])
 *
 * The current partially built subgraph is exactly the normalized left prefix.
 */
static bool lower_zip(lower_ctx *ctx, cflow_subgraph_id *current,
                      const cflow_node *zip) {
    if (!ctx || !current || !zip || zip->subgraph_count != 1u || !zip->has_fn) {
        if (ctx) ctx->error = "malformed ZIP during normalization";
        return false;
    }
    cflow_subgraph_id right = CMETA_INVALID_ID;
    if (!lower_subgraph(ctx, zip->subgraphs[0], &right)) return false;

    const cmeta_type_desc *source_type = cflow_subgraph_source_type(ctx->dst, *current);
    if (!source_type || !cmeta_type_equal(source_type,
                                           cflow_subgraph_source_type(ctx->dst, right))) {
        ctx->error = "ZIP branches do not share one root source type";
        return false;
    }

    cflow_subgraph_id combined = cflow_graph_create_subgraph(ctx->dst, source_type);
    if (combined == CMETA_INVALID_ID) {
        ctx->error = ctx->dst->error ? ctx->dst->error : "ZIP lowering subgraph allocation failed";
        return false;
    }
    cflow_subgraph_id branches[2] = { *current, right };
    cflow_node_id rel = CMETA_INVALID_ID;
    if (!cflow_graph_create_relation_node(ctx->dst, combined, source_type,
                                          branches, 2u,
                                          cflow_relation_all_invoke(), zip->fn,
                                          &rel)) {
        ctx->error = ctx->dst->error ? ctx->dst->error : "ZIP lowering relation creation failed";
        return false;
    }
    cflow_subgraph *sg = &ctx->dst->subgraphs[combined];
    if (!cflow_graph_connect(ctx->dst, combined, sg->entry, 0, rel, 0) ||
        !cflow_graph_set_subgraph_exit(ctx->dst, combined, rel)) {
        ctx->error = ctx->dst->error ? ctx->dst->error : "ZIP lowering relation wiring failed";
        return false;
    }
    *current = combined;
    return true;
}

static bool lower_subgraph(lower_ctx *ctx, cflow_subgraph_id src_id,
                           cflow_subgraph_id *out_id) {
    cflow_dense_successor_index index = {0};
    cflow_dense_successor_index_status index_status;
    bool ok = false;

    if (!ctx || !out_id || src_id >= ctx->src->subgraph_count) return false;
    if (ctx->state[src_id] == 2u) { *out_id = ctx->map[src_id]; return true; }
    if (ctx->state[src_id] == 1u) {
        ctx->error = "recursive Subgraph reference is not normalizable";
        return false;
    }
    ctx->state[src_id] = 1u;

    const cflow_subgraph *src_sg = cflow_graph_subgraph(ctx->src, src_id);
    if (!src_sg || src_sg->entry >= src_sg->node_count || src_sg->tail >= src_sg->node_count) {
        ctx->error = "invalid source Subgraph during normalization";
        return false;
    }
    const cflow_node *entry = cflow_subgraph_node(src_sg, src_sg->entry);
    if (!entry || entry->op != CFLOW_OP_SOURCE) {
        ctx->error = "normalized Subgraph must start at SOURCE";
        return false;
    }

    index_status = cflow_dense_successor_index_build(&index, src_sg);
    if (index_status != CFLOW_DENSE_SUCCESSOR_INDEX_OK) {
        ctx->error = index_status == CFLOW_DENSE_SUCCESSOR_INDEX_ALLOCATION_FAILED
                         ? "normalization successor-index allocation failed"
                         : "normalization encountered invalid DATA topology";
        return false;
    }
    if (index.has_fanout) {
        ctx->error = "normalization requires explicit single-path DATA topology";
        goto done;
    }

    cflow_subgraph_id current = cflow_graph_create_subgraph(ctx->dst, src_sg->input_type);
    if (current == CMETA_INVALID_ID) {
        ctx->error = ctx->dst->error ? ctx->dst->error : "normalization Subgraph allocation failed";
        goto done;
    }

    cflow_node_id at = src_sg->entry;
    while (at != src_sg->tail) {
        cflow_node_id next = CMETA_INVALID_ID;
        if (!cflow_dense_successor_index_successor(&index, at, &next)) {
            ctx->error = "normalization requires explicit single-path DATA topology";
            goto done;
        }
        const cflow_node *node = cflow_subgraph_node(src_sg, next);
        if (!node) { ctx->error = "normalization encountered invalid node"; goto done; }

        if (node->op == CFLOW_OP_ZIP) {
            if (!lower_zip(ctx, &current, node)) goto done;
        } else if (node_is_high_level(node)) {
            ctx->error = "no lowering rule exists for high-level operator";
            goto done;
        } else if (node->op == CFLOW_OP_RELATION) {
            if (!append_relation_node(ctx, current, node)) goto done;
        } else if (node->op == CFLOW_OP_SOURCE) {
            ctx->error = "SOURCE may only be a Subgraph entry";
            goto done;
        } else {
            if (!append_typed_node(ctx, current, node)) goto done;
        }
        at = next;
    }

    ctx->map[src_id] = current;
    ctx->state[src_id] = 2u;
    *out_id = current;
    ok = true;

done:
    cflow_dense_successor_index_destroy(&index);
    return ok;
}

bool cflow_graph_is_normalized(const cflow_graph *g) {
    if (!g || g->root >= g->subgraph_count) return false;
    for (size_t s = 0; s < g->subgraph_count; ++s) {
        const cflow_subgraph *sg = &g->subgraphs[s];
        for (size_t n = 0; n < sg->node_count; ++n)
            if (node_is_high_level(&sg->nodes[n])) return false;
    }
    return true;
}

bool cflow_graph_normalize(cflow_graph *dst, const cflow_graph *src) {
    if (!dst || !src || dst == src || src->root >= src->subgraph_count) return false;
    if (dst->subgraphs || dst->subgraph_count != 0u) return false; /* require empty destination */

    const char *validation = NULL;
    if (!cflow_graph_validate(src, &validation)) {
        dst->error = validation ? validation : "source Graph validation failed";
        return false;
    }

    lower_ctx ctx = {0};
    ctx.src = src; ctx.dst = dst;
    ctx.map = malloc(src->subgraph_count * sizeof(*ctx.map));
    ctx.state = calloc(src->subgraph_count ? src->subgraph_count : 1u, 1u);
    if (!ctx.map || !ctx.state) {
        free(ctx.map); free(ctx.state);
        dst->error = "normalization state allocation failed";
        return false;
    }
    for (size_t i = 0; i < src->subgraph_count; ++i) ctx.map[i] = CMETA_INVALID_ID;

    cflow_subgraph_id root = CMETA_INVALID_ID;
    bool ok = lower_subgraph(&ctx, src->root, &root);
    /* Preserve every source Subgraph in normalized form, even if it is not
     * reachable from root, so stable static IR inspection remains complete. */
    for (size_t i = 0; ok && i < src->subgraph_count; ++i) {
        cflow_subgraph_id ignored = CMETA_INVALID_ID;
        ok = lower_subgraph(&ctx, (cflow_subgraph_id)i, &ignored);
    }
    free(ctx.map); free(ctx.state);

    if (!ok) {
        const char *err = ctx.error ? ctx.error : (dst->error ? dst->error : "Graph normalization failed");
        cflow_graph_destroy(dst);
        dst->error = err;
        return false;
    }
    dst->root = root;
    dst->error = NULL;
    if (!cflow_graph_version_acquire(&dst->version)) {
        cflow_graph_destroy(dst);
        dst->error = "graph version space exhausted";
        return false;
    }

    if (!cflow_graph_is_normalized(dst) || !cflow_graph_validate(dst, &validation)) {
        const char *err = validation ? validation : "normalized Graph validation failed";
        cflow_graph_destroy(dst);
        dst->error = err;
        return false;
    }
    return true;
}
