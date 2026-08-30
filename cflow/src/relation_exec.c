#include <cflow/relation_exec.h>
#include <cflow/coord.h>
#include <cflow/subflow.h>

#include "value_storage.h"

#include <stdlib.h>
#include <string.h>

typedef struct relation_child_policy_state {
    cflow_resumable inner;
    cflow_relation_error policy;
} relation_child_policy_state;

typedef struct relation_state {
    cflow_relation_schema schema;
    cmeta_callable reducer;
    bool has_reducer;
    cflow_resumable coord;
    size_t child_count;
    const cmeta_type_desc *value_type;
    cflow_value_slot last_value;
} relation_state;

static cflow_step child_policy_resume(void *state, cflow_publish_context *ctx, void *out) {
    relation_child_policy_state *s = (relation_child_policy_state *)state;
    if (!s || !s->inner.ops || !s->inner.ops->resume)
        return (cflow_step){ CFLOW_STEP_ERROR, {0}, "relation child policy state is invalid" };
    cflow_step step = s->inner.ops->resume(s->inner.state, ctx, out);
    if (step.kind == CFLOW_STEP_ERROR && s->policy != CFLOW_REL_ERROR_FAIL_FAST)
        return (cflow_step){ CFLOW_STEP_DONE, {0}, NULL };
    return step;
}

static void child_policy_cancel(void *state) {
    relation_child_policy_state *s = (relation_child_policy_state *)state;
    if (s && s->inner.ops && s->inner.ops->cancel) s->inner.ops->cancel(s->inner.state);
}

static void child_policy_destroy(void *state) {
    relation_child_policy_state *s = (relation_child_policy_state *)state;
    if (!s) return;
    if (s->inner.ops && s->inner.ops->destroy) s->inner.ops->destroy(s->inner.state);
    free(s);
}

static const cflow_resumable_ops child_policy_ops = {
    child_policy_resume, child_policy_cancel, child_policy_destroy
};

static bool wrap_child_policy(cflow_resumable *machine, cflow_relation_error policy) {
    if (!machine || !machine->ops || policy == CFLOW_REL_ERROR_FAIL_FAST) return machine && machine->ops;
    relation_child_policy_state *s = calloc(1, sizeof(*s));
    if (!s) return false;
    s->inner = *machine;
    s->policy = policy;
    *machine = (cflow_resumable){ "relation-child-policy", s->inner.output_type,
                                  &child_policy_ops, s };
    return true;
}

static cflow_coord_mode coord_mode(cflow_relation_schema schema) {
    if (schema.coordination == CFLOW_REL_COORD_ALL &&
        schema.completion == CFLOW_REL_COMPLETE_ALL_DONE)
        return CFLOW_COORD_ALL_DONE;
    switch (schema.coordination) {
        case CFLOW_REL_COORD_ALL: return CFLOW_COORD_ALL;
        case CFLOW_REL_COORD_ANY: return CFLOW_COORD_ANY;
        case CFLOW_REL_COORD_LATEST: return CFLOW_COORD_LATEST;
        case CFLOW_REL_COORD_SEQUENCE: return CFLOW_COORD_SEQUENCE;
    }
    return CFLOW_COORD_ALL;
}

static bool fold_values(relation_state *rel, void *out) {
    const cmeta_type_desc *type = NULL;
    const void *value = NULL;
    if (!rel || !out || !rel->has_reducer || rel->child_count == 0 ||
        !cflow_coord_value(&rel->coord, 0, &type, &value) ||
        !cmeta_type_equal(type, rel->value_type) || !value) return false;
    cflow_value_slot accumulator = {0};
    if (!cflow_value_slot_init(&accumulator, rel->value_type) ||
        !cflow_value_slot_copy(&accumulator, value)) {
        cflow_value_slot_destroy(&accumulator);
        return false;
    }
    for (size_t i = 1; i < rel->child_count; ++i) {
        cflow_value_slot next = {0};
        const cmeta_type_desc *child_type = NULL;
        const void *child_value = NULL;
        if (!cflow_coord_value(&rel->coord, i, &child_type, &child_value) ||
            !cmeta_type_equal(child_type, rel->value_type) || !child_value) {
            cflow_value_slot_destroy(&accumulator);
            return false;
        }
        const void *args[2] = { accumulator.storage, child_value };
        if (!cflow_value_slot_init(&next, rel->value_type) ||
            !cmeta_callable_invoke(&rel->reducer, next.storage, args)) {
            cflow_value_slot_destroy(&next);
            cflow_value_slot_destroy(&accumulator);
            return false;
        }
        next.live = true;
        cflow_value_slot_destroy(&accumulator);
        accumulator = next;
    }
    const bool copied = cflow_value_construct(
        rel->value_type, out, accumulator.storage);
    cflow_value_slot_destroy(&accumulator);
    return copied;
}

static bool invoke_values(relation_state *rel, void *out) {
    const cmeta_sig_desc *sig = rel && rel->has_reducer ? cmeta_callable_signature(rel->reducer) : NULL;
    if (!rel || !out || !sig || sig->protocol != CMETA_FN_PROTOCOL_VALUE ||
        sig->param_count != rel->child_count || rel->child_count != 2u) return false;
    const void *args[2] = { NULL, NULL };
    for (size_t i = 0; i < rel->child_count; ++i) {
        const cmeta_type_desc *type = NULL;
        const void *value = NULL;
        if (!cflow_coord_value(&rel->coord, i, &type, &value) ||
            !cmeta_type_equal(type, sig->params[i]) || !value) return false;
        args[i] = value;
    }
    return cmeta_callable_invoke(&rel->reducer, out, args);
}

static bool relation_result(relation_state *rel, cflow_coord_event event, void *out) {
    if (rel->schema.result == CFLOW_REL_RESULT_FOLD)
        return fold_values(rel, out);
    if (rel->schema.result == CFLOW_REL_RESULT_INVOKE)
        return invoke_values(rel, out);

    const cmeta_type_desc *type = NULL;
    const void *value = NULL;
    if (event.child_index >= rel->child_count ||
        !cflow_coord_value(&rel->coord, event.child_index, &type, &value) ||
        !cmeta_type_equal(type, rel->value_type) || !value)
        return false;
    return cflow_value_construct(rel->value_type, out, value);
}

static cflow_step relation_resume(void *state, cflow_publish_context *ctx, void *out) {
    relation_state *rel = (relation_state *)state;
    if (!rel || !rel->coord.ops || !rel->coord.ops->resume || !rel->value_type || !out)
        return (cflow_step){ CFLOW_STEP_ERROR, {0}, "relation coordinator is invalid" };

    for (;;) {
        cflow_coord_event event = {0};
        cflow_step step = rel->coord.ops->resume(rel->coord.state, ctx, &event);
        if (step.kind == CFLOW_STEP_WAIT || step.kind == CFLOW_STEP_ERROR)
            return step;
        if (step.kind == CFLOW_STEP_DONE) {
            if (rel->schema.completion == CFLOW_REL_COMPLETE_ALL_DONE &&
                rel->last_value.live) {
                if (!cflow_value_construct(rel->value_type, out,
                                           rel->last_value.storage))
                    return (cflow_step){ CFLOW_STEP_ERROR, {0},
                                         "relation result construction failed" };
                cflow_value_slot_reset(&rel->last_value);
                return (cflow_step){ CFLOW_STEP_VALUE_AND_DONE, {0}, NULL };
            }
            return step;
        }
        if (step.kind != CFLOW_STEP_VALUE && step.kind != CFLOW_STEP_VALUE_AND_DONE)
            return (cflow_step){ CFLOW_STEP_ERROR, {0}, "relation coordinator returned invalid step" };

        void *target = out;
        if (rel->schema.completion == CFLOW_REL_COMPLETE_ALL_DONE) {
            cflow_value_slot_reset(&rel->last_value);
            target = rel->last_value.storage;
        }
        if (!relation_result(rel, event, target))
            return (cflow_step){ CFLOW_STEP_ERROR, {0}, "relation result materialization failed" };

        bool done = step.kind == CFLOW_STEP_VALUE_AND_DONE;
        if (rel->schema.completion == CFLOW_REL_COMPLETE_FIRST_RESULT) {
            if (!done && rel->coord.ops->cancel) rel->coord.ops->cancel(rel->coord.state);
            return (cflow_step){ CFLOW_STEP_VALUE_AND_DONE, {0}, NULL };
        }
        if (rel->schema.completion == CFLOW_REL_COMPLETE_ALL_DONE) {
            rel->last_value.live = true;
            if (done) {
                if (!cflow_value_construct(rel->value_type, out,
                                           rel->last_value.storage))
                    return (cflow_step){ CFLOW_STEP_ERROR, {0},
                                         "relation result construction failed" };
                cflow_value_slot_reset(&rel->last_value);
                return (cflow_step){ CFLOW_STEP_VALUE_AND_DONE, {0}, NULL };
            }
            continue; /* swallow intermediate relation values */
        }
        return (cflow_step){ done ? CFLOW_STEP_VALUE_AND_DONE : CFLOW_STEP_VALUE, {0}, NULL };
    }
}

static void relation_cancel(void *state) {
    relation_state *rel = (relation_state *)state;
    if (rel && rel->coord.ops && rel->coord.ops->cancel) rel->coord.ops->cancel(rel->coord.state);
}

static void relation_destroy(void *state) {
    relation_state *rel = (relation_state *)state;
    if (!rel) return;
    if (rel->coord.ops && rel->coord.ops->destroy) rel->coord.ops->destroy(rel->coord.state);
    cflow_value_slot_destroy(&rel->last_value);
    free(rel);
}

static const cflow_resumable_ops relation_ops = {
    relation_resume, relation_cancel, relation_destroy
};

bool cflow_resumable_from_relation(cflow_resumable *out,
                                    const cflow_graph *graph,
                                    const cflow_node *node,
                                    const void *input) {
    return cflow_resumable_from_relation_with_options(
        out, graph, node, input, NULL);
}

bool cflow_resumable_from_relation_with_options(
    cflow_resumable *out,
    const cflow_graph *graph,
    const cflow_node *node,
    const void *input,
    const cflow_eval_options *options) {
    if (!out || !graph || !cflow_value_runtime_graph_supported(graph) ||
        !node || node->op != CFLOW_OP_RELATION ||
        !node->has_relation || !input || node->subgraph_count == 0u || !node->output_type)
        return false;

    cflow_resumable *children = calloc(node->subgraph_count, sizeof(*children));
    if (!children) return false;
    size_t made = 0;
    for (; made < node->subgraph_count; ++made) {
        if (!cflow_resumable_from_subgraph_with_options(
                &children[made], graph, node->subgraphs[made], input,
                options) ||
            !wrap_child_policy(&children[made], node->relation.error)) break;
    }
    if (made != node->subgraph_count) {
        for (size_t k = 0; k <= made && k < node->subgraph_count; ++k)
            if (children[k].ops && children[k].ops->destroy)
                children[k].ops->destroy(children[k].state);
        free(children);
        return false;
    }

    relation_state *rel = calloc(1, sizeof(*rel));
    if (!rel) {
        for (size_t k = 0; k < made; ++k)
            if (children[k].ops && children[k].ops->destroy)
                children[k].ops->destroy(children[k].state);
        free(children);
        return false;
    }
    rel->schema = node->relation;
    rel->reducer = node->fn;
    rel->has_reducer = node->has_fn;
    rel->child_count = node->subgraph_count;
    rel->value_type = node->output_type;
    if (node->relation.completion == CFLOW_REL_COMPLETE_ALL_DONE) {
        if (!cflow_value_slot_init(&rel->last_value, rel->value_type)) {
            free(rel);
            goto fail_children;
        }
    }
    if (!cflow_resumable_from_coordination(&rel->coord, coord_mode(node->relation),
                                            children, node->subgraph_count)) {
        cflow_value_slot_destroy(&rel->last_value);
        free(rel);
        goto fail_children;
    }
    free(children);
    *out = (cflow_resumable){ "relation", node->output_type, &relation_ops, rel };
    return true;

fail_children:
    for (size_t k = 0; k < made; ++k)
        if (children[k].ops && children[k].ops->destroy)
            children[k].ops->destroy(children[k].state);
    free(children);
    return false;
}
