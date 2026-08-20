#include <cflow/opt.h>
#include <cflow/lower.h>
#include <cflow/effect.h>
#include <cflow/property.h>

#include <stdlib.h>
#include <string.h>

typedef struct opt_ctx {
    const cflow_graph *src;
    cflow_graph *dst;
    cflow_subgraph_id *map;
    unsigned char *state;
    unsigned passes;
    cflow_opt_stats *stats;
    const char *error;
} opt_ctx;

static size_t graph_node_count(const cflow_graph *g) {
    size_t n = 0;
    if (!g) return 0;
    for (size_t i = 0; i < g->subgraph_count; ++i) n += g->subgraphs[i].node_count;
    return n;
}

static bool maplike(const cflow_node *n) {
    return n && (n->op == CFLOW_OP_MAP || n->op == CFLOW_OP_TRANSFORM);
}

static bool node_is_pure(const opt_ctx *ctx, const cflow_node *n) {
    return ctx && n && cmeta_effects_are_pure(cflow_node_effects(ctx->src, n));
}

static bool node_is_stable(const opt_ctx *ctx, const cflow_node *n) {
    return ctx && n && cmeta_properties_include(
        cflow_node_properties(ctx->src, n), CMETA_PROP_STABLE);
}

static bool fn_safe_idempotent_endomap(cmeta_callable fn) {
    const cmeta_sig_desc *sig = cmeta_callable_signature(fn);
    return cmeta_callable_contract_valid(fn) && cmeta_effects_are_pure(fn.meta.effects) &&
        cmeta_properties_include(fn.meta.properties,
            CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_IDEMPOTENT) &&
        sig && sig->protocol == CMETA_FN_PROTOCOL_VALUE && sig->param_count == 1u &&
        cmeta_type_equal(sig->params[0], sig->return_type);
}

static bool append_edge(opt_ctx *ctx, cflow_subgraph_id sgid,
                        cflow_node_id prev, cflow_node_id id) {
    if (prev == CMETA_INVALID_ID) return true;
    if (!cflow_graph_connect(ctx->dst, sgid, prev, 0, id, 0)) {
        ctx->error = ctx->dst->error ? ctx->dst->error : "optimizer edge creation failed";
        return false;
    }
    return true;
}

static bool append_plain_node(opt_ctx *ctx, cflow_subgraph_id sgid,
                              const cflow_node *src,
                              const cflow_subgraph_id *nested,
                              size_t nested_count,
                              cflow_node_id *out) {
    cflow_subgraph *sg = &ctx->dst->subgraphs[sgid];
    cflow_node_id prev = sg->tail;
    cflow_op op = src->op;
    if ((ctx->passes & CMETA_OPT_CANONICALIZE) && op == CFLOW_OP_TRANSFORM) {
        op = CFLOW_OP_MAP;
        if (ctx->stats) ++ctx->stats->transforms_canonicalized;
    }
    if (!cflow_graph_create_node(ctx->dst, sgid, op, src->fn, nested, nested_count, out)) {
        ctx->error = ctx->dst->error ? ctx->dst->error : "optimizer node creation failed";
        return false;
    }
    if (!append_edge(ctx, sgid, prev, *out)) return false;

    /* Preserve a pre-existing optimized chain if optimization is run again. */
    if (src->fn_chain_count) {
        cflow_node *dstn = &ctx->dst->subgraphs[sgid].nodes[*out];
        dstn->fn_chain = malloc(src->fn_chain_count * sizeof(*dstn->fn_chain));
        if (!dstn->fn_chain) { ctx->error = "optimizer map-chain allocation failed"; return false; }
        memcpy(dstn->fn_chain, src->fn_chain, src->fn_chain_count * sizeof(*dstn->fn_chain));
        dstn->fn_chain_count = src->fn_chain_count;
        dstn->output_type = src->output_type;
    }
    return cflow_graph_set_subgraph_exit(ctx->dst, sgid, *out);
}

static bool optimize_subgraph(opt_ctx *ctx, cflow_subgraph_id src_id,
                              cflow_subgraph_id *out_id);

static bool optimize_nested(opt_ctx *ctx, const cflow_node *node,
                            cflow_subgraph_id **out) {
    *out = NULL;
    if (!node->subgraph_count) return true;
    cflow_subgraph_id *ids = malloc(node->subgraph_count * sizeof(*ids));
    if (!ids) { ctx->error = "optimizer nested-id allocation failed"; return false; }
    for (size_t i = 0; i < node->subgraph_count; ++i) {
        if (!optimize_subgraph(ctx, node->subgraphs[i], &ids[i])) { free(ids); return false; }
    }
    *out = ids;
    return true;
}

static cflow_relation_schema simplify_relation(opt_ctx *ctx,
                                                const cflow_node *node) {
    cflow_relation_schema s = node->relation;
    /* With one branch and FIRST_RESULT+SELECT, ALL/ANY/LATEST/SEQUENCE have the
     * same externally visible first-value result. Canonicalize to ANY unless
     * TRY_NEXT requires SEQUENCE ownership. */
    bool candidate = (ctx->passes & CMETA_OPT_RELATION_SIMPLIFY) &&
        node->subgraph_count == 1u &&
        s.completion == CFLOW_REL_COMPLETE_FIRST_RESULT &&
        s.result == CFLOW_REL_RESULT_SELECT &&
        s.error != CFLOW_REL_ERROR_TRY_NEXT &&
        s.coordination != CFLOW_REL_COORD_ANY;
    if (candidate) {
        if (!node_is_pure(ctx, node)) {
            if (ctx->stats) ++ctx->stats->effect_blocked_relation_simplifications;
        } else if (!node_is_stable(ctx, node)) {
            if (ctx->stats) ++ctx->stats->property_blocked_relation_simplifications;
        } else {
            s.coordination = CFLOW_REL_COORD_ANY;
            if (ctx->stats) ++ctx->stats->relation_schemas_simplified;
        }
    }
    return s;
}

static bool append_relation(opt_ctx *ctx, cflow_subgraph_id sgid,
                            const cflow_node *src) {
    cflow_subgraph_id *branches = NULL;
    if (!optimize_nested(ctx, src, &branches)) return false;
    cflow_subgraph *sg = &ctx->dst->subgraphs[sgid];
    cflow_node_id prev = sg->tail, id = CMETA_INVALID_ID;
    cmeta_callable fn = src->has_fn ? src->fn : (cmeta_callable){0};
    cflow_relation_schema schema = simplify_relation(ctx, src);
    bool ok = cflow_graph_create_relation_node(ctx->dst, sgid, src->input_type,
                                                branches, src->subgraph_count,
                                                schema, fn, &id);
    free(branches);
    if (!ok || id == CMETA_INVALID_ID) {
        ctx->error = ctx->dst->error ? ctx->dst->error : "optimizer relation creation failed";
        return false;
    }
    if (!append_edge(ctx, sgid, prev, id)) return false;
    return cflow_graph_set_subgraph_exit(ctx->dst, sgid, id);
}

static size_t node_chain_count(const cflow_node *n) {
    return n && n->fn_chain_count ? n->fn_chain_count : 1u;
}

static bool append_map_chain(opt_ctx *ctx, cflow_subgraph_id sgid,
                             const cflow_subgraph *srcsg,
                             cflow_node_id first,
                             cflow_node_id *last_out) {
    size_t total = 0, nodes = 0;
    cflow_node_id at = first;
    for (;;) {
        const cflow_node *n = cflow_subgraph_node(srcsg, at);
        if (!maplike(n) || !node_is_pure(ctx, n)) break;
        total += node_chain_count(n);
        ++nodes;
        if (at == srcsg->tail) break;
        cflow_node_id next = CMETA_INVALID_ID;
        if (!cflow_subgraph_single_successor(srcsg, at, &next)) break;
        const cflow_node *next_node = cflow_subgraph_node(srcsg, next);
        if (!maplike(next_node)) break;
        if (!node_is_pure(ctx, next_node)) {
            if (ctx->stats) ++ctx->stats->effect_blocked_map_fusions;
            break;
        }
        at = next;
    }

    cmeta_callable *chain = malloc(total * sizeof(*chain));
    if (!chain) { ctx->error = "optimizer fused-map allocation failed"; return false; }
    size_t pos = 0;
    cflow_node_id walk = first;
    for (;;) {
        const cflow_node *n = cflow_subgraph_node(srcsg, walk);
        const cmeta_callable *fns = n->fn_chain_count ? n->fn_chain : &n->fn;
        size_t fn_count = n->fn_chain_count ? n->fn_chain_count : 1u;
        for (size_t fi = 0; fi < fn_count; ++fi) {
            cmeta_callable fn = fns[fi];
            bool duplicate = pos > 0u && cmeta_callable_same(chain[pos - 1u], fn);
            if (duplicate && (ctx->passes & CMETA_OPT_PROPERTY_REWRITES)) {
                if (fn_safe_idempotent_endomap(chain[pos - 1u]) &&
                    fn_safe_idempotent_endomap(fn)) {
                    if (ctx->stats) ++ctx->stats->idempotent_maps_eliminated;
                    continue;
                }
                if (ctx->stats) ++ctx->stats->property_blocked_idempotent_eliminations;
            }
            chain[pos++] = fn;
        }
        if (walk == at) break;
        cflow_node_id next = CMETA_INVALID_ID;
        if (!cflow_subgraph_single_successor(srcsg, walk, &next)) { free(chain); return false; }
        walk = next;
    }

    cflow_subgraph *dstsg = &ctx->dst->subgraphs[sgid];
    cflow_node_id prev = dstsg->tail, id = CMETA_INVALID_ID;
    if (!cflow_graph_create_node(ctx->dst, sgid, CFLOW_OP_MAP, chain[0], NULL, 0, &id)) {
        free(chain); ctx->error = ctx->dst->error ? ctx->dst->error : "optimizer fused MAP creation failed"; return false;
    }
    if (!append_edge(ctx, sgid, prev, id)) { free(chain); return false; }
    cflow_node *dstn = &ctx->dst->subgraphs[sgid].nodes[id];
    dstn->fn_chain = chain;
    dstn->fn_chain_count = pos;
    dstn->output_type = cflow_subgraph_node(srcsg, at)->output_type;
    if (!cflow_graph_set_subgraph_exit(ctx->dst, sgid, id)) return false;
    if (ctx->stats && nodes > 1u) ctx->stats->map_nodes_fused += nodes - 1u;
    if (ctx->stats && (ctx->passes & CMETA_OPT_CANONICALIZE)) {
        walk = first;
        for (;;) {
            const cflow_node *n = cflow_subgraph_node(srcsg, walk);
            if (n->op == CFLOW_OP_TRANSFORM) ++ctx->stats->transforms_canonicalized;
            if (walk == at) break;
            cflow_node_id next = CMETA_INVALID_ID;
            if (!cflow_subgraph_single_successor(srcsg, walk, &next)) break;
            walk = next;
        }
    }
    *last_out = at;
    return true;
}

static bool optimize_subgraph(opt_ctx *ctx, cflow_subgraph_id src_id,
                              cflow_subgraph_id *out_id) {
    if (!ctx || !out_id || src_id >= ctx->src->subgraph_count) return false;
    if (ctx->state[src_id] == 2u) { *out_id = ctx->map[src_id]; return true; }
    if (ctx->state[src_id] == 1u) { ctx->error = "recursive subgraph reference is not optimizable"; return false; }
    ctx->state[src_id] = 1u;

    const cflow_subgraph *srcsg = cflow_graph_subgraph(ctx->src, src_id);
    cflow_subgraph_id dstid = cflow_graph_create_subgraph(ctx->dst, srcsg->input_type);
    if (dstid == CMETA_INVALID_ID) { ctx->error = ctx->dst->error; return false; }

    cflow_node_id at = srcsg->entry;
    while (at != srcsg->tail) {
        cflow_node_id next = CMETA_INVALID_ID;
        if (!cflow_subgraph_single_successor(srcsg, at, &next)) {
            ctx->error = "optimizer requires explicit single-path DATA topology";
            return false;
        }
        const cflow_node *node = cflow_subgraph_node(srcsg, next);
        if (!node) { ctx->error = "optimizer encountered invalid node"; return false; }

        if ((ctx->passes & CMETA_OPT_MAP_FUSION) && maplike(node) && node_is_pure(ctx, node)) {
            cflow_node_id last = next;
            if (!append_map_chain(ctx, dstid, srcsg, next, &last)) return false;
            at = last;
            continue;
        }
        if ((ctx->passes & CMETA_OPT_MAP_FUSION) && maplike(node) && !node_is_pure(ctx, node)) {
            cflow_node_id after = CMETA_INVALID_ID;
            if (next != srcsg->tail && cflow_subgraph_single_successor(srcsg, next, &after) &&
                maplike(cflow_subgraph_node(srcsg, after)) && ctx->stats)
                ++ctx->stats->effect_blocked_map_fusions;
        }
        if (node->op == CFLOW_OP_RELATION) {
            if (!append_relation(ctx, dstid, node)) return false;
        } else if (node->op == CFLOW_OP_SOURCE) {
            ctx->error = "SOURCE may only be a subgraph entry";
            return false;
        } else {
            cflow_subgraph_id *nested = NULL;
            if (!optimize_nested(ctx, node, &nested)) return false;
            cflow_node_id id = CMETA_INVALID_ID;
            bool ok = append_plain_node(ctx, dstid, node, nested, node->subgraph_count, &id);
            free(nested);
            if (!ok) return false;
        }
        at = next;
    }

    ctx->map[src_id] = dstid;
    ctx->state[src_id] = 2u;
    *out_id = dstid;
    return true;
}

bool cflow_graph_optimize(cflow_graph *dst,
                          const cflow_graph *src,
                          cflow_opt_options options,
                          cflow_opt_stats *stats) {
    if (!dst || !src || dst == src || dst->subgraphs || dst->subgraph_count != 0u)
        return false;
    if (!cflow_graph_is_normalized(src)) { dst->error = "optimizer requires normalized IR"; return false; }
    const char *validation = NULL;
    if (!cflow_graph_validate(src, &validation)) {
        dst->error = validation ? validation : "optimizer source validation failed";
        return false;
    }
    if (options.passes == 0u) options.passes = CMETA_OPT_DEFAULT;
    if (stats) {
        memset(stats, 0, sizeof(*stats));
        stats->subgraphs_before = src->subgraph_count;
        stats->nodes_before = graph_node_count(src);
    }

    opt_ctx ctx = {0};
    ctx.src = src; ctx.dst = dst; ctx.passes = options.passes; ctx.stats = stats;
    ctx.map = malloc(src->subgraph_count * sizeof(*ctx.map));
    ctx.state = calloc(src->subgraph_count ? src->subgraph_count : 1u, 1u);
    if (!ctx.map || !ctx.state) {
        free(ctx.map); free(ctx.state); dst->error = "optimizer state allocation failed"; return false;
    }
    for (size_t i = 0; i < src->subgraph_count; ++i) ctx.map[i] = CMETA_INVALID_ID;

    cflow_subgraph_id root = CMETA_INVALID_ID;
    bool ok = optimize_subgraph(&ctx, src->root, &root);
    if (ok && !(options.passes & CMETA_OPT_DEAD_SUBGRAPHS)) {
        for (size_t i = 0; i < src->subgraph_count; ++i) {
            cflow_subgraph_id ignored = CMETA_INVALID_ID;
            if (!optimize_subgraph(&ctx, (cflow_subgraph_id)i, &ignored)) { ok = false; break; }
        }
    }
    free(ctx.map); free(ctx.state);
    if (!ok) {
        const char *err = ctx.error ? ctx.error : (dst->error ? dst->error : "Graph optimization failed");
        cflow_graph_destroy(dst); dst->error = err; return false;
    }
    dst->root = root;
    dst->error = NULL;
    ++dst->version;

    if (!cflow_graph_validate(dst, &validation) || !cflow_graph_is_normalized(dst)) {
        const char *err = validation ? validation : "optimized Graph validation failed";
        cflow_graph_destroy(dst); dst->error = err; return false;
    }
    if (stats) {
        stats->subgraphs_after = dst->subgraph_count;
        stats->nodes_after = graph_node_count(dst);
        if ((options.passes & CMETA_OPT_DEAD_SUBGRAPHS) &&
            stats->subgraphs_before > stats->subgraphs_after)
            stats->dead_subgraphs_removed = stats->subgraphs_before - stats->subgraphs_after;
    }
    return true;
}
