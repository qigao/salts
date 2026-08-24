#include <cflow/cflow.h>
#include <turbo/thread.h>
#include "cflow_test_ops.h"
#include "../src/value_storage.h"
#include "tinytest.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct lifecycle_test_value {
    int *resource;
} lifecycle_test_value;

static size_t lifecycle_test_copies;
static size_t lifecycle_test_moves;
static size_t lifecycle_test_destroys;
static bool lifecycle_test_copy_fails;
static size_t lifecycle_test_copy_fail_at;

static bool lifecycle_test_copy(void *destination_, const void *source_) {
    lifecycle_test_value *destination = (lifecycle_test_value *)destination_;
    const lifecycle_test_value *source =
        (const lifecycle_test_value *)source_;

    ++lifecycle_test_copies;
    destination->resource = NULL;
    if (lifecycle_test_copy_fails ||
        (lifecycle_test_copy_fail_at != 0u &&
         lifecycle_test_copies == lifecycle_test_copy_fail_at))
        return false;
    if (!source->resource)
        return true;
    destination->resource = (int *)malloc(sizeof(*destination->resource));
    if (!destination->resource)
        return false;
    *destination->resource = *source->resource;
    return true;
}

static void lifecycle_test_move(void *destination_, void *source_) {
    lifecycle_test_value *destination = (lifecycle_test_value *)destination_;
    lifecycle_test_value *source = (lifecycle_test_value *)source_;

    ++lifecycle_test_moves;
    destination->resource = source->resource;
    source->resource = NULL;
}

static void lifecycle_test_destroy(void *value_) {
    lifecycle_test_value *value = (lifecycle_test_value *)value_;

    ++lifecycle_test_destroys;
    free(value->resource);
    value->resource = NULL;
}

static const cmeta_type_traits lifecycle_test_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = lifecycle_test_copy,
    .move_construct = lifecycle_test_move,
    .destroy = lifecycle_test_destroy
};

static const cmeta_type_desc lifecycle_test_type = {
    .name = "lifecycle_test_value",
    .size = sizeof(lifecycle_test_value),
    .align = _Alignof(lifecycle_test_value),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &lifecycle_test_traits,
    .identity = NULL
};

static const cmeta_type_traits lifecycle_overaligned_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY
};

static const cmeta_type_desc lifecycle_overaligned_type = {
    .name = "lifecycle_overaligned",
    .size = 64u,
    .align = 64u,
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &lifecycle_overaligned_traits,
    .identity = NULL
};

static lifecycle_test_value lifecycle_test_make(int value) {
    lifecycle_test_value out = {0};

    out.resource = (int *)malloc(sizeof(*out.resource));
    if (out.resource)
        *out.resource = value;
    return out;
}

static void lifecycle_test_reset(void) {
    lifecycle_test_copies = 0u;
    lifecycle_test_moves = 0u;
    lifecycle_test_destroys = 0u;
    lifecycle_test_copy_fails = false;
    lifecycle_test_copy_fail_at = 0u;
}

typedef struct lifecycle_range_owner {
    lifecycle_test_value values[2];
    size_t count;
} lifecycle_range_owner;

typedef struct lifecycle_collect_output {
    lifecycle_test_value values[2];
    size_t count;
} lifecycle_collect_output;

typedef struct lifecycle_collector_state {
    lifecycle_collect_output staged;
    lifecycle_collect_output *output;
    size_t aborts;
} lifecycle_collector_state;

static size_t lifecycle_range_size(const void *object) {
    const lifecycle_range_owner *owner =
        (const lifecycle_range_owner *)object;

    return owner ? owner->count : 0u;
}

static cmeta_gen_status lifecycle_range_next(const void *object,
                                              cmeta_range_cursor *cursor,
                                              void *out_value) {
    const lifecycle_range_owner *owner =
        (const lifecycle_range_owner *)object;

    if (!owner || !cursor || !out_value)
        return CMETA_GEN_ERROR;
    if (cursor->index >= owner->count)
        return CMETA_GEN_DONE;
    if (!lifecycle_test_copy(out_value, &owner->values[cursor->index]))
        return CMETA_GEN_ERROR;
    ++cursor->index;
    return cursor->index == owner->count ? CMETA_GEN_VALUE_AND_DONE
                                         : CMETA_GEN_VALUE;
}

static cmeta_status lifecycle_collector_begin(
    void *context, const cmeta_type_desc *input, size_t limit) {
    lifecycle_collector_state *state =
        (lifecycle_collector_state *)context;

    if (!state || !state->output ||
        !cmeta_type_equal(input, &lifecycle_test_type) || limit > 2u)
        return CMETA_INVALID_ARGUMENT;
    return CMETA_OK;
}

static cmeta_status lifecycle_collector_accept(void *context,
                                                const void *value) {
    lifecycle_collector_state *state =
        (lifecycle_collector_state *)context;

    if (!state || !value)
        return CMETA_INVALID_ARGUMENT;
    if (state->staged.count >= 2u)
        return CMETA_CAPACITY_EXCEEDED;
    if (!lifecycle_test_copy(
            &state->staged.values[state->staged.count], value))
        return CMETA_OUT_OF_MEMORY;
    ++state->staged.count;
    return CMETA_OK;
}

static cmeta_status lifecycle_collector_finish(void *context) {
    lifecycle_collector_state *state =
        (lifecycle_collector_state *)context;

    if (!state || !state->output)
        return CMETA_INVALID_ARGUMENT;
    *state->output = state->staged;
    memset(&state->staged, 0, sizeof(state->staged));
    return CMETA_OK;
}

static void lifecycle_collector_abort(void *context) {
    lifecycle_collector_state *state =
        (lifecycle_collector_state *)context;
    size_t index;

    if (!state)
        return;
    for (index = 0u; index < state->staged.count; ++index)
        lifecycle_test_destroy(&state->staged.values[index]);
    memset(&state->staged, 0, sizeof(state->staged));
    ++state->aborts;
}

static const cmeta_collector_ops lifecycle_collector_ops = {
    lifecycle_collector_begin,
    lifecycle_collector_accept,
    lifecycle_collector_finish,
    lifecycle_collector_abort
};

static cmeta_collector lifecycle_collector(
    lifecycle_collector_state *state,
    lifecycle_collect_output *output) {
    cmeta_collector collector = {
        .ops = &lifecycle_collector_ops,
        .context = state,
        .zero_output = output,
        .input_type = &lifecycle_test_type,
        .limit = 2u,
        .count = 0u,
        .state = CMETA_COLLECTOR_ZERO,
        .status = CMETA_OK
    };

    state->output = output;
    return collector;
}

static void lifecycle_collect_output_destroy(
    lifecycle_collect_output *output) {
    size_t index;

    if (!output)
        return;
    for (index = 0u; index < output->count; ++index)
        lifecycle_test_destroy(&output->values[index]);
    memset(output, 0, sizeof(*output));
}

typedef struct lifecycle_sink_state {
    size_t values;
    int sum;
    bool reject;
    bool done;
    bool failed;
} lifecycle_sink_state;

static bool lifecycle_sink_value(void *user,
                                 const cmeta_type_desc *type,
                                 const void *value_) {
    lifecycle_sink_state *state = (lifecycle_sink_state *)user;
    const lifecycle_test_value *value =
        (const lifecycle_test_value *)value_;

    if (!state || !cmeta_type_equal(type, &lifecycle_test_type) ||
        !value || !value->resource)
        return false;
    ++state->values;
    state->sum += *value->resource;
    return !state->reject;
}

static void lifecycle_sink_error(void *user, const char *message) {
    lifecycle_sink_state *state = (lifecycle_sink_state *)user;

    (void)message;
    if (state)
        state->failed = true;
}

static void lifecycle_sink_done(void *user) {
    lifecycle_sink_state *state = (lifecycle_sink_state *)user;

    if (state)
        state->done = true;
}

typedef struct lifecycle_close_sink_state {
    cflow_run *run;
    size_t values;
    bool close_returned;
} lifecycle_close_sink_state;

static bool lifecycle_close_sink_value(void *user,
                                       const cmeta_type_desc *type,
                                       const void *value_) {
    lifecycle_close_sink_state *state =
        (lifecycle_close_sink_state *)user;
    const lifecycle_test_value *value =
        (const lifecycle_test_value *)value_;

    if (!state || !state->run ||
        !cmeta_type_equal(type, &lifecycle_test_type) || !value ||
        !value->resource)
        return false;
    ++state->values;
    cflow_run_close(state->run);
    state->close_returned = true;
    return true;
}

static void lifecycle_close_sink_error(void *user, const char *message) {
    (void)user;
    (void)message;
}

static void lifecycle_close_sink_done(void *user) {
    (void)user;
}

typedef struct close_from_sink_state {
    cflow_run *run;
    size_t values;
    bool close_returned;
} close_from_sink_state;

static bool close_from_sink_value(void *user,
                                  const cmeta_type_desc *type,
                                  const void *value) {
    close_from_sink_state *state = (close_from_sink_state *)user;
    if (!state || !cmeta_type_equal(type, &cmeta_type_int) || !value)
        return false;
    ++state->values;
    cflow_run_close(state->run);
    state->close_returned = true;
    return true;
}

static void close_from_sink_error(void *user, const char *message) {
    (void)user;
    (void)message;
}

static void close_from_sink_done(void *user) {
    (void)user;
}

typedef struct concurrent_close_state {
    cflow_run *run;
    turbo_mutex_t lock;
    turbo_cond_t changed;
    bool callback_entered;
    bool external_started;
    bool callback_returned;
    bool external_returned;
} concurrent_close_state;

static bool concurrent_close_value(void *user,
                                   const cmeta_type_desc *type,
                                   const void *value) {
    concurrent_close_state *state = (concurrent_close_state *)user;
    if (!state || !cmeta_type_equal(type, &cmeta_type_int) || !value)
        return false;

    turbo_mutex_lock(&state->lock);
    state->callback_entered = true;
    turbo_cond_broadcast(&state->changed);
    while (!state->external_started)
        turbo_cond_wait(&state->changed, &state->lock);
    turbo_mutex_unlock(&state->lock);

    cflow_run_close(state->run);

    turbo_mutex_lock(&state->lock);
    state->callback_returned = true;
    turbo_cond_broadcast(&state->changed);
    turbo_mutex_unlock(&state->lock);
    return true;
}

static void concurrent_external_close(void *user) {
    concurrent_close_state *state = (concurrent_close_state *)user;
    if (!state) return;

    turbo_mutex_lock(&state->lock);
    state->external_started = true;
    turbo_cond_broadcast(&state->changed);
    turbo_mutex_unlock(&state->lock);

    cflow_run_close(state->run);

    turbo_mutex_lock(&state->lock);
    state->external_returned = true;
    turbo_cond_broadcast(&state->changed);
    turbo_mutex_unlock(&state->lock);
}

typedef struct destroy_reentrant_close_state {
    cflow_run *run;
    bool close_returned;
} destroy_reentrant_close_state;

static cflow_read_status destroy_reentrant_read(void *user,
                                                void *out_value,
                                                const char **error) {
    (void)user;
    (void)out_value;
    (void)error;
    return CFLOW_READ_DONE;
}

static bool destroy_reentrant_arm(void *user, cflow_waker waker) {
    (void)user;
    (void)waker;
    return true;
}

static void destroy_reentrant_close(void *user) {
    destroy_reentrant_close_state *state =
        (destroy_reentrant_close_state *)user;
    if (!state) return;
    cflow_run_close(state->run);
    state->close_returned = true;
}

static size_t owned_range_size(const void *object) {
    (void)object;
    return 1u;
}

static cmeta_gen_status owned_range_next(const void *object,
                                         cmeta_range_cursor *cursor,
                                         void *out_value) {
    (void)object;
    (void)cursor;
    (void)out_value;
    return CMETA_GEN_DONE;
}

static cmeta_gen_status lifecycle_constructing_range_next(
    const void *object,
    cmeta_range_cursor *cursor,
    void *out_value) {
    if (!object || !cursor || !out_value)
        return CMETA_GEN_ERROR;
    if (cursor->index != 0u)
        return CMETA_GEN_DONE;
    if (!lifecycle_test_copy(out_value, object))
        return CMETA_GEN_ERROR;
    ++cursor->index;
    return CMETA_GEN_VALUE_AND_DONE;
}

typedef struct owned_source_state {
    bool destroyed;
} owned_source_state;

static const char *owned_source_name(void *state) {
    (void)state;
    return "owned_source";
}

static const cmeta_type_desc *owned_source_type(void *state) {
    (void)state;
    return &cflow_test_owned_value_type;
}

static cflow_step owned_source_resume(void *state,
                                      cflow_resume_ctx *ctx,
                                      void *out_value) {
    (void)state;
    (void)ctx;
    (void)out_value;
    return (cflow_step){CFLOW_STEP_DONE, {0}, NULL};
}

static void owned_source_noop(void *state) {
    (void)state;
}

static void owned_source_destroy(void *state) {
    owned_source_state *owned = (owned_source_state *)state;

    if (owned)
        owned->destroyed = true;
}

static void owned_source_bind(void *state, cflow_waker waker) {
    (void)state;
    (void)waker;
}

static cflow_source_terminal owned_source_poll(void *state,
                                                const char **error) {
    (void)state;
    (void)error;
    return CFLOW_SOURCE_OPEN;
}

static const cflow_resumable_ops owned_machine_ops = {
    owned_source_resume,
    owned_source_noop,
    owned_source_destroy
};

CMETA_IMPLEMENTS(cflow_source, owned_source, 0,
    .name = owned_source_name,
    .output_type = owned_source_type,
    .resume = owned_source_resume,
    .cancel = owned_source_noop,
    .destroy = owned_source_destroy,
    .bind_terminal_waker = owned_source_bind,
    .poll_terminal = owned_source_poll
);

suite("CFlow runtime") {
    group("managed value slots") {
        before_each() {
            lifecycle_test_reset();
        }

        it("copy-constructs and destroys an owning value exactly once") {
            lifecycle_test_value source = lifecycle_test_make(17);
            cflow_value_slot slot = {0};

            check_not_null(source.resource);
            check_true(cflow_value_slot_init(&slot, &lifecycle_test_type));
            check_true(cflow_value_slot_copy(&slot, &source));
            check_not_null(((lifecycle_test_value *)slot.storage)->resource);
            check_true(((lifecycle_test_value *)slot.storage)->resource !=
                       source.resource);
            check_equal(
                *((lifecycle_test_value *)slot.storage)->resource,
                17);

            cflow_value_slot_destroy(&slot);
            lifecycle_test_destroy(&source);
            check_equal(lifecycle_test_copies, (size_t)1u);
            check_equal(lifecycle_test_destroys, (size_t)2u);
        }

        it("leaves a failed copy destination empty") {
            lifecycle_test_value source = lifecycle_test_make(23);
            cflow_value_slot slot = {0};

            lifecycle_test_copy_fails = true;
            check_not_null(source.resource);
            check_true(cflow_value_slot_init(&slot, &lifecycle_test_type));
            check_false(cflow_value_slot_copy(&slot, &source));
            check_false(slot.live);

            cflow_value_slot_destroy(&slot);
            check_equal(lifecycle_test_destroys, (size_t)0u);
            lifecycle_test_destroy(&source);
            check_equal(lifecycle_test_destroys, (size_t)1u);
        }

        it("move-constructs then destroys the moved-from slot") {
            lifecycle_test_value source = lifecycle_test_make(31);
            cflow_value_slot from = {0};
            cflow_value_slot to = {0};

            check_not_null(source.resource);
            check_true(cflow_value_slot_init(&from, &lifecycle_test_type));
            check_true(cflow_value_slot_init(&to, &lifecycle_test_type));
            check_true(cflow_value_slot_copy(&from, &source));
            lifecycle_test_destroy(&source);
            check_true(cflow_value_slot_move(&to, &from));
            check_false(from.live);
            check_true(to.live);
            check_equal(lifecycle_test_moves, (size_t)1u);
            check_equal(lifecycle_test_destroys, (size_t)2u);

            cflow_value_slot_destroy(&from);
            cflow_value_slot_destroy(&to);
            check_equal(lifecycle_test_destroys, (size_t)3u);
        }

        it("honors descriptor alignment") {
            cflow_value_slot slot = {0};

            check_true(cflow_value_slot_init(
                &slot, &lifecycle_overaligned_type));
            check_equal((uintptr_t)slot.storage % 64u, (uintptr_t)0u);
            cflow_value_slot_destroy(&slot);
        }
    }

    it("rejects one-shot values that require lifecycle callbacks") {
        const cflow_test_owned_value value = {0};
        cflow_resumable machine = {0};
        const bool initialized = cflow_resumable_from_value(
            &machine, &cflow_test_owned_value_type, &value);

        check_false(initialized);
        check_null(machine.ops);
        if (initialized)
            machine.ops->destroy(machine.state);
    }

    it("rejects owning coordination children before ownership transfer") {
        owned_source_state state = {false};
        cflow_resumable children[] = {{
            "owned_machine",
            &cflow_test_owned_value_type,
            &owned_machine_ops,
            &state
        }};
        cflow_resumable coordination = {0};
        const bool initialized = cflow_resumable_from_coordination(
            &coordination, CFLOW_COORD_ALL, children, 1u);

        check_false(initialized);
        check_null(coordination.ops);
        check_not_null(children[0].ops);
        check_false(state.destroyed);
        if (initialized)
            coordination.ops->destroy(coordination.state);
        else
            children[0].ops->destroy(children[0].state);
        check_true(state.destroyed);
    }

    it("rejects owning SubRuns before copying their input") {
        const cflow_test_owned_value value = {0};
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_resumable machine = {0};
        bool initialized;

        normalized.root = CMETA_INVALID_ID;
        cflow_graph_init(&surface, &cflow_test_owned_value_type);
        check_true(cflow_graph_normalize(&normalized, &surface));

        initialized = cflow_resumable_from_subgraph(
            &machine, &normalized, normalized.root, &value);

        check_false(initialized);
        check_null(machine.ops);
        if (initialized)
            machine.ops->destroy(machine.state);

        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);
    }

    it("rejects an owning source before run ownership transfer") {
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_scheduler scheduler = {0};
        owned_source_state state = {false};
        cflow_source source = owned_source_as_cflow_source(&state);
        cflow_run run = {0};
        bool opened;

        normalized.root = CMETA_INVALID_ID;
        cflow_graph_init(&surface, &cflow_test_owned_value_type);
        check_true(cflow_graph_normalize(&normalized, &surface));
        check_true(cflow_scheduler_test_init(&scheduler));

        opened = cflow_run_open(
            &run, &normalized, &source, &scheduler, NULL);

        check_false(opened);
        check_null(run.impl);
        check_not_null(source.self);
        check_false(state.destroyed);
        if (opened)
            cflow_run_close(&run);
        else
            cflow_source_destroy(&source);
        check_true(state.destroyed);

        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);
    }

    it("rejects managed graphs once an operator enters the path") {
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_subgraph *root;

        normalized.root = CMETA_INVALID_ID;
        cflow_graph_init(&surface, &lifecycle_test_type);
        check_true(cflow_graph_normalize(&normalized, &surface));
        check_true(cflow_value_runtime_graph_supported(&normalized));

        root = &normalized.subgraphs[normalized.root];
        root->nodes[root->entry].op = CFLOW_OP_FILTER;
        check_false(cflow_value_runtime_graph_supported(&normalized));

        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);
    }

    it("creates a constructing array source for managed values") {
        lifecycle_test_value value = lifecycle_test_make(41);
        lifecycle_test_value output = {0};
        cflow_source source = {0};
        cflow_step step;

        lifecycle_test_reset();
        check_not_null(value.resource);
        check_true(cflow_source_from_array(
            &source, &lifecycle_test_type, &value, 1u));
        check_true(cflow_source_has(
            &source, CFLOW_SOURCE_CAP_CONSTRUCTS_VALUES));
        step = cflow_source_resume(&source, NULL, &output);
        check_true(step.kind == CFLOW_STEP_VALUE_AND_DONE);
        check_not_null(output.resource);
        check_true(output.resource != value.resource);
        check_equal(*output.resource, 41);

        lifecycle_test_destroy(&output);
        lifecycle_test_destroy(&value);
        cflow_source_destroy(&source);
        check_equal(lifecycle_test_copies, (size_t)1u);
        check_equal(lifecycle_test_destroys, (size_t)2u);
    }

    it("rejects range sources whose values require lifecycle callbacks") {
        const cflow_test_owned_value value = {0};
        const cmeta_range range = {
            .object = &value,
            .element_type = &cflow_test_owned_value_type,
            .flags = CMETA_RANGE_SIZED,
            .size = owned_range_size,
            .next = owned_range_next,
            .version = 0u,
            .current_version = NULL
        };
        cflow_source source = {0};
        const bool initialized = cflow_source_from_range(&source, range);

        check_false(initialized);
        check_null(source.self);
        if (initialized)
            cflow_source_destroy(&source);
    }

    it("creates a managed source from a constructing range") {
        lifecycle_test_value value = lifecycle_test_make(43);
        lifecycle_test_value output = {0};
        const cmeta_range range = {
            .object = &value,
            .element_type = &lifecycle_test_type,
            .flags = CMETA_RANGE_SIZED | CMETA_RANGE_CONSTRUCTS_VALUES,
            .size = owned_range_size,
            .next = lifecycle_constructing_range_next,
            .version = 0u,
            .current_version = NULL
        };
        cflow_source source = {0};
        cflow_step step;

        lifecycle_test_reset();
        check_not_null(value.resource);
        check_true(cflow_source_from_range(&source, range));
        check_true(cflow_source_has(
            &source, CFLOW_SOURCE_CAP_CONSTRUCTS_VALUES));
        step = cflow_source_resume(&source, NULL, &output);
        check_true(step.kind == CFLOW_STEP_VALUE_AND_DONE);
        check_not_null(output.resource);
        check_true(output.resource != value.resource);
        check_equal(*output.resource, 43);

        lifecycle_test_destroy(&output);
        lifecycle_test_destroy(&value);
        cflow_source_destroy(&source);
        check_equal(lifecycle_test_copies, (size_t)1u);
        check_equal(lifecycle_test_destroys, (size_t)2u);
    }

    it("runs a managed array through a source-only graph") {
        lifecycle_test_value input[] = {
            lifecycle_test_make(5), lifecycle_test_make(8)
        };
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_scheduler scheduler = {0};
        cflow_source source = {0};
        cflow_run run = {0};
        lifecycle_sink_state state = {0};
        cflow_sink_callbacks callbacks = {
            lifecycle_sink_value,
            lifecycle_sink_error,
            lifecycle_sink_done,
            &state
        };
        cflow_sink sink = cflow_sink_from_callbacks(&callbacks);
        bool opened;

        lifecycle_test_reset();
        normalized.root = CMETA_INVALID_ID;
        check_not_null(input[0].resource);
        check_not_null(input[1].resource);
        cflow_graph_init(&surface, &lifecycle_test_type);
        check_true(cflow_graph_normalize(&normalized, &surface));
        check_true(cflow_scheduler_test_init(&scheduler));
        check_true(cflow_source_from_array(
            &source, &lifecycle_test_type, input, 2u));
        opened = cflow_run_open(
            &run, &normalized, &source, &scheduler, &sink);
        check_true(opened);
        if (opened) {
            check_true(cflow_run_request(&run, 2u));
            (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
            check_true(cflow_run_is_done(&run));
            cflow_run_close(&run);
        } else {
            cflow_source_destroy(&source);
        }

        check_equal(state.values, (size_t)2u);
        check_equal(state.sum, 13);
        check_true(state.done);
        check_false(state.failed);
        lifecycle_test_destroy(&input[0]);
        lifecycle_test_destroy(&input[1]);
        check_equal(lifecycle_test_copies, (size_t)2u);
        check_equal(lifecycle_test_destroys, (size_t)4u);

        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);
    }

    it("destroys a managed value when the sink rejects it") {
        lifecycle_test_value input = lifecycle_test_make(11);
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_scheduler scheduler = {0};
        cflow_source source = {0};
        cflow_run run = {0};
        lifecycle_sink_state state = {.reject = true};
        cflow_sink_callbacks callbacks = {
            lifecycle_sink_value,
            lifecycle_sink_error,
            lifecycle_sink_done,
            &state
        };
        cflow_sink sink = cflow_sink_from_callbacks(&callbacks);

        lifecycle_test_reset();
        normalized.root = CMETA_INVALID_ID;
        check_not_null(input.resource);
        cflow_graph_init(&surface, &lifecycle_test_type);
        check_true(cflow_graph_normalize(&normalized, &surface));
        check_true(cflow_scheduler_test_init(&scheduler));
        check_true(cflow_source_from_array(
            &source, &lifecycle_test_type, &input, 1u));
        check_true(cflow_run_open(
            &run, &normalized, &source, &scheduler, &sink));
        check_true(cflow_run_request(&run, 1u));
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);

        check_equal(state.values, (size_t)1u);
        check_true(state.failed);
        check_equal(cflow_run_error(&run), "observer rejected value");
        check_equal(lifecycle_test_destroys, (size_t)1u);
        cflow_run_close(&run);
        lifecycle_test_destroy(&input);
        check_equal(lifecycle_test_copies, (size_t)1u);
        check_equal(lifecycle_test_destroys, (size_t)2u);

        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);
    }

    it("cleans a managed slot after a sink closes its run") {
        lifecycle_test_value input = lifecycle_test_make(19);
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_scheduler scheduler = {0};
        cflow_source source = {0};
        cflow_run run = {0};
        lifecycle_close_sink_state state = {&run, 0u, false};
        cflow_sink_callbacks callbacks = {
            lifecycle_close_sink_value,
            lifecycle_close_sink_error,
            lifecycle_close_sink_done,
            &state
        };
        cflow_sink sink = cflow_sink_from_callbacks(&callbacks);

        lifecycle_test_reset();
        normalized.root = CMETA_INVALID_ID;
        check_not_null(input.resource);
        cflow_graph_init(&surface, &lifecycle_test_type);
        check_true(cflow_graph_normalize(&normalized, &surface));
        check_true(cflow_scheduler_test_init(&scheduler));
        check_true(cflow_source_from_array(
            &source, &lifecycle_test_type, &input, 1u));
        check_true(cflow_run_open(
            &run, &normalized, &source, &scheduler, &sink));
        check_true(cflow_run_request(&run, 1u));
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);

        check_equal(state.values, (size_t)1u);
        check_true(state.close_returned);
        check_null(run.impl);
        check_equal(lifecycle_test_destroys, (size_t)1u);
        lifecycle_test_destroy(&input);
        check_equal(lifecycle_test_copies, (size_t)1u);
        check_equal(lifecycle_test_destroys, (size_t)2u);

        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);
    }

    it("collects a managed range with independent ownership") {
        lifecycle_range_owner owner = {{
            lifecycle_test_make(3), lifecycle_test_make(7)
        }, 2u};
        lifecycle_collect_output output = {0};
        lifecycle_collector_state collector_state = {0};
        cmeta_collector collector =
            lifecycle_collector(&collector_state, &output);
        cmeta_range range = {
            .object = &owner,
            .element_type = &lifecycle_test_type,
            .flags = CMETA_RANGE_SIZED | CMETA_RANGE_CONSTRUCTS_VALUES,
            .size = lifecycle_range_size,
            .next = lifecycle_range_next,
            .version = 0u,
            .current_version = NULL
        };
        cflow_stream stream = {0};
        const char *error = NULL;

        lifecycle_test_reset();
        check_not_null(owner.values[0].resource);
        check_not_null(owner.values[1].resource);
        check_not_null(cflow_stream_from_range(&stream, range));
        check_true(cflow_eval_collect(&stream, &collector, &error));
        check_null(error);
        check_true(collector.state == CMETA_COLLECTOR_COMMITTED);
        check_equal(output.count, (size_t)2u);
        check_not_null(output.values[0].resource);
        check_not_null(output.values[1].resource);
        check_true(output.values[0].resource != owner.values[0].resource);
        check_true(output.values[1].resource != owner.values[1].resource);
        check_equal(*output.values[0].resource, 3);
        check_equal(*output.values[1].resource, 7);
        check_equal(collector_state.aborts, (size_t)0u);
        check_equal(lifecycle_test_copies, (size_t)4u);
        check_equal(lifecycle_test_destroys, (size_t)2u);

        lifecycle_test_destroy(&owner.values[0]);
        lifecycle_test_destroy(&owner.values[1]);
        lifecycle_collect_output_destroy(&output);
        check_equal(lifecycle_test_destroys, (size_t)6u);
        cflow_stream_destroy(&stream);
    }

    it("aborts a managed collector when its copy construction fails") {
        lifecycle_range_owner owner = {{
            lifecycle_test_make(13), lifecycle_test_make(17)
        }, 2u};
        lifecycle_collect_output output = {0};
        lifecycle_collector_state collector_state = {0};
        cmeta_collector collector =
            lifecycle_collector(&collector_state, &output);
        cmeta_range range = {
            .object = &owner,
            .element_type = &lifecycle_test_type,
            .flags = CMETA_RANGE_SIZED | CMETA_RANGE_CONSTRUCTS_VALUES,
            .size = lifecycle_range_size,
            .next = lifecycle_range_next,
            .version = 0u,
            .current_version = NULL
        };
        cflow_stream stream = {0};
        const char *error = NULL;

        lifecycle_test_reset();
        lifecycle_test_copy_fail_at = 4u;
        check_not_null(owner.values[0].resource);
        check_not_null(owner.values[1].resource);
        check_not_null(cflow_stream_from_range(&stream, range));
        check_false(cflow_eval_collect(&stream, &collector, &error));
        check_not_null(error);
        check_true(collector.state == CMETA_COLLECTOR_ABORTED);
        check_true(collector.status == CMETA_OUT_OF_MEMORY);
        check_equal(collector_state.aborts, (size_t)1u);
        check_equal(output.count, (size_t)0u);
        check_equal(lifecycle_test_copies, (size_t)4u);
        check_equal(lifecycle_test_destroys, (size_t)3u);

        lifecycle_test_destroy(&owner.values[0]);
        lifecycle_test_destroy(&owner.values[1]);
        check_equal(lifecycle_test_destroys, (size_t)5u);
        cflow_stream_destroy(&stream);
    }

    it("keeps byte results fail-fast for managed streams") {
        lifecycle_range_owner owner = {{
            lifecycle_test_make(29), {0}
        }, 1u};
        cmeta_range range = {
            .object = &owner,
            .element_type = &lifecycle_test_type,
            .flags = CMETA_RANGE_SIZED | CMETA_RANGE_CONSTRUCTS_VALUES,
            .size = lifecycle_range_size,
            .next = lifecycle_range_next,
            .version = 0u,
            .current_version = NULL
        };
        cflow_stream stream = {0};
        cflow_result result = {0};

        lifecycle_test_reset();
        check_not_null(owner.values[0].resource);
        check_not_null(cflow_stream_from_range(&stream, range));
        check_false(cflow_eval_stream(&stream, &result));
        check_null(result.data);
        check_equal(result.count, (size_t)0u);
        check_null(result.type);
        check_equal(lifecycle_test_copies, (size_t)0u);

        lifecycle_test_destroy(&owner.values[0]);
        cflow_result_destroy(&result);
        cflow_stream_destroy(&stream);
        check_equal(lifecycle_test_destroys, (size_t)1u);
    }

    it("rejects channels whose values require lifecycle callbacks") {
        cflow_channel channel = {0};
        const bool initialized = cflow_channel_init(
            &channel, &cflow_test_owned_value_type, 1u);

        check_false(initialized);
        if (initialized)
            cflow_channel_destroy(&channel);
    }

    it("rejects readiness sources whose values require lifecycle callbacks") {
        cflow_source source = {0};
        const bool initialized = cflow_source_from_readiness(
            &source,
            "owned_readiness",
            &cflow_test_owned_value_type,
            destroy_reentrant_read,
            destroy_reentrant_arm,
            NULL,
            NULL,
            NULL);

        check_false(initialized);
        check_null(source.self);
        if (initialized)
            cflow_source_destroy(&source);
    }

    it("rejects channel storage size overflow") {
        static const cmeta_type_traits trivial_traits = {
            .flags = CMETA_TRAIT_TRIVIAL_COPY |
                     CMETA_TRAIT_TRIVIAL_DESTROY
        };
        const cmeta_type_desc three_byte_type = {
            .name = "three_byte",
            .size = 3u,
            .align = 1u,
            .kind = CMETA_T_OBJECT,
            .pointee = NULL,
            .traits = &trivial_traits,
            .identity = NULL
        };
        cflow_channel channel = {0};
        const size_t overflowing_capacity = SIZE_MAX / three_byte_type.size + 1u;
        const bool initialized = cflow_channel_init(
            &channel, &three_byte_type, overflowing_capacity);

        check_false(initialized);
        if (initialized)
            cflow_channel_destroy(&channel);
    }

    it("rejects array byte extent overflow") {
        unsigned char value = 0u;
        cflow_source source = {0};

        check_false(cflow_source_from_array(
            &source, &lifecycle_overaligned_type, &value,
            SIZE_MAX / lifecycle_overaligned_type.size + 1u));
        check_null(source.self);
    }

    it("allows a sink callback to close its run") {
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_scheduler scheduler = {0};
        cflow_source source = {0};
        cflow_run run = {0};
        close_from_sink_state state = {&run, 0u, false};
        cflow_sink_callbacks callbacks = {
            close_from_sink_value,
            close_from_sink_error,
            close_from_sink_done,
            &state
        };
        cflow_sink sink = cflow_sink_from_callbacks(&callbacks);
        const int input = 7;

        normalized.root = CMETA_INVALID_ID;
        cflow_graph_init(&surface, &cmeta_type_int);
        check_true(cflow_graph_normalize(&normalized, &surface));
        check_true(cflow_scheduler_test_init(&scheduler));
        check_true(cflow_source_from_array(
            &source, &cmeta_type_int, &input, 1u));
        check_true(cflow_run_open(
            &run, &normalized, &source, &scheduler, &sink));
        check_true(cflow_run_request(&run, 1u));

        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);

        check_equal(state.values, (size_t)1u);
        check_true(state.close_returned);
        check_null(run.impl);

        cflow_run_close(&run);
        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);
    }

    it("serializes callback and external close callers") {
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_scheduler scheduler = {0};
        cflow_source source = {0};
        cflow_run run = {0};
        concurrent_close_state state = {0};
        cflow_sink_callbacks callbacks = {
            concurrent_close_value,
            close_from_sink_error,
            close_from_sink_done,
            &state
        };
        cflow_sink sink = cflow_sink_from_callbacks(&callbacks);
        turbo_thread_t external_thread;
        const int input = 11;

        state.run = &run;
        normalized.root = CMETA_INVALID_ID;
        turbo_mutex_init(&state.lock);
        turbo_cond_init(&state.changed);
        check_not_null(state.lock);
        check_not_null(state.changed);
        cflow_graph_init(&surface, &cmeta_type_int);
        check_true(cflow_graph_normalize(&normalized, &surface));
        check_true(cflow_scheduler_worker_init(&scheduler, 1u));
        check_true(cflow_source_from_array(
            &source, &cmeta_type_int, &input, 1u));
        check_true(cflow_run_open(
            &run, &normalized, &source, &scheduler, &sink));
        check_true(cflow_run_request(&run, 1u));

        turbo_mutex_lock(&state.lock);
        while (!state.callback_entered)
            turbo_cond_wait(&state.changed, &state.lock);
        turbo_mutex_unlock(&state.lock);
        check_equal(turbo_thread_create(
            &external_thread, concurrent_external_close, &state), 0);
        check_equal(turbo_thread_join(&external_thread), 0);
        check_true(cflow_scheduler_wait_idle(&scheduler));

        check_true(state.callback_returned);
        check_true(state.external_returned);
        check_null(run.impl);

        cflow_run_close(&run);
        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);
        turbo_cond_destroy(&state.changed);
        turbo_mutex_destroy(&state.lock);
    }

    it("allows a source destroy callback to close the same run") {
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_scheduler scheduler = {0};
        cflow_source source = {0};
        cflow_run run = {0};
        destroy_reentrant_close_state state = {&run, false};

        normalized.root = CMETA_INVALID_ID;
        cflow_graph_init(&surface, &cmeta_type_int);
        check_true(cflow_graph_normalize(&normalized, &surface));
        check_true(cflow_scheduler_test_init(&scheduler));
        check_true(cflow_source_from_readiness(
            &source,
            "destroy_reentrant",
            &cmeta_type_int,
            destroy_reentrant_read,
            destroy_reentrant_arm,
            NULL,
            destroy_reentrant_close,
            &state));
        check_true(cflow_run_open(
            &run, &normalized, &source, &scheduler, NULL));

        cflow_run_close(&run);

        check_true(state.close_returned);
        check_null(run.impl);
        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);
    }
}
