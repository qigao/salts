#include <cflow/operators.h>
#include <cflow/runtime.h>
#include "scheduler_internal.h"
#include <cflow/lower.h>
#include <cflow/subrun.h>
#include <cflow/coord.h>
#include <cflow/relation_exec.h>
#include <turbo/thread.h>

#include "value_storage.h"
#include "runtime_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef CMETA_RUN_QUANTUM
#define CMETA_RUN_QUANTUM 64u
#endif

#ifndef CMETA_RUN_MAX_CONTINUATIONS
#define CMETA_RUN_MAX_CONTINUATIONS 64u
#endif

typedef struct generator_state {
    cmeta_callable fn;
    cflow_value_slot input;
    size_t cursor;
} generator_state;

typedef struct continuation_frame {
    cflow_node_id node_id;
    cflow_resumable machine;
    cflow_value_slot root;
    cflow_value_slot output;
    bool done;
} continuation_frame;

typedef struct run_impl {
    cflow_run *owner;
    const cflow_graph *graph;
    cflow_subgraph_id subgraph_id;
    const cflow_subgraph *subgraph;
    cflow_source source;
    cflow_scheduler *scheduler;
    cflow_sink sink;
    cflow_resume_ctx resume_ctx;

    cflow_value_slot source_slot;
    cflow_value_slot *reduce_value;
    bool *reduce_flushed;

    continuation_frame continuations[CMETA_RUN_MAX_CONTINUATIONS];
    size_t continuation_count;

    turbo_mutex_t lock;
    turbo_cond_t task_cv;
    size_t task_refs;
    size_t demand;
    bool task_scheduled;
    /* A posting ref keeps the Run alive when a foreign Scheduler invokes the
     * pump inline; the task ref settles only through run or cancellation. */
    size_t task_posting;
    cflow_task_id scheduled_task_id;
    uint64_t scheduled_task_generation;
    uint64_t next_task_generation;
    bool scheduler_settles_cancel;
    bool rejection_must_fail;
    bool pump_running;
    bool waiting;
    cflow_waitable active_wait;
    bool source_done;
    bool cancel_requested;
    bool cancelled;
    bool terminated;
    bool close_requested;
    bool external_closer;
    bool destroying;
    bool closed;
    const char *error;
} run_impl;

static TURBO_THREAD_LOCAL run_impl *active_pump_run;
static TURBO_THREAD_LOCAL const cflow_run *active_destroy_owner;
static turbo_once_t run_lifecycle_once = TURBO_ONCE_INIT;
static turbo_mutex_t run_lifecycle_lock;
static turbo_cond_t run_lifecycle_cv;
static bool run_lifecycle_ready;

static void run_lifecycle_init(void) {
    turbo_mutex_init(&run_lifecycle_lock);
    if (!run_lifecycle_lock) return;
    turbo_cond_init(&run_lifecycle_cv);
    if (!run_lifecycle_cv) {
        turbo_mutex_destroy(&run_lifecycle_lock);
        return;
    }
    run_lifecycle_ready = true;
}

static bool run_lifecycle_ensure(void) {
    turbo_once(&run_lifecycle_once, run_lifecycle_init);
    return run_lifecycle_ready;
}

static run_impl *impl_of(const cflow_run *run) {
    return run ? (run_impl *)run->impl : NULL;
}

bool cflow_run_active_on_current_thread(const cflow_run *run) {
    run_impl *impl = impl_of(run);
    return (impl != NULL && active_pump_run == impl) ||
           (run != NULL && active_destroy_owner == run);
}

static cflow_step generator_resume_machine(void *state, cflow_resume_ctx *ctx, void *out) {
    (void)ctx;
    generator_state *g = (generator_state *)state;
    if (!g) return (cflow_step){ CFLOW_STEP_ERROR, {0}, "generator state is null" };
    cmeta_gen_status st = cmeta_callable_generate(
        &g->fn, g->input.storage, out, &g->cursor);
    switch (st) {
        case CMETA_GEN_VALUE: return (cflow_step){ CFLOW_STEP_VALUE, {0}, NULL };
        case CMETA_GEN_VALUE_AND_DONE: return (cflow_step){ CFLOW_STEP_VALUE_AND_DONE, {0}, NULL };
        case CMETA_GEN_DONE: return (cflow_step){ CFLOW_STEP_DONE, {0}, NULL };
        case CMETA_GEN_ERROR: return (cflow_step){ CFLOW_STEP_ERROR, {0}, "generator returned error" };
    }
    return (cflow_step){ CFLOW_STEP_ERROR, {0}, "invalid generator status" };
}
static void generator_destroy_machine(void *state) {
    generator_state *g = (generator_state *)state;
    if (g) { cflow_value_slot_destroy(&g->input); free(g); }
}
static const cflow_resumable_ops generator_machine_ops = {
    generator_resume_machine, NULL, generator_destroy_machine
};


static void continuation_clear(continuation_frame *f) {
    if (!f) return;
    if (f->machine.ops && f->machine.ops->destroy) f->machine.ops->destroy(f->machine.state);
    cflow_value_slot_destroy(&f->root);
    cflow_value_slot_destroy(&f->output);
    memset(f, 0, sizeof(*f));
}

static void continuations_clear(run_impl *r) {
    while (r && r->continuation_count) continuation_clear(&r->continuations[--r->continuation_count]);
}

/* Takes ownership of machine on success. root_source is copied so nested
 * child/stateful work can outlive the current process_path() stack frame. */
static bool push_continuation(run_impl *r,
                              cflow_node_id node_id,
                              cflow_resumable machine,
                              const void *root_source) {
    if (!r || !machine.ops || !machine.ops->resume || !machine.output_type ||
        r->continuation_count >= CMETA_RUN_MAX_CONTINUATIONS) return false;
    continuation_frame *f = &r->continuations[r->continuation_count++];
    memset(f, 0, sizeof(*f));
    f->node_id = node_id;
    f->machine = machine;
    if (!cflow_value_slot_init(&f->output, machine.output_type)) {
        continuation_clear(f);
        --r->continuation_count;
        return false;
    }
    if (root_source) {
        const cmeta_type_desc *src_type = cflow_subgraph_source_type(r->graph, r->subgraph_id);
        if (!cflow_value_slot_init(&f->root, src_type) ||
            !cflow_value_slot_copy(&f->root, root_source)) {
            continuation_clear(f);
            --r->continuation_count;
            return false;
        }
    }
    return true;
}

static void reducers_clear(run_impl *r) {
    if (!r) return;
    if (r->reduce_value) {
        for (size_t i = 0; i < r->subgraph->node_count; ++i)
            cflow_value_slot_destroy(&r->reduce_value[i]);
    }
    free(r->reduce_value);
    free(r->reduce_flushed);
    r->reduce_value = NULL;
    r->reduce_flushed = NULL;
}


static size_t demand_get(run_impl *r) {
    size_t n = 0;
    turbo_mutex_lock(&r->lock);
    n = r->demand;
    turbo_mutex_unlock(&r->lock);
    return n;
}

static cflow_resume_ctx *run_resume_context(run_impl *r) {
    r->resume_ctx.downstream_demand = demand_get(r);
    return &r->resume_ctx;
}

static bool demand_consume_one(run_impl *r) {
    bool ok = false;
    turbo_mutex_lock(&r->lock);
    if (r->demand) { --r->demand; ok = true; }
    turbo_mutex_unlock(&r->lock);
    return ok;
}

static void run_fail(run_impl *r, const char *message) {
    if (!r) return;
    const char *msg = message ? message : "runtime error";
    bool notify = false;
    turbo_mutex_lock(&r->lock);
    if (!r->terminated) {
        r->error = msg;
        r->terminated = true;
        notify = true;
    }
    turbo_mutex_unlock(&r->lock);
    if (notify && cflow_sink_valid(&r->sink)) cflow_sink_error(&r->sink, msg);
}

static bool reducers_done(const run_impl *r) {
    if (!r || !r->subgraph) return true;
    for (size_t i = 0; i < r->subgraph->node_count; ++i) {
        const cflow_node *node = cflow_subgraph_node(r->subgraph, (cflow_node_id)i);
        const cflow_op_schema *schema = node ? cflow_op_schema_get(node->op) : NULL;
        if (schema && schema->cardinality == CMETA_CARD_REDUCE &&
            r->reduce_value[i].live && !r->reduce_flushed[i]) return false;
    }
    return true;
}


typedef enum node_action {
    NODE_CONTINUE,
    NODE_STOP,
    NODE_FAIL
} node_action;

typedef node_action (*node_handler)(run_impl *r,
                                    cflow_node_id node_id,
                                    const cflow_node *n,
                                    cflow_value_slot *owned,
                                    const cmeta_type_desc **cur_type,
                                    const void *root_source);

static node_action semantic_filter(run_impl *r, cflow_node_id i, const cflow_node *n,
                                   cflow_value_slot *owned, const cmeta_type_desc **cur_type,
                                   const void *root_source) {
    (void)r; (void)i; (void)cur_type; (void)root_source;
    _Bool keep = false; const void *args[1] = { owned->storage };
    if (!cmeta_callable_invoke(&n->fn, &keep, args)) return NODE_FAIL;
    if (!keep) { cflow_value_slot_reset(owned); return NODE_STOP; }
    return NODE_CONTINUE;
}

static node_action semantic_map(run_impl *r, cflow_node_id i, const cflow_node *n,
                                cflow_value_slot *owned, const cmeta_type_desc **cur_type,
                                const void *root_source) {
    (void)r; (void)i; (void)root_source;
    const cmeta_callable *chain = n->fn_chain_count ? n->fn_chain : &n->fn;
    size_t count = n->fn_chain_count ? n->fn_chain_count : 1u;
    const cmeta_type_desc *type = *cur_type;
    for (size_t k = 0; k < count; ++k) {
        const cmeta_sig_desc *sig = cmeta_callable_signature(chain[k]);
        if (!sig || sig->param_count != 1u || !cmeta_type_equal(sig->params[0], type))
            return NODE_FAIL;
        const cmeta_type_desc *next_type =
            k + 1u == count ? n->output_type : sig->return_type;
        cflow_value_slot next = {0};
        const void *args[1] = { owned->storage };
        if (!cflow_value_slot_init(&next, next_type)) return NODE_FAIL;
        if (!cmeta_callable_invoke(&chain[k], next.storage, args)) {
            cflow_value_slot_destroy(&next);
            return NODE_FAIL;
        }
        next.live = true;
        cflow_value_slot_destroy(owned);
        *owned = next;
        type = next_type;
    }
    *cur_type = type;
    return cmeta_type_equal(type, n->output_type) ? NODE_CONTINUE : NODE_FAIL;
}

static node_action semantic_high_level(run_impl *r, cflow_node_id i, const cflow_node *n,
                                       cflow_value_slot *owned, const cmeta_type_desc **cur_type,
                                       const void *root_source) {
    (void)r; (void)i; (void)n; (void)owned; (void)cur_type; (void)root_source;
    return NODE_FAIL;
}

static node_action semantic_relation(run_impl *r, cflow_node_id i, const cflow_node *n,
                                     cflow_value_slot *owned, const cmeta_type_desc **cur_type,
                                     const void *root_source) {
    (void)cur_type;
    if (!r || !n || !owned || !owned->live || r->continuation_count >= CMETA_RUN_MAX_CONTINUATIONS)
        return NODE_FAIL;
    cflow_resumable machine = {0};
    if (!cflow_resumable_from_relation(&machine, r->graph, n,
                                        owned->storage)) return NODE_FAIL;
    cflow_value_slot_reset(owned);
    if (!push_continuation(r, i, machine, root_source)) {
        if (machine.ops && machine.ops->destroy) machine.ops->destroy(machine.state);
        return NODE_FAIL;
    }
    return NODE_STOP;
}

static node_action semantic_reduce(run_impl *r, cflow_node_id i, const cflow_node *n,
                                   cflow_value_slot *owned, const cmeta_type_desc **cur_type,
                                   const void *root_source) {
    (void)root_source;
    if (!r->reduce_value[i].live) {
        if (!cflow_value_slot_move(&r->reduce_value[i], owned))
            return NODE_FAIL;
    } else {
        cflow_value_slot next = {0};
        const void *args[2] = {
            r->reduce_value[i].storage, owned->storage };
        if (!cflow_value_slot_init(&next, n->output_type))
            return NODE_FAIL;
        if (!cmeta_callable_invoke(&n->fn, next.storage, args)) {
            cflow_value_slot_destroy(&next);
            return NODE_FAIL;
        }
        next.live = true;
        cflow_value_slot_reset(&r->reduce_value[i]);
        cflow_value_slot_reset(owned);
        if (!cflow_value_slot_move(&r->reduce_value[i], &next)) {
            cflow_value_slot_destroy(&next);
            return NODE_FAIL;
        }
        cflow_value_slot_destroy(&next);
    }
    return NODE_STOP;
}

static node_action semantic_flat_map(run_impl *r, cflow_node_id i, const cflow_node *n,
                                     cflow_value_slot *owned, const cmeta_type_desc **cur_type,
                                     const void *root_source) {
    (void)cur_type;
    if (r->continuation_count >= CMETA_RUN_MAX_CONTINUATIONS) return NODE_FAIL;
    generator_state *gs = calloc(1, sizeof(*gs));
    if (!gs) return NODE_FAIL;
    gs->fn = n->fn;
    if (!cflow_value_slot_init(&gs->input, owned->type) ||
        !cflow_value_slot_move(&gs->input, owned)) {
        generator_destroy_machine(gs);
        return NODE_FAIL;
    }
    cflow_resumable machine = { "flatMap-generator", n->output_type,
                                &generator_machine_ops, gs };
    if (!push_continuation(r, i, machine, root_source)) {
        generator_destroy_machine(gs);
        return NODE_FAIL;
    }
    return NODE_STOP;
}

static node_handler handlers[CFLOW_OP_COUNT] = {
    [CFLOW_OP_RELATION] = semantic_relation,
#define CFLOW_OP_ROW(E, method, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, intrinsic_effects) \
    [CFLOW_OP_##E] = semantic_##semantic,
Replay(CFlowOperators, CFLOW_OP_ROW)
#undef CFLOW_OP_ROW
};

static bool successor_of(run_impl *r,
                         cflow_node_id node,
                         cflow_node_id *next,
                         bool *has_next) {
    if (!r || !r->subgraph || !next || !has_next) return false;
    size_t degree = cflow_subgraph_out_degree(r->subgraph, node);
    if (degree == 0) {
        *has_next = false;
        *next = CMETA_INVALID_ID;
        return true;
    }
    if (degree != 1 || !cflow_subgraph_single_successor(r->subgraph, node, next)) {
        run_fail(r, "fan-out requires an explicit RELATION semantic");
        return false;
    }
    *has_next = true;
    return true;
}

static bool process_path(run_impl *r,
                         cflow_node_id node_id,
                         const void *value,
                         const cmeta_type_desc *type,
                         const void *root_source) {
    cflow_value_slot owned = {0};
    if (!cflow_value_slot_init(&owned, type) ||
        !cflow_value_slot_copy(&owned, value)) {
        cflow_value_slot_destroy(&owned);
        run_fail(r, "value construction failed");
        return false;
    }
    const cmeta_type_desc *cur_type = type;

    cflow_node_id current = node_id;
    while (current != CMETA_INVALID_ID) {
        const cflow_node *n = cflow_subgraph_node(r->subgraph, current);
        if (!n || n->op <= CFLOW_OP_SOURCE || n->op >= CFLOW_OP_COUNT || !handlers[n->op]) {
            cflow_value_slot_destroy(&owned); run_fail(r, "operator has no execution semantic"); return false;
        }
        node_action a = handlers[n->op](r, current, n, &owned, &cur_type, root_source);
        if (a == NODE_FAIL) {
            cflow_value_slot_destroy(&owned); run_fail(r, "operator semantic failed"); return false;
        }
        if (a == NODE_STOP) {
            cflow_value_slot_destroy(&owned);
            return true;
        }

        cflow_node_id next = CMETA_INVALID_ID;
        bool has_next = false;
        if (!successor_of(r, current, &next, &has_next)) { cflow_value_slot_destroy(&owned); return false; }
        current = has_next ? next : CMETA_INVALID_ID;
    }

    if (!demand_consume_one(r)) {
        cflow_value_slot_destroy(&owned); run_fail(r, "runtime produced value without demand"); return false;
    }
    if (cflow_sink_valid(&r->sink) && !cflow_sink_value(&r->sink, cur_type, owned.storage)) {
        cflow_value_slot_destroy(&owned); run_fail(r, "observer rejected value"); return false;
    }
    cflow_value_slot_destroy(&owned); return true;
}


static void wake_cb(void *user);

static bool arm_waitable(run_impl *r, cflow_waitable waitable) {
    if (!r || !cflow_waitable_valid(&waitable)) {
        run_fail(r, "WAIT step has no armable waitable");
        return false;
    }
    turbo_mutex_lock(&r->lock);
    r->waiting = true;
    r->active_wait = waitable;
    turbo_mutex_unlock(&r->lock);
    cflow_waker w = { wake_cb, r->owner };
    if (!cflow_waitable_arm(&waitable, w)) {
        turbo_mutex_lock(&r->lock);
        r->waiting = false;
        memset(&r->active_wait, 0, sizeof(r->active_wait));
        turbo_mutex_unlock(&r->lock);
        run_fail(r, "waitable arm failed");
        return false;
    }
    return true;
}

static bool resume_top_continuation(run_impl *r) {
    if (!r->continuation_count) return true;
    continuation_frame *f = &r->continuations[r->continuation_count - 1];
    const cflow_node *n = cflow_subgraph_node(r->subgraph, f->node_id);
    if (!n) { run_fail(r, "continuation references invalid node"); return false; }
    if (f->done) {
        continuation_clear(f); --r->continuation_count; return true;
    }
    if (f->output.live) {
        run_fail(r, "continuation output slot is not empty");
        return false;
    }
    cflow_step step = f->machine.ops->resume(
        f->machine.state, run_resume_context(r), f->output.storage);
    if (step.kind == CFLOW_STEP_ERROR) { run_fail(r, step.error ? step.error : "operator resumable error"); return false; }
    if (step.kind == CFLOW_STEP_WAIT) return arm_waitable(r, step.waitable);
    if (step.kind == CFLOW_STEP_DONE) { f->done = true; return true; }
    if (step.kind != CFLOW_STEP_VALUE && step.kind != CFLOW_STEP_VALUE_AND_DONE) {
        run_fail(r, "invalid operator resumable step"); return false;
    }
    f->output.live = true;
    if (step.kind == CFLOW_STEP_VALUE_AND_DONE) f->done = true;
    cflow_node_id next = CMETA_INVALID_ID;
    bool has_next = false;
    if (!successor_of(r, f->node_id, &next, &has_next)) return false;
    if (!process_path(r, has_next ? next : CMETA_INVALID_ID,
                      f->output.storage, n->output_type,
                      f->root.live ? f->root.storage : NULL)) return false;
    cflow_value_slot_reset(&f->output);
    return true;
}


static bool flush_one_reducer(run_impl *r) {
    for (size_t i = 0; i < r->subgraph->node_count; ++i) {
        const cflow_node *node = cflow_subgraph_node(r->subgraph, (cflow_node_id)i);
        const cflow_op_schema *schema = node ? cflow_op_schema_get(node->op) : NULL;
        if (!schema || schema->cardinality != CMETA_CARD_REDUCE ||
            !r->reduce_value[i].live || r->reduce_flushed[i]) continue;
        r->reduce_flushed[i] = true;
        cflow_node_id next = CMETA_INVALID_ID;
        bool has_next = false;
        if (!successor_of(r, (cflow_node_id)i, &next, &has_next)) return false;
        if (!process_path(r, has_next ? next : CMETA_INVALID_ID,
                          r->reduce_value[i].storage,
                          node->output_type, NULL)) return false;
        cflow_value_slot_reset(&r->reduce_value[i]);
        return true;
    }
    return true;
}


static void finish_if_possible(run_impl *r) {
    if (!r || !r->source_done || r->continuation_count || !reducers_done(r)) return;
    bool notify = false;
    turbo_mutex_lock(&r->lock);
    if (!r->terminated) { r->terminated = true; notify = true; }
    turbo_mutex_unlock(&r->lock);
    if (notify && cflow_sink_valid(&r->sink)) cflow_sink_done(&r->sink);
}

static bool schedule_pump(run_impl *r, bool fail_on_rejection);

static void wake_cb(void *user) {
    cflow_run *run = (cflow_run *)user;
    run_impl *r = impl_of(run);
    if (!r) return;
    turbo_mutex_lock(&r->lock);
    r->waiting = false;
    memset(&r->active_wait, 0, sizeof(r->active_wait));
    turbo_mutex_unlock(&r->lock);
    (void)schedule_pump(r, true);
}

void cflow_run_wake(cflow_run *run) { wake_cb(run); }

static bool process_source_value(run_impl *r,
                                 bool has_first,
                                 cflow_node_id first,
                                 const cmeta_type_desc *source_type) {
    r->source_slot.live = true;
    if (!has_first) {
        bool accepted;
        if (!demand_consume_one(r)) {
            cflow_value_slot_reset(&r->source_slot);
            run_fail(r, "runtime produced value without demand");
            return false;
        }
        accepted = !cflow_sink_valid(&r->sink) ||
            cflow_sink_value(&r->sink, source_type,
                             r->source_slot.storage);
        cflow_value_slot_reset(&r->source_slot);
        if (!accepted)
            run_fail(r, "observer rejected value");
        return accepted;
    }
    bool processed = process_path(
        r, first, r->source_slot.storage,
        source_type, r->source_slot.storage);
    cflow_value_slot_reset(&r->source_slot);
    return processed;
}

static bool process_source_step(run_impl *r) {
    const cmeta_type_desc *source_type =
        cflow_source_output_type(&r->source);
    cflow_step step;
    cflow_node_id first = CMETA_INVALID_ID;
    bool has_first = false;

    if (r->source_slot.live) {
        run_fail(r, "source output slot is not empty");
        return false;
    }
    step = cflow_source_resume(&r->source, run_resume_context(r),
                               r->source_slot.storage);
    if (!successor_of(r, r->subgraph->entry, &first, &has_first)) return false;
    switch (step.kind) {
        case CFLOW_STEP_VALUE:
            return process_source_value(r, has_first, first, source_type);
        case CFLOW_STEP_VALUE_AND_DONE:
            r->source_done = true;
            return process_source_value(r, has_first, first, source_type);
        case CFLOW_STEP_WAIT:
            return arm_waitable(r, step.waitable);
        case CFLOW_STEP_DONE:
            r->source_done = true;
            return true;
        case CFLOW_STEP_ERROR:
            run_fail(r, step.error ? step.error : "source error");
            return false;
    }
    run_fail(r, "invalid source step"); return false;
}


static bool poll_source_terminal(run_impl *r) {
    if (!r || !cflow_source_valid(&r->source)) return false;
    const char *err = NULL;
    cflow_source_terminal st = cflow_source_poll_terminal(&r->source, &err);
    if (st == CFLOW_SOURCE_ERROR) { run_fail(r, err ? err : "source terminal error"); return true; }
    if (st == CFLOW_SOURCE_DONE) { r->source_done = true; return true; }
    return false;
}

static bool take_cancel_request(run_impl *r) {
    bool cancel = false;
    turbo_mutex_lock(&r->lock);
    if (r->cancel_requested && !r->terminated) {
        r->cancel_requested = false;
        r->cancelled = true;
        r->terminated = true;
        cancel = true;
    }
    turbo_mutex_unlock(&r->lock);
    return cancel;
}

static bool process_unit(run_impl *r) {
    if (r->terminated) return false;
    if (take_cancel_request(r)) {
        cflow_source_cancel(&r->source);
        continuations_clear(r);
        return false;
    }
    if (r->continuation_count) {
        continuation_frame *top = &r->continuations[r->continuation_count - 1];
        if (top->done) return resume_top_continuation(r); /* pop is terminal bookkeeping, not a value */
        if (demand_get(r) == 0) return false;
        return resume_top_continuation(r);
    }
    if (r->source_done) {
        if (!reducers_done(r)) {
            if (demand_get(r) == 0) return false;
            return flush_one_reducer(r);
        }
        finish_if_possible(r);
        return false;
    }
    if (demand_get(r) == 0) {
        if (poll_source_terminal(r)) return true;
        return false;
    }
    return process_source_step(r);
}

static void run_destroy_claimed(run_impl *r) {
    cflow_run *owner;
    const cflow_run *previous_destroy_owner;
    if (!r) return;
    owner = r->owner;
    previous_destroy_owner = active_destroy_owner;
    active_destroy_owner = owner;
    if (cflow_source_valid(&r->source)) cflow_source_destroy(&r->source);
    continuations_clear(r);
    reducers_clear(r);
    cflow_value_slot_destroy(&r->source_slot);
    turbo_mutex_lock(&r->lock);
    r->closed = true;
    turbo_mutex_unlock(&r->lock);
    turbo_cond_destroy(&r->task_cv);
    turbo_mutex_destroy(&r->lock);
    active_destroy_owner = previous_destroy_owner;

    turbo_mutex_lock(&run_lifecycle_lock);
    if (owner && owner->impl == r) owner->impl = NULL;
    free(r);
    turbo_cond_broadcast(&run_lifecycle_cv);
    turbo_mutex_unlock(&run_lifecycle_lock);
}

static bool run_release_task_ref(run_impl *r) {
    bool destroy = false;
    if (!r) return false;
    turbo_mutex_lock(&run_lifecycle_lock);
    turbo_mutex_lock(&r->lock);
    if (r->task_refs != 0u) --r->task_refs;
    if (r->task_refs == 0u) {
        if (r->close_requested && r->owner && r->owner->impl == r &&
            !r->external_closer && !r->destroying) {
            r->destroying = true;
            destroy = true;
        }
        turbo_cond_broadcast(&r->task_cv);
    }
    turbo_mutex_unlock(&r->lock);
    turbo_mutex_unlock(&run_lifecycle_lock);
    return destroy;
}

static void run_clear_scheduled_task_locked(run_impl *r) {
    r->task_scheduled = false;
    r->scheduled_task_id = 0u;
    r->scheduler_settles_cancel = false;
}

static void pump_task(void *user) {
    run_impl *r = (run_impl *)user;
    run_impl *previous_active_run;
    bool destroy;

    if (!r) return;
    previous_active_run = active_pump_run;
    active_pump_run = r;
    turbo_mutex_lock(&r->lock);
    run_clear_scheduled_task_locked(r);
    r->rejection_must_fail = false;
    r->pump_running = true;
    turbo_mutex_unlock(&r->lock);

    for (unsigned i = 0; i < CMETA_RUN_QUANTUM; ++i) {
        bool waiting = false;
        turbo_mutex_lock(&r->lock);
        waiting = r->waiting;
        turbo_mutex_unlock(&r->lock);
        if (waiting || r->terminated) break;
        if (demand_get(r) == 0 && r->continuation_count &&
            !r->continuations[r->continuation_count - 1].done) break;
        if (r->source_done && !reducers_done(r) && demand_get(r) == 0) break;
        if (!process_unit(r)) break;
    }

    bool terminated = false, waiting = false;
    turbo_mutex_lock(&r->lock);
    r->pump_running = false;
    terminated = r->terminated;
    waiting = r->waiting;
    turbo_mutex_unlock(&r->lock);
    size_t d = demand_get(r);
    bool continuation_runnable = r->continuation_count &&
                          (r->continuations[r->continuation_count - 1].done || d > 0);
    bool runnable = continuation_runnable ||
                    (!r->source_done && d > 0) ||
                    (r->source_done && r->continuation_count == 0 && reducers_done(r)) ||
                    (r->source_done && !reducers_done(r) && d > 0);
    if (!terminated && !waiting && runnable) (void)schedule_pump(r, true);

    active_pump_run = previous_active_run;
    destroy = run_release_task_ref(r);
    if (destroy) run_destroy_claimed(r);
}

static void pump_task_cancel(void *user) {
    run_impl *r = (run_impl *)user;
    run_impl *previous_active_run;
    bool cancel_source = false;
    bool destroy;

    if (!r) return;
    previous_active_run = active_pump_run;
    active_pump_run = r;
    turbo_mutex_lock(&r->lock);
    run_clear_scheduled_task_locked(r);
    r->rejection_must_fail = false;
    if (!r->terminated) {
        r->cancel_requested = false;
        r->cancelled = true;
        r->terminated = true;
        cancel_source = true;
    }
    turbo_mutex_unlock(&r->lock);

    if (cancel_source) {
        cflow_source_cancel(&r->source);
        continuations_clear(r);
    }
    active_pump_run = previous_active_run;
    destroy = run_release_task_ref(r);
    if (destroy) run_destroy_claimed(r);
}

static const char *scheduler_rejection_error(cflow_admission_status status) {
    switch (status) {
        case CFLOW_ADMISSION_FULL:
            return "scheduler is full";
        case CFLOW_ADMISSION_CLOSED:
            return "scheduler is closed";
        case CFLOW_ADMISSION_ALLOCATION_FAILED:
            return "scheduler could not allocate Run pump";
        case CFLOW_ADMISSION_INVALID_ARGUMENT:
        case CFLOW_ADMISSION_ACCEPTED:
        default:
            return "scheduler rejected Run pump";
    }
}

static bool schedule_pump(run_impl *r, bool fail_on_rejection) {
    cflow_schedule_result result;
    const cflow_executor_task task = {
        .run = pump_task,
        .cancel = pump_task_cancel,
        .finalize = NULL,
        .user = r
    };
    bool destroy;
    bool scheduler_settles_cancel;
    bool notify = false;
    bool must_fail = false;
    uint64_t generation;
    const char *error = NULL;
    run_impl *previous_active_run;
    if (!r || !r->scheduler) return false;
    turbo_mutex_lock(&r->lock);
    if (r->closed || r->terminated || r->pump_running || r->waiting) {
        turbo_mutex_unlock(&r->lock);
        return true;
    }
    if (r->task_scheduled) {
        r->rejection_must_fail =
            r->rejection_must_fail || fail_on_rejection;
        turbo_mutex_unlock(&r->lock);
        return true;
    }
    if (r->task_refs > SIZE_MAX - 2u || r->task_posting == SIZE_MAX ||
        r->next_task_generation == UINT64_MAX) {
        turbo_mutex_unlock(&r->lock);
        return false;
    }
    r->task_scheduled = true;
    r->scheduled_task_id = 0u;
    r->scheduler_settles_cancel = false;
    generation = ++r->next_task_generation;
    r->scheduled_task_generation = generation;
    r->rejection_must_fail = fail_on_rejection;
    ++r->task_posting;
    r->task_refs += 2u;
    turbo_mutex_unlock(&r->lock);
    scheduler_settles_cancel =
        cflow_scheduler_try_post_task_after_internal(
            r->scheduler, 0u, &task, &result);
    if (!scheduler_settles_cancel) {
        result = cflow_scheduler_try_post_after(
            r->scheduler, 0u, pump_task, r);
    }
    turbo_mutex_lock(&r->lock);
    --r->task_posting;
    if (result.status == CFLOW_ADMISSION_ACCEPTED && result.task_id != 0u) {
        if (r->task_scheduled &&
            r->scheduled_task_generation == generation) {
            r->scheduled_task_id = result.task_id;
            r->scheduler_settles_cancel = scheduler_settles_cancel;
        }
    } else if (r->task_scheduled &&
               r->scheduled_task_generation == generation) {
        run_clear_scheduled_task_locked(r);
        must_fail = r->rejection_must_fail;
        r->rejection_must_fail = false;
        if (must_fail && !r->cancel_requested &&
            !r->close_requested && !r->terminated) {
            error = scheduler_rejection_error(result.status);
            r->error = error;
            r->terminated = true;
            notify = true;
        }
    }
    turbo_cond_broadcast(&r->task_cv);
    turbo_mutex_unlock(&r->lock);
    if (result.status != CFLOW_ADMISSION_ACCEPTED || result.task_id == 0u) {
        /* The rejected task and posting refs protect r through synchronous
         * Sink delivery; the TLS marker preserves callback-close deferral. */
        previous_active_run = active_pump_run;
        active_pump_run = r;
        if (notify && cflow_sink_valid(&r->sink))
            cflow_sink_error(&r->sink, error);

        active_pump_run = previous_active_run;
        destroy = run_release_task_ref(r);
        if (!destroy) destroy = run_release_task_ref(r);
        if (destroy) run_destroy_claimed(r);
        return false;
    }
    destroy = run_release_task_ref(r);
    if (destroy) run_destroy_claimed(r);
    return true;
}


bool cflow_run_open_subgraph(cflow_run *run,
                             const cflow_graph *graph,
                             cflow_subgraph_id subgraph_id,
                             cflow_source *source,
                             cflow_scheduler *scheduler,
                             const cflow_sink *sink) {
    const cflow_subgraph *subgraph;
    const cmeta_type_desc *source_type;
    const char *validation_error = NULL;

    if (!run || run->impl || !graph || !scheduler || !source ||
        !cflow_source_valid(source) || !cflow_graph_is_normalized(graph))
        return false;
    subgraph = cflow_graph_subgraph(graph, subgraph_id);
    source_type = cflow_source_output_type(source);
    if (!subgraph || !source_type)
        return false;
    if (!cflow_graph_validate(graph, &validation_error)) return false;
    if (!cflow_value_type_supported(source_type) ||
        !cflow_value_runtime_graph_supported(graph) ||
        (!cflow_value_storage_type_supported(source_type) &&
         !cflow_source_has(source, CFLOW_SOURCE_CAP_CONSTRUCTS_VALUES)))
        return false;
    if (!cmeta_type_equal(cflow_subgraph_source_type(graph, subgraph_id),
                          source_type))
        return false;
    if (!run_lifecycle_ensure()) return false;

    run_impl *r = calloc(1, sizeof(*r));
    if (!r) return false;
    turbo_mutex_init(&r->lock);
    if (!r->lock) { free(r); return false; }
    turbo_cond_init(&r->task_cv);
    if (!r->task_cv) { turbo_mutex_destroy(&r->lock); free(r); return false; }
    r->owner = run;
    r->graph = graph;
    r->subgraph_id = subgraph_id;
    r->subgraph = subgraph;
    r->source = *source;
    r->scheduler = scheduler;
    r->resume_ctx.scheduler = scheduler;
    if (sink) r->sink = *sink;
    if (!cflow_value_slot_init(&r->source_slot, source_type)) {
        turbo_cond_destroy(&r->task_cv);
        turbo_mutex_destroy(&r->lock);
        free(r);
        return false;
    }
    r->reduce_value = calloc(subgraph->node_count ? subgraph->node_count : 1, sizeof(*r->reduce_value));
    r->reduce_flushed = calloc(subgraph->node_count ? subgraph->node_count : 1, sizeof(*r->reduce_flushed));
    if (!r->reduce_value || !r->reduce_flushed) {
        cflow_value_slot_destroy(&r->source_slot); reducers_clear(r);
        turbo_cond_destroy(&r->task_cv);
        turbo_mutex_destroy(&r->lock);
        free(r);
        return false;
    }
    for (size_t i = 0; i < subgraph->node_count; ++i) {
        const cflow_node *node = cflow_subgraph_node(
            subgraph, (cflow_node_id)i);
        const cflow_op_schema *schema = node
            ? cflow_op_schema_get(node->op) : NULL;
        if (schema && schema->cardinality == CMETA_CARD_REDUCE &&
            !cflow_value_slot_init(&r->reduce_value[i],
                                   node->output_type)) {
            cflow_value_slot_destroy(&r->source_slot);
            reducers_clear(r);
            turbo_cond_destroy(&r->task_cv);
            turbo_mutex_destroy(&r->lock);
            free(r);
            return false;
        }
    }
    run->impl = r;
    memset(source, 0, sizeof(*source));
    {
        cflow_waker w = { wake_cb, run };
        cflow_source_bind_terminal_waker(&r->source, w);
    }
    return true;
}

bool cflow_run_open(cflow_run *run,
                    const cflow_graph *graph,
                    cflow_source *source,
                    cflow_scheduler *scheduler,
                    const cflow_sink *sink) {
    if (!graph || graph->root >= graph->subgraph_count) return false;
    return cflow_run_open_subgraph(run, graph, graph->root, source, scheduler, sink);
}


bool cflow_run_request(cflow_run *run, size_t n) {
    run_impl *r = impl_of(run);
    if (!r || n == 0) return false;
    turbo_mutex_lock(&r->lock);
    if (r->closed || r->terminated) { turbo_mutex_unlock(&r->lock); return false; }
    if (SIZE_MAX - r->demand < n) r->demand = SIZE_MAX;
    else r->demand += n;
    turbo_mutex_unlock(&r->lock);
    return schedule_pump(r, false);
}

void cflow_run_cancel(cflow_run *run) {
    run_impl *r = impl_of(run);
    if (!r) return;
    cflow_waitable wait = {0};
    turbo_mutex_lock(&r->lock);
    r->cancel_requested = true;
    if (r->waiting) { wait = r->active_wait; r->waiting = false; memset(&r->active_wait, 0, sizeof(r->active_wait)); }
    turbo_mutex_unlock(&r->lock);
    if (cflow_waitable_valid(&wait)) cflow_waitable_cancel(&wait);
    (void)schedule_pump(r, false);
}

void cflow_run_close(cflow_run *run) {
    run_impl *r;
    bool initiate_close = false;
    uint64_t cancel_attempted_generation = 0u;

    if (!run || active_destroy_owner == run || !run_lifecycle_ensure()) return;
    turbo_mutex_lock(&run_lifecycle_lock);
    for (;;) {
        r = (run_impl *)run->impl;
        if (!r) {
            turbo_mutex_unlock(&run_lifecycle_lock);
            return;
        }
        if (active_pump_run == r) break;
        if (!r->external_closer && !r->destroying) {
            r->external_closer = true;
            break;
        }
        turbo_cond_wait(&run_lifecycle_cv, &run_lifecycle_lock);
    }
    turbo_mutex_unlock(&run_lifecycle_lock);

    turbo_mutex_lock(&r->lock);
    initiate_close = !r->close_requested;
    r->close_requested = true;
    turbo_mutex_unlock(&r->lock);
    if (initiate_close) {
        cflow_run_cancel(run);
        cflow_source_bind_terminal_waker(&r->source, (cflow_waker){0});
    }

    if (active_pump_run == r) return;

    unsigned caps = cflow_scheduler_capabilities(r->scheduler);
    for (;;) {
        size_t refs = 0;
        cflow_task_id task_id = 0u;
        uint64_t task_generation = 0u;
        bool scheduler_settles_cancel = false;
        turbo_mutex_lock(&r->lock);
        refs = r->task_refs;
        if (!refs) { turbo_mutex_unlock(&r->lock); break; }
        if (r->task_posting != 0u) {
            turbo_cond_wait(&r->task_cv, &r->lock);
            turbo_mutex_unlock(&r->lock);
            continue;
        }
        if (r->task_scheduled && r->scheduled_task_id != 0u &&
            r->scheduled_task_generation != cancel_attempted_generation) {
            task_id = r->scheduled_task_id;
            task_generation = r->scheduled_task_generation;
            scheduler_settles_cancel = r->scheduler_settles_cancel;
        }
        if (task_id == 0u && (caps & CMETA_SCHED_CAP_CONCURRENT)) {
            turbo_cond_wait(&r->task_cv, &r->lock);
            turbo_mutex_unlock(&r->lock);
            continue;
        }
        turbo_mutex_unlock(&r->lock);
        if (task_id != 0u) {
            cancel_attempted_generation = task_generation;
            /* Built-ins own the descriptor cancel callback. A foreign
             * Scheduler only removes fn/user, so Run settles that task ref. */
            if (cflow_scheduler_cancel(r->scheduler, task_id) &&
                !scheduler_settles_cancel)
                pump_task_cancel(r);
            continue;
        }
        (void)cflow_scheduler_run_until_idle(r->scheduler, 0);
    }

    turbo_mutex_lock(&run_lifecycle_lock);
    if (run->impl == r && !r->destroying) r->destroying = true;
    turbo_mutex_unlock(&run_lifecycle_lock);
    run_destroy_claimed(r);
}

bool cflow_run_is_done(const cflow_run *run) {
    run_impl *r = impl_of(run); bool v = false;
    if (r) {
        turbo_mutex_lock(&r->lock);
        v = r->terminated && !r->cancelled && !r->error;
        turbo_mutex_unlock(&r->lock);
    }
    return v;
}
bool cflow_run_is_cancelled(const cflow_run *run) {
    run_impl *r = impl_of(run); bool v = false;
    if (r) { turbo_mutex_lock(&r->lock); v = r->cancelled; turbo_mutex_unlock(&r->lock); }
    return v;
}
const char *cflow_run_error(const cflow_run *run) {
    run_impl *r = impl_of(run); const char *v = "run is null";
    if (r) { turbo_mutex_lock(&r->lock); v = r->error; turbo_mutex_unlock(&r->lock); }
    return v;
}
size_t cflow_run_outstanding_demand(const cflow_run *run) {
    run_impl *r = impl_of(run); return r ? demand_get(r) : 0;
}
