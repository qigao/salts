#include <cflow/subrun.h>
#include <cflow/sources.h>
#include <turbo/thread.h>

#include "value_storage.h"

#include <stdlib.h>
#include <string.h>

typedef struct subrun_state {
    const cflow_graph *graph;
    cflow_subgraph_id subgraph;
    unsigned char *input;
    unsigned char *value;
    cflow_run run;
    cflow_scheduler *scheduler;

    turbo_mutex_t lock;
    cflow_waker waiter;
    bool started;
    bool requested;
    bool value_ready;
    bool done;
    bool cancelled;
    const char *error;
} subrun_state;

static bool subrun_on_value(void *user,
                           const cmeta_type_desc *type,
                           const void *value) {
    subrun_state *s = (subrun_state *)user;
    if (!s || !value ||
        !cmeta_type_equal(type, cflow_subgraph_output_type(s->graph, s->subgraph))) return false;
    turbo_mutex_lock(&s->lock);
    if (s->cancelled || s->value_ready) {
        turbo_mutex_unlock(&s->lock);
        return false;
    }
    memcpy(s->value, value, type->size);
    s->value_ready = true;
    s->requested = false;
    cflow_waker w = s->waiter;
    s->waiter = (cflow_waker){0};
    turbo_mutex_unlock(&s->lock);
    if (w.wake) w.wake(w.user);
    return true;
}

static void subrun_on_error(void *user, const char *message) {
    subrun_state *s = (subrun_state *)user;
    if (!s) return;
    turbo_mutex_lock(&s->lock);
    if (!s->cancelled) s->error = message ? message : "subgraph error";
    s->requested = false;
    cflow_waker w = s->waiter;
    s->waiter = (cflow_waker){0};
    turbo_mutex_unlock(&s->lock);
    if (w.wake) w.wake(w.user);
}

static void subrun_on_done(void *user) {
    subrun_state *s = (subrun_state *)user;
    if (!s) return;
    turbo_mutex_lock(&s->lock);
    s->done = true;
    s->requested = false;
    cflow_waker w = s->waiter;
    s->waiter = (cflow_waker){0};
    turbo_mutex_unlock(&s->lock);
    if (w.wake) w.wake(w.user);
}


CMETA_IMPLEMENTS(cflow_sink, subrun_sink, 0,
    .value = subrun_on_value,
    .error = subrun_on_error,
    .done = subrun_on_done
);
static bool subrun_start(subrun_state *s, cflow_resume_ctx *ctx) {
    if (!s || !ctx || !ctx->scheduler) return false;
    if (s->started) return s->scheduler == ctx->scheduler;

    cflow_source source = {0};
    cflow_sink sink = subrun_sink_as_cflow_sink(s);
    if (!cflow_source_from_array(&source,
                                 cflow_subgraph_source_type(s->graph, s->subgraph),
                                 s->input,
                                 1)) return false;
    if (!cflow_run_open_subgraph(&s->run, s->graph, s->subgraph, &source, ctx->scheduler, &sink)) {
        cflow_source_destroy(&source);
        return false;
    }
    s->scheduler = ctx->scheduler;
    s->started = true;
    return true;
}

static bool subrun_wait_arm(void *state, cflow_waker w) {
    subrun_state *s = (subrun_state *)state;
    if (!s) return false;
    turbo_mutex_lock(&s->lock);
    bool ready = s->value_ready || s->done || s->error || s->cancelled;
    if (!ready) s->waiter = w;
    turbo_mutex_unlock(&s->lock);
    if (ready && w.wake) w.wake(w.user);
    return true;
}

static void subrun_wait_cancel(void *state) {
    subrun_state *s = (subrun_state *)state;
    if (!s) return;
    bool started = false;
    turbo_mutex_lock(&s->lock);
    s->waiter = (cflow_waker){0};
    started = s->started;
    turbo_mutex_unlock(&s->lock);
    if (started) cflow_run_cancel(&s->run);
}

CMETA_IMPLEMENTS(cflow_waitable, subrun_waitable, 0,
    .arm = subrun_wait_arm,
    .cancel = subrun_wait_cancel
);

static cflow_step subrun_resume(void *state,
                               cflow_resume_ctx *ctx,
                               void *out_value) {
    subrun_state *s = (subrun_state *)state;
    if (!s || !out_value) return (cflow_step){ CFLOW_STEP_ERROR, {0}, "subrun state is invalid" };
    if (!subrun_start(s, ctx))
        return (cflow_step){ CFLOW_STEP_ERROR, {0}, "subgraph could not start" };

    bool need_request = false;
    turbo_mutex_lock(&s->lock);

    if (s->cancelled) {
        turbo_mutex_unlock(&s->lock);
        return (cflow_step){ CFLOW_STEP_DONE, {0}, NULL };
    }
    if (s->error) {
        const char *err = s->error;
        turbo_mutex_unlock(&s->lock);
        return (cflow_step){ CFLOW_STEP_ERROR, {0}, err };
    }
    if (s->value_ready) {
        const cmeta_type_desc *type = cflow_subgraph_output_type(s->graph, s->subgraph);
        memcpy(out_value, s->value, type->size);
        s->value_ready = false;
        bool done = s->done;
        turbo_mutex_unlock(&s->lock);
        return (cflow_step){ done ? CFLOW_STEP_VALUE_AND_DONE : CFLOW_STEP_VALUE, {0}, NULL };
    }
    if (s->done) {
        turbo_mutex_unlock(&s->lock);
        return (cflow_step){ CFLOW_STEP_DONE, {0}, NULL };
    }
    if (!s->requested) {
        s->requested = true;
        need_request = true;
    }
    turbo_mutex_unlock(&s->lock);

    if (need_request && !cflow_run_request(&s->run, 1)) {
        turbo_mutex_lock(&s->lock);
        s->requested = false;
        turbo_mutex_unlock(&s->lock);
        return (cflow_step){ CFLOW_STEP_ERROR, {0}, "subgraph request failed" };
    }

    /* A concurrent scheduler may have completed between request() and here. */
    turbo_mutex_lock(&s->lock);
    bool ready = s->value_ready || s->done || s->error || s->cancelled;
    turbo_mutex_unlock(&s->lock);
    if (ready) return subrun_resume(state, ctx, out_value);
    return (cflow_step){ CFLOW_STEP_WAIT, subrun_waitable_as_cflow_waitable(s), NULL };
}

static void subrun_cancel(void *state) {
    subrun_state *s = (subrun_state *)state;
    if (!s) return;
    turbo_mutex_lock(&s->lock);
    s->cancelled = true;
    s->waiter = (cflow_waker){0};
    turbo_mutex_unlock(&s->lock);
    if (s->started) cflow_run_cancel(&s->run);
}

static void subrun_destroy(void *state) {
    subrun_state *s = (subrun_state *)state;
    if (!s) return;
    subrun_cancel(s);
    if (s->started) cflow_run_close(&s->run);
    turbo_mutex_destroy(&s->lock);
    free(s->value);
    free(s->input);
    free(s);
}

static const cflow_resumable_ops subrun_ops = {
    subrun_resume,
    subrun_cancel,
    subrun_destroy
};

bool cflow_resumable_from_subgraph(cflow_resumable *out,
                                    const cflow_graph *graph,
                                    cflow_subgraph_id subgraph,
                                    const void *source_value) {
    if (!out || !graph || !cflow_graph_subgraph(graph, subgraph) ||
        !source_value || !cflow_value_storage_graph_supported(graph))
        return false;
    subrun_state *s = calloc(1, sizeof(*s));
    if (!s) return false;
    turbo_mutex_init(&s->lock);
    if (!s->lock) {
        free(s);
        return false;
    }
    const cmeta_type_desc *in = cflow_subgraph_source_type(graph, subgraph);
    const cmeta_type_desc *out_type = cflow_subgraph_output_type(graph, subgraph);
    s->input = malloc(in->size ? in->size : 1);
    s->value = malloc(out_type->size ? out_type->size : 1);
    if (!s->input || !s->value) {
        free(s->value); free(s->input); turbo_mutex_destroy(&s->lock); free(s);
        return false;
    }
    memcpy(s->input, source_value, in->size);
    s->graph = graph;
    s->subgraph = subgraph;
    *out = (cflow_resumable){ "subrun", out_type, &subrun_ops, s };
    return true;
}
