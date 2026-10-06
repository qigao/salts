#include <salts/plugin_decl.h>

#include "plugin_lifecycle_test_interface.h"

#include <stdatomic.h>

typedef struct lifecycle_fixture_state {
    atomic_bool started;
    atomic_bool stopping;
    atomic_uint active_calls;
    atomic_ullong accepted_calls;
    atomic_uint destroy_calls;
} lifecycle_fixture_state;

static lifecycle_fixture_state fixture_state;

static bool fixture_send(void *self, int value) {
    lifecycle_fixture_state *state = (lifecycle_fixture_state *)self;
    bool accepted = false;
    (void)value;

    if (!atomic_load(&state->started) || atomic_load(&state->stopping))
        return false;

    atomic_fetch_add(&state->active_calls, 1u);
    if (!atomic_load(&state->stopping)) {
        atomic_fetch_add(&state->accepted_calls, 1u);
        accepted = true;
    }
    atomic_fetch_sub(&state->active_calls, 1u);
    return accepted;
}

static uint64_t fixture_accepted(void *self) {
    lifecycle_fixture_state *state = (lifecycle_fixture_state *)self;
    return (uint64_t)atomic_load(&state->accepted_calls);
}

CMETA_IMPLEMENTS(plugin_lifecycle_test_api, lifecycle_fixture_api_impl, 1u,
    .send = fixture_send,
    .accepted = fixture_accepted);

static plugin_lifecycle_test_api fixture_api = {
    &fixture_state, &lifecycle_fixture_api_impl_vtable
};

static salts_plugin_status SALTS_PLUGIN_CALL
fixture_start(void *self) {
    lifecycle_fixture_state *state = (lifecycle_fixture_state *)self;
    atomic_store(&state->stopping, false);
    atomic_store(&state->started, true);
    return SALTS_PLUGIN_OK;
}

static salts_plugin_status SALTS_PLUGIN_CALL
fixture_request_stop(void *self) {
    lifecycle_fixture_state *state = (lifecycle_fixture_state *)self;
    atomic_store(&state->stopping, true);
    atomic_store(&state->started, false);
    return SALTS_PLUGIN_OK;
}

static bool SALTS_PLUGIN_CALL
fixture_is_quiescent(const void *self) {
    const lifecycle_fixture_state *state =
        (const lifecycle_fixture_state *)self;
    return atomic_load(&state->stopping) &&
           atomic_load(&state->active_calls) == 0u;
}

static void SALTS_PLUGIN_CALL
fixture_destroy(void *self) {
    lifecycle_fixture_state *state = (lifecycle_fixture_state *)self;
    atomic_store(&state->started, false);
    atomic_store(&state->stopping, true);
    atomic_fetch_add(&state->destroy_calls, 1u);
}

#define FIXTURE_EXPORTS(X) \
    X(interface, (plugin_lifecycle_test_api, &fixture_api), "service", \
      "test.lifecycle.service", 1u, 1u)

SALTS_PLUGIN_DECLARE(fixture, "test.loader.lifecycle", (1u,0u,0u),
    FIXTURE_EXPORTS, SALTS_PLUGIN_LIFECYCLE(&fixture_state,
        fixture_start, fixture_request_stop, fixture_is_quiescent, fixture_destroy));
