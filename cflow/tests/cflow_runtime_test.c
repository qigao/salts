#include <cflow/cflow.h>
#include <turbo/thread.h>
#include "tinytest.h"

#include <stdint.h>

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

suite("CFlow runtime") {
    it("rejects channel storage size overflow") {
        const cmeta_type_desc three_byte_type = {
            .name = "three_byte",
            .size = 3u,
            .align = 1u,
            .kind = CMETA_T_OBJECT,
            .pointee = NULL,
            .traits = NULL,
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
