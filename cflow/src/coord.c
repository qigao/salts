#include <cflow/coord.h>

#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

const cmeta_type_desc cflow_type_coord_event = {
    .name = "cflow_coord_event",
    .size = sizeof(cflow_coord_event),
    .align = _Alignof(cflow_coord_event),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .identity = NULL
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

    mtx_t lock;
    cflow_waker waiter;
    atomic_bool wake_posted;
};

static bool coord_init(cflow_resumable *out,
                       cflow_coord_mode mode,
                       const cflow_resumable *children,
                       size_t count,
                       size_t value_size);

static void coord_child_wake(void *context);
static cflow_step coord_resume(void *state, void *out);
static void coord_cancel(void *state);
static void coord_destroy(void *state);

static const cflow_resumable_ops coord_ops = {
    coord_resume,
    coord_cancel,
    coord_destroy
};

static void coord_child_wake(void *context) {
    coord_child *child = (coord_child *)context;
    coord_state *state;
    cflow_waker waiter = {0};

    if (!child || !(state = child->owner)) return;

    mtx_lock(&state->lock);
    atomic_store_explicit(&child->waiting, false, memory_order_release);
    if (state->waiter.wake &&
        !atomic_exchange_explicit(&state->wake_posted, true, memory_order_acq_rel))
        waiter = state->waiter;
    mtx_unlock(&state->lock);

    cflow_waker_wake(waiter);
}

static bool coord_child_wait(coord_child *child) {
    cflow_waker waker;
    if (!child || !child->waitable.ops) return false;
    waker.wake = coord_child_wake;
    waker.context = child;
    atomic_store_explicit(&child->waiting, true, memory_order_release);
    if (!cflow_waitable_arm(child->waitable, waker)) {
        atomic_store_explicit(&child->waiting, false, memory_order_release);
        return false;
    }
    atomic_store_explicit(&child->armed, true, memory_order_release);
    return true;
}

static void coord_child_disarm(coord_child *child) {
    if (!child) return;
    if (atomic_exchange_explicit(&child->armed, false, memory_order_acq_rel))
        cflow_waitable_cancel(child->waitable);
    atomic_store_explicit(&child->waiting, false, memory_order_release);
}

static void coord_cancel_others(coord_state *state, size_t keep) {
    size_t i;
    for (i = 0; i < state->count; ++i) {
        if (i == keep || state->children[i].done) continue;
        coord_child_disarm(&state->children[i]);
        cflow_resumable_cancel(&state->children[i].machine);
        state->children[i].done = true;
    }
}

static bool coord_all_done(const coord_state *state) {
    size_t i;
    for (i = 0; i < state->count; ++i)
        if (!state->children[i].done) return false;
    return true;
}

static cflow_step coord_step_child(coord_state *state, size_t index) {
    coord_child *child = &state->children[index];
    cflow_step step;

    if (child->done) return cflow_step_done();
    coord_child_disarm(child);
    step = cflow_resumable_resume(&child->machine, child->value);
    switch (step.kind) {
        case CFLOW_STEP_VALUE:
            child->has_value = true;
            return step;
        case CFLOW_STEP_VALUE_AND_DONE:
            child->has_value = true;
            child->done = true;
            return step;
        case CFLOW_STEP_WAIT:
            child->waitable = step.waitable;
            if (!coord_child_wait(child)) {
                state->error = "coordination wait arm failed";
                child->done = true;
                return cflow_step_error(state->error);
            }
            return step;
        case CFLOW_STEP_DONE:
            child->done = true;
            return step;
        case CFLOW_STEP_ERROR:
            child->done = true;
            state->error = step.error;
            return step;
    }
    state->error = "invalid coordination step";
    child->done = true;
    return cflow_step_error(state->error);
}

static cflow_step coord_resume_any(coord_state *state, void *out) {
    size_t i;
    bool waiting = false;

    for (i = 0; i < state->count; ++i) {
        cflow_step step;
        if (state->children[i].done) continue;
        step = coord_step_child(state, i);
        if (step.kind == CFLOW_STEP_VALUE || step.kind == CFLOW_STEP_VALUE_AND_DONE) {
            if (out) memcpy(out, state->children[i].value, step.value_size);
            coord_cancel_others(state, i);
            return cflow_step_value_and_done(step.value_size);
        }
        if (step.kind == CFLOW_STEP_ERROR) return step;
        if (step.kind == CFLOW_STEP_WAIT) waiting = true;
    }

    if (waiting) return cflow_step_wait((cflow_waitable){0});
    return cflow_step_done();
}

static cflow_step coord_resume_all(coord_state *state, void *out, bool require_done) {
    size_t i;
    bool waiting = false;
    bool progressed = false;

    for (i = 0; i < state->count; ++i) {
        cflow_step step;
        if (state->children[i].done) continue;
        step = coord_step_child(state, i);
        if (step.kind == CFLOW_STEP_ERROR) return step;
        if (step.kind == CFLOW_STEP_WAIT) waiting = true;
        if (step.kind == CFLOW_STEP_VALUE || step.kind == CFLOW_STEP_VALUE_AND_DONE)
            progressed = true;
    }

    if (coord_all_done(state)) {
        if (out && state->count > 0 && state->children[state->count - 1].has_value)
            memcpy(out, state->children[state->count - 1].value,
                   state->children[state->count - 1].machine.value_size);
        return require_done ? cflow_step_done() :
            cflow_step_value_and_done(state->count > 0 ?
                state->children[state->count - 1].machine.value_size : 0u);
    }
    if (progressed && !require_done) return cflow_step_value(0u);
    if (waiting) return cflow_step_wait((cflow_waitable){0});
    return cflow_step_done();
}

static cflow_step coord_resume_sequence(coord_state *state, void *out) {
    while (state->sequence_index < state->count) {
        coord_child *child = &state->children[state->sequence_index];
        cflow_step step = coord_step_child(state, state->sequence_index);
        if (step.kind == CFLOW_STEP_ERROR || step.kind == CFLOW_STEP_WAIT)
            return step;
        if (step.kind == CFLOW_STEP_VALUE) {
            if (out) memcpy(out, child->value, step.value_size);
            return step;
        }
        if (step.kind == CFLOW_STEP_VALUE_AND_DONE) {
            if (out) memcpy(out, child->value, step.value_size);
            ++state->sequence_index;
            return state->sequence_index == state->count ?
                cflow_step_value_and_done(step.value_size) : cflow_step_value(step.value_size);
        }
        if (step.kind == CFLOW_STEP_DONE) ++state->sequence_index;
    }
    return cflow_step_done();
}

static cflow_step coord_resume_latest(coord_state *state, void *out) {
    size_t i;
    bool waiting = false;
    bool have = true;
    bool changed = false;

    for (i = 0; i < state->count; ++i) {
        cflow_step step;
        if (state->children[i].done) continue;
        step = coord_step_child(state, i);
        if (step.kind == CFLOW_STEP_ERROR) return step;
        if (step.kind == CFLOW_STEP_WAIT) waiting = true;
        if (step.kind == CFLOW_STEP_VALUE || step.kind == CFLOW_STEP_VALUE_AND_DONE)
            changed = true;
    }
    for (i = 0; i < state->count; ++i)
        if (!state->children[i].has_value) have = false;

    if (have && changed) {
        if (out && state->count > 0)
            memcpy(out, state->children[state->count - 1].value,
                   state->children[state->count - 1].machine.value_size);
        return coord_all_done(state) ?
            cflow_step_value_and_done(state->count > 0 ?
                state->children[state->count - 1].machine.value_size : 0u) :
            cflow_step_value(state->count > 0 ?
                state->children[state->count - 1].machine.value_size : 0u);
    }
    if (coord_all_done(state)) return cflow_step_done();
    if (waiting) return cflow_step_wait((cflow_waitable){0});
    return cflow_step_done();
}

static cflow_step coord_resume(void *opaque, void *out) {
    coord_state *state = (coord_state *)opaque;
    cflow_step result;
    if (!state || state->cancelled) return cflow_step_done();

    mtx_lock(&state->lock);
    atomic_store_explicit(&state->wake_posted, false, memory_order_release);
    mtx_unlock(&state->lock);

    switch (state->mode) {
        case CFLOW_COORD_ALL:
            result = coord_resume_all(state, out, false);
            break;
        case CFLOW_COORD_ALL_DONE:
            result = coord_resume_all(state, out, true);
            break;
        case CFLOW_COORD_ANY:
            result = coord_resume_any(state, out);
            break;
        case CFLOW_COORD_LATEST:
            result = coord_resume_latest(state, out);
            break;
        case CFLOW_COORD_SEQUENCE:
            result = coord_resume_sequence(state, out);
            break;
        default:
            result = cflow_step_error("unknown coordination mode");
            break;
    }

    if (result.kind == CFLOW_STEP_WAIT) {
        mtx_lock(&state->lock);
        state->waiter = result.waitable.ops ? state->waiter : state->waiter;
        mtx_unlock(&state->lock);
    }
    return result;
}

static void coord_cancel(void *opaque) {
    coord_state *state = (coord_state *)opaque;
    size_t i;
    if (!state) return;
    state->cancelled = true;
    for (i = 0; i < state->count; ++i) {
        coord_child_disarm(&state->children[i]);
        cflow_resumable_cancel(&state->children[i].machine);
    }
}

static void coord_destroy(void *opaque) {
    coord_state *state = (coord_state *)opaque;
    size_t i;
    if (!state) return;
    for (i = 0; i < state->count; ++i) {
        coord_child_disarm(&state->children[i]);
        cflow_resumable_destroy(&state->children[i].machine);
        free(state->children[i].value);
    }
    free(state->children);
    mtx_destroy(&state->lock);
    free(state);
}

static bool coord_init(cflow_resumable *out,
                       cflow_coord_mode mode,
                       const cflow_resumable *children,
                       size_t count,
                       size_t value_size) {
    coord_state *state;
    size_t i;
    if (!out || (count && !children)) return false;
    state = (coord_state *)calloc(1, sizeof(*state));
    if (!state) return false;
    state->mode = mode;
    state->count = count;
    if (mtx_init(&state->lock, mtx_plain) != thrd_success) {
        free(state);
        return false;
    }
    if (count) {
        state->children = (coord_child *)calloc(count, sizeof(*state->children));
        if (!state->children) {
            mtx_destroy(&state->lock);
            free(state);
            return false;
        }
    }
    for (i = 0; i < count; ++i) {
        state->children[i].owner = state;
        state->children[i].index = i;
        state->children[i].machine = children[i];
        state->children[i].value = value_size ? (unsigned char *)malloc(value_size) : NULL;
        if (value_size && !state->children[i].value) {
            coord_destroy(state);
            return false;
        }
        atomic_init(&state->children[i].waiting, false);
        atomic_init(&state->children[i].armed, false);
    }
    atomic_init(&state->wake_posted, false);
    out->ops = &coord_ops;
    out->state = state;
    out->value_size = value_size;
    return true;
}

bool cflow_coord_all(cflow_resumable *out,
                     const cflow_resumable *children,
                     size_t count,
                     size_t value_size) {
    return coord_init(out, CFLOW_COORD_ALL, children, count, value_size);
}

bool cflow_coord_all_done(cflow_resumable *out,
                          const cflow_resumable *children,
                          size_t count,
                          size_t value_size) {
    return coord_init(out, CFLOW_COORD_ALL_DONE, children, count, value_size);
}

bool cflow_coord_any(cflow_resumable *out,
                     const cflow_resumable *children,
                     size_t count,
                     size_t value_size) {
    return coord_init(out, CFLOW_COORD_ANY, children, count, value_size);
}

bool cflow_coord_latest(cflow_resumable *out,
                        const cflow_resumable *children,
                        size_t count,
                        size_t value_size) {
    return coord_init(out, CFLOW_COORD_LATEST, children, count, value_size);
}

bool cflow_coord_sequence(cflow_resumable *out,
                          const cflow_resumable *children,
                          size_t count,
                          size_t value_size) {
    return coord_init(out, CFLOW_COORD_SEQUENCE, children, count, value_size);
}
