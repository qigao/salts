#include <cflow/coord.h>
#include <turbo/thread.h>

#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

const cmeta_type_desc cflow_type_coord_event = {
    "cflow_coord_event", sizeof(cflow_coord_event), _Alignof(cflow_coord_event),
    CMETA_T_OBJECT, NULL
};

typedef struct coord_state coord_state;

typedef struct coord_child {
    coord_state *owner;
    size_t index;
    cflow_resumable machine;
    unsigned char *value;
    bool has_value;
    bool done;
    atomic_bool waiting;
    atomic_bool armed;
    cflow_waitable waitable;
} coord_child;

struct coord_state {
    cflow_coord_mode mode;
    coord_child *children;
    size_t count;
    size_t cursor;
    size_t sequence_index;
    size_t generation;
    bool emitted_all;
    bool cancelled;
    const char *error;

    turbo_mutex_t lock;
    cflow_waker waiter;
};

typedef struct value_state {
    const cmeta_type_desc *type;
    unsigned char *value;
    bool emitted;
} value_state;

static cflow_step value_resume(void *state, cflow_resume_ctx *ctx, void *out) {
    (void)ctx;
    value_state *s = (value_state *)state;
    if (!s || !out) return (cflow_step){ CFLOW_STEP_ERROR, {0}, "value machine is invalid" };
    if (s->emitted) return (cflow_step){ CFLOW_STEP_DONE, {0}, NULL };
    memcpy(out, s->value, s->type->size);
    s->emitted = true;
    return (cflow_step){ CFLOW_STEP_VALUE_AND_DONE, {0}, NULL };
}

static void value_destroy(void *state) {
    value_state *s = (value_state *)state;
    if (!s) return;
    free(s->value);
    free(s);
}

static const cflow_resumable_ops value_ops = { value_resume, NULL, value_destroy };

bool cflow_resumable_from_value(cflow_resumable *out,
                                 const cmeta_type_desc *type,
                                 const void *value) {
    if (!out || !type || !value || !type->size) return false;
    value_state *s = calloc(1, sizeof(*s));
    if (!s) return false;
    s->value = malloc(type->size);
    if (!s->value) { free(s); return false; }
    memcpy(s->value, value, type->size);
    s->type = type;
    *out = (cflow_resumable){ "value", type, &value_ops, s };
    return true;
}

static bool all_have_value(const coord_state *s);

static bool coord_child_runnable(const coord_state *s, size_t i) {
    const coord_child *c = &s->children[i];
    if (c->done || atomic_load(&c->waiting)) return false;
    switch (s->mode) {
        case CFLOW_COORD_ALL:
            return !c->has_value;
        case CFLOW_COORD_ALL_DONE:
            return true;
        case CFLOW_COORD_ANY:
            return true;
        case CFLOW_COORD_LATEST:
            /* Before combineLatest has an initial value from every child,
             * do not drain a fast already-initialized child while another
             * child is WAITing. This preserves scheduler fairness. */
            return all_have_value(s) ? true : !c->has_value;
        case CFLOW_COORD_SEQUENCE:
            return i == s->sequence_index && !c->has_value;
    }
    return false;
}

static void coord_child_wake(void *user) {
    coord_child *c = (coord_child *)user;
    if (!c || !c->owner) return;
    coord_state *s = c->owner;
    cflow_waker parent = {0};
    turbo_mutex_lock(&s->lock);
    atomic_store(&c->waiting, false);
    atomic_store(&c->armed, false);
    parent = s->waiter;
    s->waiter = (cflow_waker){0};
    turbo_mutex_unlock(&s->lock);
    if (parent.wake) parent.wake(parent.user);
}

static bool coord_wait_arm(void *state, cflow_waker waker) {
    coord_state *s = (coord_state *)state;
    if (!s) return false;

    size_t *to_arm = calloc(s->count ? s->count : 1, sizeof(*to_arm));
    if (!to_arm) return false;
    size_t arm_count = 0;
    bool ready = false;

    turbo_mutex_lock(&s->lock);
    if (s->cancelled || s->error) ready = true;
    else {
        s->waiter = waker;
        for (size_t i = 0; i < s->count; ++i) {
            coord_child *c = &s->children[i];
            if (atomic_load(&c->waiting) && !atomic_load(&c->armed)) {
                atomic_store(&c->armed, true);
                to_arm[arm_count++] = i;
            } else if (coord_child_runnable(s, i)) {
                ready = true;
            }
        }
    }
    if (ready) s->waiter = (cflow_waker){0};
    turbo_mutex_unlock(&s->lock);

    bool ok = true;
    for (size_t n = 0; n < arm_count; ++n) {
        coord_child *c = &s->children[to_arm[n]];
        cflow_waker cw = { coord_child_wake, c };
        if (!cflow_waitable_valid(&c->waitable) ||
            !cflow_waitable_arm(&c->waitable, cw)) {
            ok = false;
            turbo_mutex_lock(&s->lock);
            atomic_store(&c->armed, false);
            atomic_store(&c->waiting, false);
            s->error = "coordination child waitable could not be armed";
            s->waiter = (cflow_waker){0};
            turbo_mutex_unlock(&s->lock);
            break;
        }
    }
    free(to_arm);
    if (ready && waker.wake) waker.wake(waker.user);
    return ok;
}

static void coord_wait_cancel(void *state) {
    coord_state *s = (coord_state *)state;
    if (!s) return;
    turbo_mutex_lock(&s->lock);
    s->waiter = (cflow_waker){0};
    for (size_t i = 0; i < s->count; ++i) {
        coord_child *c = &s->children[i];
        if (atomic_load(&c->armed) && cflow_waitable_valid(&c->waitable)) {
            cflow_waitable w = c->waitable;
            atomic_store(&c->armed, false);
            turbo_mutex_unlock(&s->lock);
            cflow_waitable_cancel(&w);
            turbo_mutex_lock(&s->lock);
        }
    }
    turbo_mutex_unlock(&s->lock);
}

CMETA_IMPLEMENTS(cflow_waitable, coord_waitable, 0,
    .arm = coord_wait_arm,
    .cancel = coord_wait_cancel
);

static bool all_have_value(const coord_state *s) {
    for (size_t i = 0; i < s->count; ++i) if (!s->children[i].has_value) return false;
    return true;
}

static bool all_done(const coord_state *s) {
    for (size_t i = 0; i < s->count; ++i) if (!s->children[i].done) return false;
    return true;
}

static bool any_runnable(const coord_state *s) {
    for (size_t i = 0; i < s->count; ++i)
        if (coord_child_runnable(s, i)) return true;
    return false;
}

static void cancel_other_children(coord_state *s, size_t winner) {
    for (size_t i = 0; i < s->count; ++i) {
        if (i == winner) continue;
        coord_child *c = &s->children[i];
        if (!c->done && c->machine.ops && c->machine.ops->cancel)
            c->machine.ops->cancel(c->machine.state);
        c->done = true;
    }
}

static cflow_step emit_event(coord_state *s, size_t index, bool done, void *out) {
    cflow_coord_event event = { index, ++s->generation };
    memcpy(out, &event, sizeof(event));
    return (cflow_step){ done ? CFLOW_STEP_VALUE_AND_DONE : CFLOW_STEP_VALUE, {0}, NULL };
}

static cflow_step resume_child(coord_state *s,
                               size_t idx,
                               cflow_resume_ctx *ctx,
                               bool *produced) {
    coord_child *c = &s->children[idx];
    *produced = false;
    if (c->done || atomic_load(&c->waiting))
        return (cflow_step){ CFLOW_STEP_DONE, {0}, NULL };

    cflow_step step = c->machine.ops->resume(c->machine.state, ctx, c->value);
    switch (step.kind) {
        case CFLOW_STEP_VALUE:
            c->has_value = true;
            *produced = true;
            return step;
        case CFLOW_STEP_VALUE_AND_DONE:
            c->has_value = true;
            c->done = true;
            *produced = true;
            return step;
        case CFLOW_STEP_WAIT:
            atomic_store(&c->waiting, true);
            c->waitable = step.waitable;
            return step;
        case CFLOW_STEP_DONE:
            c->done = true;
            return step;
        case CFLOW_STEP_ERROR:
            s->error = step.error ? step.error : "coordination child failed";
            return step;
    }
    s->error = "coordination child returned invalid step";
    return (cflow_step){ CFLOW_STEP_ERROR, {0}, s->error };
}

static cflow_step coord_resume_all(coord_state *s, cflow_resume_ctx *ctx, void *out) {
    if (s->emitted_all) return (cflow_step){ CFLOW_STEP_DONE, {0}, NULL };
    for (;;) {
        if (all_have_value(s)) {
            /* ALL is a one-shot barrier: once every child produced one value,
             * the coordinator is complete and any longer-lived child is cancelled. */
            for (size_t i = 0; i < s->count; ++i) {
                coord_child *c = &s->children[i];
                if (!c->done && c->machine.ops && c->machine.ops->cancel)
                    c->machine.ops->cancel(c->machine.state);
                c->done = true;
            }
            s->emitted_all = true;
            return emit_event(s, SIZE_MAX, true, out);
        }
        bool progressed = false;
        size_t start = s->cursor;
        for (size_t n = 0; n < s->count; ++n) {
            size_t i = (start + n) % s->count;
            coord_child *c = &s->children[i];
            if (c->done || atomic_load(&c->waiting) || c->has_value) continue;
            bool produced = false;
            cflow_step step = resume_child(s, i, ctx, &produced);
            s->cursor = (i + 1) % s->count;
            if (step.kind == CFLOW_STEP_ERROR) return step;
            if (c->done && !c->has_value)
                return (cflow_step){ CFLOW_STEP_ERROR, {0}, "ALL child completed without a value" };
            if (produced || step.kind == CFLOW_STEP_DONE) progressed = true;
            if (all_have_value(s)) break;
        }
        if (all_have_value(s)) continue;
        if (!progressed && !any_runnable(s))
            return (cflow_step){ CFLOW_STEP_WAIT, coord_waitable_as_cflow_waitable(s), NULL };
    }
}


static cflow_step coord_resume_all_done(coord_state *s, cflow_resume_ctx *ctx, void *out) {
    if (s->emitted_all) return (cflow_step){ CFLOW_STEP_DONE, {0}, NULL };
    for (;;) {
        if (all_done(s)) {
            if (!all_have_value(s))
                return (cflow_step){ CFLOW_STEP_ERROR, {0}, "ALL_DONE child completed without a value" };
            s->emitted_all = true;
            return emit_event(s, SIZE_MAX, true, out);
        }
        bool progressed = false;
        size_t start = s->cursor;
        for (size_t n = 0; n < s->count; ++n) {
            size_t i = (start + n) % s->count;
            coord_child *c = &s->children[i];
            if (c->done || atomic_load(&c->waiting)) continue;
            bool produced = false;
            cflow_step step = resume_child(s, i, ctx, &produced);
            s->cursor = (i + 1) % s->count;
            if (step.kind == CFLOW_STEP_ERROR) return step;
            if (produced || step.kind == CFLOW_STEP_DONE) progressed = true;
        }
        if (all_done(s)) continue;
        if (!progressed && !any_runnable(s))
            return (cflow_step){ CFLOW_STEP_WAIT, coord_waitable_as_cflow_waitable(s), NULL };
    }
}

static cflow_step coord_resume_any(coord_state *s, cflow_resume_ctx *ctx, void *out) {
    for (;;) {
        size_t start = s->cursor;
        for (size_t n = 0; n < s->count; ++n) {
            size_t i = (start + n) % s->count;
            coord_child *c = &s->children[i];
            if (c->done || atomic_load(&c->waiting)) continue;
            bool produced = false;
            cflow_step step = resume_child(s, i, ctx, &produced);
            s->cursor = (i + 1) % s->count;
            if (step.kind == CFLOW_STEP_ERROR) return step;
            if (produced) {
                cancel_other_children(s, i);
                return emit_event(s, i, true, out);
            }
        }
        if (all_done(s)) return (cflow_step){ CFLOW_STEP_DONE, {0}, NULL };
        if (!any_runnable(s)) return (cflow_step){ CFLOW_STEP_WAIT, coord_waitable_as_cflow_waitable(s), NULL };
    }
}

static cflow_step coord_resume_sequence(coord_state *s, cflow_resume_ctx *ctx, void *out) {
    while (s->sequence_index < s->count) {
        size_t i = s->sequence_index;
        coord_child *c = &s->children[i];
        if (atomic_load(&c->waiting)) return (cflow_step){ CFLOW_STEP_WAIT, coord_waitable_as_cflow_waitable(s), NULL };
        if (c->done && !c->has_value) { ++s->sequence_index; continue; }
        if (!c->has_value) {
            bool produced = false;
            cflow_step step = resume_child(s, i, ctx, &produced);
            if (step.kind == CFLOW_STEP_ERROR) return step;
            if (step.kind == CFLOW_STEP_WAIT) return (cflow_step){ CFLOW_STEP_WAIT, coord_waitable_as_cflow_waitable(s), NULL };
            if (!produced) { if (c->done) ++s->sequence_index; continue; }
        }
        bool final = (i + 1 == s->count);
        if (!c->done && c->machine.ops && c->machine.ops->cancel)
            c->machine.ops->cancel(c->machine.state);
        c->done = true;
        ++s->sequence_index;
        return emit_event(s, i, final, out);
    }
    return (cflow_step){ CFLOW_STEP_DONE, {0}, NULL };
}

static cflow_step coord_resume_latest(coord_state *s, cflow_resume_ctx *ctx, void *out) {
    for (;;) {
        if (all_done(s)) return (cflow_step){ CFLOW_STEP_DONE, {0}, NULL };
        bool progressed = false;
        size_t start = s->cursor;
        for (size_t n = 0; n < s->count; ++n) {
            size_t i = (start + n) % s->count;
            if (!coord_child_runnable(s, i)) continue;
            bool produced = false;
            cflow_step step = resume_child(s, i, ctx, &produced);
            s->cursor = (i + 1) % s->count;
            if (step.kind == CFLOW_STEP_ERROR) return step;
            if (produced) {
                progressed = true;
                if (all_have_value(s)) return emit_event(s, i, all_done(s), out);
            }
        }
        /* If a child terminates before ever producing, combineLatest can never
         * become ready. */
        for (size_t i = 0; i < s->count; ++i)
            if (s->children[i].done && !s->children[i].has_value)
                return (cflow_step){ CFLOW_STEP_DONE, {0}, NULL };
        if (!progressed && !any_runnable(s))
            return (cflow_step){ CFLOW_STEP_WAIT, coord_waitable_as_cflow_waitable(s), NULL };
    }
}

static cflow_step coord_resume(void *state, cflow_resume_ctx *ctx, void *out) {
    coord_state *s = (coord_state *)state;
    if (!s || !ctx || !out) return (cflow_step){ CFLOW_STEP_ERROR, {0}, "coordination state is invalid" };
    if (s->cancelled) return (cflow_step){ CFLOW_STEP_DONE, {0}, NULL };
    if (s->error) return (cflow_step){ CFLOW_STEP_ERROR, {0}, s->error };
    switch (s->mode) {
        case CFLOW_COORD_ALL: return coord_resume_all(s, ctx, out);
        case CFLOW_COORD_ALL_DONE: return coord_resume_all_done(s, ctx, out);
        case CFLOW_COORD_ANY: return coord_resume_any(s, ctx, out);
        case CFLOW_COORD_LATEST: return coord_resume_latest(s, ctx, out);
        case CFLOW_COORD_SEQUENCE: return coord_resume_sequence(s, ctx, out);
    }
    return (cflow_step){ CFLOW_STEP_ERROR, {0}, "invalid coordination mode" };
}

static void coord_cancel(void *state) {
    coord_state *s = (coord_state *)state;
    if (!s) return;
    s->cancelled = true;
    coord_wait_cancel(s);
    for (size_t i = 0; i < s->count; ++i) {
        coord_child *c = &s->children[i];
        if (!c->done && c->machine.ops && c->machine.ops->cancel)
            c->machine.ops->cancel(c->machine.state);
        c->done = true;
    }
}

static void coord_destroy(void *state) {
    coord_state *s = (coord_state *)state;
    if (!s) return;
    coord_cancel(s);
    for (size_t i = 0; i < s->count; ++i) {
        coord_child *c = &s->children[i];
        if (c->machine.ops && c->machine.ops->destroy)
            c->machine.ops->destroy(c->machine.state);
        free(c->value);
    }
    turbo_mutex_destroy(&s->lock);
    free(s->children);
    free(s);
}

static const cflow_resumable_ops coord_ops = { coord_resume, coord_cancel, coord_destroy };

bool cflow_resumable_from_coordination(cflow_resumable *out,
                                        cflow_coord_mode mode,
                                        cflow_resumable *children,
                                        size_t count) {
    if (!out || !children || count == 0) return false;
    coord_state *s = calloc(1, sizeof(*s));
    if (!s) return false;
    turbo_mutex_init(&s->lock);
    if (!s->lock) { free(s); return false; }
    s->children = calloc(count, sizeof(*s->children));
    if (!s->children) { turbo_mutex_destroy(&s->lock); free(s); return false; }
    s->mode = mode;
    s->count = count;
    for (size_t i = 0; i < count; ++i) {
        if (!children[i].ops || !children[i].ops->resume || !children[i].output_type) {
            coord_destroy(s);
            return false;
        }
        s->children[i].owner = s;
        s->children[i].index = i;
        atomic_init(&s->children[i].waiting, false);
        atomic_init(&s->children[i].armed, false);
        s->children[i].machine = children[i];
        s->children[i].value = malloc(children[i].output_type->size ? children[i].output_type->size : 1);
        if (!s->children[i].value) {
            /* Only machines already moved into s are destroyed here. */
            for (size_t j = i + 1; j < count; ++j) memset(&s->children[j].machine, 0, sizeof(s->children[j].machine));
            coord_destroy(s);
            return false;
        }
        memset(&children[i], 0, sizeof(children[i]));
    }
    *out = (cflow_resumable){ "coordination", &cflow_type_coord_event, &coord_ops, s };
    return true;
}

bool cflow_coord_value(const cflow_resumable *coord,
                       size_t child_index,
                       const cmeta_type_desc **type,
                       const void **value) {
    if (!coord || coord->ops != &coord_ops || !coord->state) return false;
    coord_state *s = (coord_state *)coord->state;
    if (child_index >= s->count || !s->children[child_index].has_value) return false;
    if (type) *type = s->children[child_index].machine.output_type;
    if (value) *value = s->children[child_index].value;
    return true;
}
