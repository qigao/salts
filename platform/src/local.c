#include <salts/local.h>

static int local_binding_valid(const salts_local_state *state, const void *owner) {
    if (state == NULL || owner == NULL || state->self != state || state->owner != owner ||
        state->thread != salts_thread_current_token()) return SALTS_EINVAL;
    return SALTS_OK;
}

int salts_local_begin(salts_local_state *state, const void *owner) {
    if (state == NULL || owner == NULL || state->self != NULL || state->owner != NULL ||
        state->thread != NULL || state->phase != SALTS_LOCAL_ZERO) return SALTS_EINVAL;
    state->self = state;
    state->owner = owner;
    state->thread = salts_thread_current_token();
    state->phase = SALTS_LOCAL_BUSY;
    return SALTS_OK;
}

int salts_local_publish(salts_local_state *state, const void *owner) {
    int status = local_binding_valid(state, owner);
    if (status != SALTS_OK) return status;
    if (state->phase != SALTS_LOCAL_BUSY) return SALTS_EINVAL;
    state->phase = SALTS_LOCAL_READY;
    return SALTS_OK;
}

int salts_local_check(const salts_local_state *state, const void *owner) {
    int status = local_binding_valid(state, owner);
    if (status != SALTS_OK) return status;
    if (state->phase == SALTS_LOCAL_BUSY) return SALTS_EBUSY;
    return state->phase == SALTS_LOCAL_READY ? SALTS_OK : SALTS_EINVAL;
}

int salts_local_enter(salts_local_state *state, const void *owner) {
    int status = salts_local_check(state, owner);
    if (status != SALTS_OK) return status;
    state->phase = SALTS_LOCAL_BUSY;
    return SALTS_OK;
}

int salts_local_reset(salts_local_state *state, const void *owner) {
    int status = local_binding_valid(state, owner);
    if (status != SALTS_OK) return status;
    if (state->phase != SALTS_LOCAL_BUSY) return SALTS_EINVAL;
    *state = (salts_local_state){0};
    return SALTS_OK;
}
