#include <cflow/io_source.h>

#include <turbo/thread.h>

#include "value_storage.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    CFLOW_IO_SOURCE_CAPACITY = 1,
    CFLOW_IO_SOURCE_LEASE_ID = 1
};

typedef struct cflow_io_source_state {
    cflow_io_actor actor;
    cflow_executor executor;
    cflow_value_slot result;
    turbo_mutex_t gate;
    cflow_waker source_waker;
    cflow_io_request_id request_id;
    bool source_live;
    bool owner_live;
    bool close_requested;
    bool driver_active;
    bool result_ready;
    bool acknowledged;
    const char *name;
    const cmeta_type_desc *type;
    cflow_io_source_prepare_fn prepare;
    cflow_io_source_encode_fn encode;
    void *user;
    cflow_source_terminal terminal;
    const char *terminal_error;
} cflow_io_source_state;

static const char io_source_unavailable_error[] =
    "IO source is unavailable";
static const char io_source_cancelled_error[] =
    "IO source is cancelled";
static const char io_source_prepare_error[] =
    "IO source preparation failed";
static const char io_source_prepare_status_error[] =
    "IO source preparation returned an invalid status";
static const char io_source_operation_error[] =
    "IO source operation is missing a release callback";
static const char io_source_submit_full_error[] =
    "IO source Actor admission is full";
static const char io_source_submit_closed_error[] =
    "IO source Actor admission is closed";
static const char io_source_submit_lease_error[] =
    "IO source Actor lease is already active";
static const char io_source_submit_id_error[] =
    "IO source Actor request IDs are exhausted";
static const char io_source_submit_invalid_error[] =
    "IO source Actor rejected the operation";

static bool io_source_type_valid(const cmeta_type_desc *type) {
    return cmeta_type_desc_valid(type) &&
           cflow_value_storage_type_supported(type) &&
           type->size != 0u && type->align != 0u &&
           (type->align & (type->align - 1u)) == 0u &&
           type->size <= SIZE_MAX - (type->align - 1u);
}

static cflow_step io_source_terminal_step(
    cflow_io_source_state *state) {
    if (state->terminal == CFLOW_SOURCE_DONE)
        return (cflow_step){CFLOW_STEP_DONE, {0}, NULL};
    if (state->terminal == CFLOW_SOURCE_ERROR)
        return (cflow_step){CFLOW_STEP_ERROR, {0},
                            state->terminal_error};
    return (cflow_step){CFLOW_STEP_ERROR, {0},
                        io_source_unavailable_error};
}

static cflow_step io_source_set_terminal(
    cflow_io_source_state *state,
    cflow_source_terminal terminal,
    const char *error) {
    cflow_step step;

    turbo_mutex_lock(&state->gate);
    if (state->terminal == CFLOW_SOURCE_OPEN) {
        state->terminal = terminal;
        state->terminal_error = error;
    }
    step = io_source_terminal_step(state);
    turbo_mutex_unlock(&state->gate);
    return step;
}

static bool io_source_wait_arm(void *self, cflow_waker waker) {
    cflow_io_source_state *state = (cflow_io_source_state *)self;
    cflow_waker wake_now = {0};

    if (state == NULL || waker.wake == NULL)
        return false;
    turbo_mutex_lock(&state->gate);
    if (!state->source_live || state->close_requested) {
        turbo_mutex_unlock(&state->gate);
        return false;
    }
    if (state->terminal != CFLOW_SOURCE_OPEN || state->result_ready ||
        state->acknowledged) {
        wake_now = waker;
    } else if (state->request_id == 0u) {
        turbo_mutex_unlock(&state->gate);
        return false;
    } else {
        state->source_waker = waker;
    }
    turbo_mutex_unlock(&state->gate);
    if (wake_now.wake != NULL)
        wake_now.wake(wake_now.user);
    return true;
}

static void io_source_wait_cancel(void *self) {
    cflow_io_source_state *state = (cflow_io_source_state *)self;

    if (state == NULL)
        return;
    turbo_mutex_lock(&state->gate);
    state->source_waker = (cflow_waker){0};
    turbo_mutex_unlock(&state->gate);
}

CMETA_IMPLEMENTS(cflow_waitable, io_source_waitable, 0,
    .arm = io_source_wait_arm,
    .cancel = io_source_wait_cancel
);

static const char *io_source_submit_error(cflow_io_submit_status status) {
    switch (status) {
        case CFLOW_IO_SUBMIT_FULL:
            return io_source_submit_full_error;
        case CFLOW_IO_SUBMIT_CLOSED:
            return io_source_submit_closed_error;
        case CFLOW_IO_SUBMIT_LEASE_IN_USE:
            return io_source_submit_lease_error;
        case CFLOW_IO_SUBMIT_ID_EXHAUSTED:
            return io_source_submit_id_error;
        case CFLOW_IO_SUBMIT_INVALID_ARGUMENT:
        case CFLOW_IO_SUBMIT_ACCEPTED:
            return io_source_submit_invalid_error;
    }
    return io_source_submit_invalid_error;
}

static cflow_step io_source_resume(
    void *self, cflow_resume_ctx *ctx, void *out_value) {
    cflow_io_source_state *state = (cflow_io_source_state *)self;
    cflow_io_operation operation = {0};
    cflow_io_submit_result submitted;
    cflow_io_source_prepare_status prepared;
    const char *error = NULL;
    cflow_step step;

    (void)ctx;
    if (state == NULL || out_value == NULL)
        return (cflow_step){CFLOW_STEP_ERROR, {0},
                            io_source_unavailable_error};

    turbo_mutex_lock(&state->gate);
    if (state->terminal != CFLOW_SOURCE_OPEN) {
        step = io_source_terminal_step(state);
        turbo_mutex_unlock(&state->gate);
        return step;
    }
    if (state->close_requested || !state->source_live) {
        turbo_mutex_unlock(&state->gate);
        return io_source_set_terminal(
            state, CFLOW_SOURCE_ERROR, io_source_cancelled_error);
    }
    if (state->request_id != 0u || state->result_ready) {
        turbo_mutex_unlock(&state->gate);
        return (cflow_step){
            CFLOW_STEP_WAIT,
            io_source_waitable_as_cflow_waitable(state), NULL};
    }
    turbo_mutex_unlock(&state->gate);

    prepared = state->prepare(state->user, &operation, &error);
    if (prepared == CFLOW_IO_SOURCE_PREPARE_DONE)
        return io_source_set_terminal(
            state, CFLOW_SOURCE_DONE, NULL);
    if (prepared == CFLOW_IO_SOURCE_PREPARE_ERROR)
        return io_source_set_terminal(
            state, CFLOW_SOURCE_ERROR,
            error != NULL && error[0] != '\0'
                ? error : io_source_prepare_error);
    if (prepared != CFLOW_IO_SOURCE_PREPARE_OPERATION)
        return io_source_set_terminal(
            state, CFLOW_SOURCE_ERROR,
            io_source_prepare_status_error);
    if (operation.release == NULL)
        return io_source_set_terminal(
            state, CFLOW_SOURCE_ERROR, io_source_operation_error);

    submitted = cflow_io_actor_try_submit(
        &state->actor, CFLOW_IO_SOURCE_LEASE_ID, &operation);
    if (submitted.status != CFLOW_IO_SUBMIT_ACCEPTED) {
        const char *submit_error = io_source_submit_error(
            submitted.status);
        operation.release(operation.user);
        return io_source_set_terminal(
            state, CFLOW_SOURCE_ERROR, submit_error);
    }

    turbo_mutex_lock(&state->gate);
    state->request_id = submitted.request_id;
    state->acknowledged = false;
    turbo_mutex_unlock(&state->gate);
    return (cflow_step){
        CFLOW_STEP_WAIT,
        io_source_waitable_as_cflow_waitable(state), NULL};
}

static void io_source_cancel(void *self) {
    cflow_io_source_state *state = (cflow_io_source_state *)self;
    bool close_actor = false;

    if (state == NULL)
        return;
    turbo_mutex_lock(&state->gate);
    if (!state->close_requested) {
        state->close_requested = true;
        close_actor = true;
    }
    state->source_waker = (cflow_waker){0};
    turbo_mutex_unlock(&state->gate);
    if (close_actor)
        (void)cflow_io_actor_close(&state->actor);
}

static void io_source_destroy(void *self) {
    cflow_io_source_state *state = (cflow_io_source_state *)self;

    if (state == NULL)
        return;
    io_source_cancel(state);
    turbo_mutex_lock(&state->gate);
    state->source_live = false;
    state->source_waker = (cflow_waker){0};
    turbo_mutex_unlock(&state->gate);
}

static const char *io_source_name(void *self) {
    cflow_io_source_state *state = (cflow_io_source_state *)self;
    return state != NULL && state->name != NULL
        ? state->name : "io-source";
}

static const cmeta_type_desc *io_source_output_type(void *self) {
    cflow_io_source_state *state = (cflow_io_source_state *)self;
    return state != NULL ? state->type : NULL;
}

static void io_source_bind_terminal_waker(
    void *self, cflow_waker waker) {
    (void)self;
    (void)waker;
}

static cflow_source_terminal io_source_poll_terminal(
    void *self, const char **error) {
    cflow_io_source_state *state = (cflow_io_source_state *)self;
    cflow_source_terminal terminal;

    if (state == NULL)
        return CFLOW_SOURCE_ERROR;
    turbo_mutex_lock(&state->gate);
    terminal = state->terminal;
    if (terminal == CFLOW_SOURCE_ERROR && error != NULL)
        *error = state->terminal_error;
    turbo_mutex_unlock(&state->gate);
    return terminal;
}

CMETA_IMPLEMENTS(cflow_source, io_actor_source, 0,
    .name = io_source_name,
    .output_type = io_source_output_type,
    .resume = io_source_resume,
    .cancel = io_source_cancel,
    .destroy = io_source_destroy,
    .bind_terminal_waker = io_source_bind_terminal_waker,
    .poll_terminal = io_source_poll_terminal
);

static void io_source_completion(
    void *completion_user,
    cflow_io_request_id request_id,
    cflow_io_lease_id lease_id,
    void *operation_user,
    const cflow_io_completion *completion) {
    cflow_io_source_state *state =
        (cflow_io_source_state *)completion_user;

    (void)lease_id;
    (void)operation_user;
    (void)completion;
    if (state == NULL)
        return;
    turbo_mutex_lock(&state->gate);
    if (state->request_id == request_id)
        state->acknowledged = false;
    turbo_mutex_unlock(&state->gate);
}

int cflow_source_from_io_actor(
    cflow_source *out,
    cflow_io_source_owner *owner,
    const cflow_io_source_config *config) {
    const bool out_zero = out != NULL &&
        out->self == NULL && out->vtable == NULL;
    const bool owner_zero = owner != NULL && owner->impl == NULL;
    cflow_io_source_state *state = NULL;
    cflow_io_actor_config actor_config = {0};
    bool gate_initialized = false;
    bool result_initialized = false;
    bool executor_initialized = false;
    int status = TURBO_ENOMEM;

    if (out != NULL)
        *out = (cflow_source){0};
    if (owner != NULL)
        *owner = (cflow_io_source_owner){0};
    if (!out_zero || !owner_zero || config == NULL ||
        config->backend.submit == NULL || config->prepare == NULL ||
        config->encode == NULL || config->drive == NULL ||
        !io_source_type_valid(config->type))
        return TURBO_EINVAL;

    state = (cflow_io_source_state *)calloc(1u, sizeof(*state));
    if (state == NULL)
        goto cleanup;
    turbo_mutex_init(&state->gate);
    if (state->gate == NULL)
        goto cleanup;
    gate_initialized = true;
    if (!cflow_value_slot_init(&state->result, config->type))
        goto cleanup;
    result_initialized = true;
    if (!cflow_executor_manual_init_with_capacity(
            &state->executor, CFLOW_IO_SOURCE_CAPACITY))
        goto cleanup;
    executor_initialized = true;

    actor_config.request_capacity = CFLOW_IO_SOURCE_CAPACITY;
    actor_config.command_capacity = CFLOW_IO_SOURCE_CAPACITY;
    actor_config.executor = &state->executor;
    actor_config.backend = config->backend;
    actor_config.backend_user = config->backend_user;
    actor_config.completion = io_source_completion;
    actor_config.completion_user = state;
    actor_config.wake = config->drive;
    actor_config.wake_user = config->drive_user;
    status = cflow_io_actor_init(&state->actor, &actor_config);
    if (status != TURBO_OK) {
        if (status != TURBO_ENOMEM)
            status = TURBO_ENOMEM;
        goto cleanup;
    }

    state->name = config->name;
    state->type = config->type;
    state->prepare = config->prepare;
    state->encode = config->encode;
    state->user = config->user;
    state->source_live = true;
    state->owner_live = true;
    state->terminal = CFLOW_SOURCE_OPEN;
    *out = io_actor_source_as_cflow_source(state);
    owner->impl = state;
    return TURBO_OK;

cleanup:
    if (executor_initialized)
        cflow_executor_destroy(&state->executor);
    if (result_initialized)
        cflow_value_slot_destroy(&state->result);
    if (gate_initialized)
        turbo_mutex_destroy(&state->gate);
    free(state);
    return status;
}

int cflow_io_source_owner_run_ready(
    cflow_io_source_owner *owner,
    size_t max_steps,
    size_t *progressed) {
    cflow_io_source_state *state;
    int status = TURBO_OK;

    if (progressed != NULL)
        *progressed = 0u;
    if (owner == NULL || owner->impl == NULL || max_steps == 0u ||
        progressed == NULL)
        return TURBO_EINVAL;
    state = (cflow_io_source_state *)owner->impl;
    turbo_mutex_lock(&state->gate);
    if (state->driver_active || !state->owner_live) {
        turbo_mutex_unlock(&state->gate);
        return TURBO_EBUSY;
    }
    state->driver_active = true;
    turbo_mutex_unlock(&state->gate);

    while (*progressed < max_steps) {
        cflow_io_run_result result =
            cflow_io_actor_run_one(&state->actor);
        if (result.status == CFLOW_IO_RUN_BUSY) {
            status = TURBO_EBUSY;
            break;
        }
        if (result.status != CFLOW_IO_RUN_PROGRESSED)
            break;
        ++*progressed;
    }

    turbo_mutex_lock(&state->gate);
    state->driver_active = false;
    turbo_mutex_unlock(&state->gate);
    return status;
}

bool cflow_io_source_owner_is_quiescent(
    const cflow_io_source_owner *owner) {
    cflow_io_source_state *state;
    bool driver_active;

    if (owner == NULL || owner->impl == NULL)
        return false;
    state = (cflow_io_source_state *)owner->impl;
    turbo_mutex_lock(&state->gate);
    driver_active = state->driver_active;
    turbo_mutex_unlock(&state->gate);
    return !driver_active && cflow_io_actor_is_quiescent(&state->actor);
}

bool cflow_io_source_owner_get_stats(
    const cflow_io_source_owner *owner,
    cflow_io_source_stats *out) {
    cflow_io_source_state *state;
    cflow_io_source_stats snapshot = {0};

    if (owner == NULL || owner->impl == NULL || out == NULL)
        return false;
    state = (cflow_io_source_state *)owner->impl;
    turbo_mutex_lock(&state->gate);
    snapshot.source_live = state->source_live;
    snapshot.request_active = state->request_id != 0u;
    snapshot.result_ready = state->result_ready;
    snapshot.close_requested = state->close_requested;
    turbo_mutex_unlock(&state->gate);
    if (!cflow_io_actor_get_stats(&state->actor, &snapshot.actor))
        return false;
    *out = snapshot;
    return true;
}

int cflow_io_source_owner_close(cflow_io_source_owner *owner) {
    cflow_io_source_state *state;
    int status;

    if (owner == NULL)
        return TURBO_EINVAL;
    if (owner->impl == NULL)
        return TURBO_OK;
    state = (cflow_io_source_state *)owner->impl;
    turbo_mutex_lock(&state->gate);
    if (!state->owner_live || state->source_live || state->driver_active) {
        turbo_mutex_unlock(&state->gate);
        return TURBO_EBUSY;
    }
    turbo_mutex_unlock(&state->gate);
    if (!cflow_io_actor_is_quiescent(&state->actor))
        return TURBO_EBUSY;

    status = cflow_io_actor_destroy(&state->actor);
    if (status != TURBO_OK)
        return status == TURBO_EBUSY ? TURBO_EBUSY : TURBO_EINVAL;
    turbo_mutex_lock(&state->gate);
    state->owner_live = false;
    turbo_mutex_unlock(&state->gate);
    (void)cflow_executor_shutdown(&state->executor);
    cflow_executor_destroy(&state->executor);
    cflow_value_slot_destroy(&state->result);
    owner->impl = NULL;
    turbo_mutex_destroy(&state->gate);
    free(state);
    return TURBO_OK;
}
