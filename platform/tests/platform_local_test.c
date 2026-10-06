#include <salts/local.h>
#include "tinytest.h"

static salts_local_state state;
static int owner;
typedef struct local_probe {
    int check, publish, enter, reset;
    int own_begin, own_publish, own_check, own_enter, own_reset;
} local_probe;
static SALTS_THREAD_LOCAL salts_local_state worker_state;
static SALTS_THREAD_LOCAL int worker_owner;
static void probe_foreign(void *arg) {
    local_probe *probe = arg;
    probe->check = salts_local_check(&state, &owner);
    probe->publish = salts_local_publish(&state, &owner);
    probe->enter = salts_local_enter(&state, &owner);
    probe->reset = salts_local_reset(&state, &owner);
    probe->own_begin = salts_local_begin(&worker_state, &worker_owner);
    probe->own_publish = salts_local_publish(&worker_state, &worker_owner);
    probe->own_check = salts_local_check(&worker_state, &worker_owner);
    probe->own_enter = salts_local_enter(&worker_state, &worker_owner);
    probe->own_reset = salts_local_reset(&worker_state, &worker_owner);
}

suite("Platform-owned local binding transitions") {
    before_each() { state = (salts_local_state){0}; }
    after_each() {
        if (state.phase == SALTS_LOCAL_READY)
            check_equal(salts_local_enter(&state, &owner), SALTS_OK);
        if (state.phase == SALTS_LOCAL_BUSY)
            check_equal(salts_local_reset(&state, &owner), SALTS_OK);
    }
    it("makes construction, publication, exclusive callbacks and reset explicit") {
        check_equal(salts_local_check(&state, &owner), SALTS_EINVAL);
        check_equal(salts_local_begin(&state, &owner), SALTS_OK);
        check_equal(salts_local_check(&state, &owner), SALTS_EBUSY);
        check_equal(salts_local_enter(&state, &owner), SALTS_EBUSY);
        check_equal(salts_local_begin(&state, &owner), SALTS_EINVAL);
        check_equal(salts_local_publish(&state, &owner), SALTS_OK);
        check_equal(salts_local_check(&state, &owner), SALTS_OK);
        check_equal(salts_local_publish(&state, &owner), SALTS_EINVAL);
        check_equal(salts_local_reset(&state, &owner), SALTS_EINVAL);
        check_equal(state.phase, SALTS_LOCAL_READY);
        check_equal(salts_local_enter(&state, &owner), SALTS_OK);
        check_equal(salts_local_check(&state, &owner), SALTS_EBUSY);
        check_equal(salts_local_publish(&state, &owner), SALTS_OK);
        check_equal(salts_local_check(&state, &owner), SALTS_OK);
        check_equal(salts_local_enter(&state, &owner), SALTS_OK);
        check_equal(salts_local_reset(&state, &owner), SALTS_OK);
        check_equal(state.phase, SALTS_LOCAL_ZERO);
        check_null(state.self);
        check_null(state.owner);
        check_null(state.thread);
        check_equal(salts_local_reset(&state, &owner), SALTS_EINVAL);
        check_equal(salts_local_begin(&state, &owner), SALTS_OK);
        /* Failed payload construction follows the same bounded terminal path. */
        check_equal(salts_local_reset(&state, &owner), SALTS_OK);
    }
    it("rejects null, wrong owner and copied bindings without changing the original") {
        int other;
        check_equal(salts_local_begin(NULL, &owner), SALTS_EINVAL);
        check_equal(salts_local_begin(&state, NULL), SALTS_EINVAL);
        check_equal(state.phase, SALTS_LOCAL_ZERO);
        check_equal(salts_local_publish(NULL, &owner), SALTS_EINVAL);
        check_equal(salts_local_check(NULL, &owner), SALTS_EINVAL);
        check_equal(salts_local_enter(NULL, &owner), SALTS_EINVAL);
        check_equal(salts_local_reset(NULL, &owner), SALTS_EINVAL);
        check_equal(salts_local_begin(&state, &owner), SALTS_OK);
        check_equal(salts_local_publish(&state, &owner), SALTS_OK);
        check_equal(salts_local_check(&state, &other), SALTS_EINVAL);
        check_equal(salts_local_enter(&state, NULL), SALTS_EINVAL);
        salts_local_state copied = state;
        check_equal(salts_local_check(&copied, &owner), SALTS_EINVAL);
        check_equal(salts_local_enter(&copied, &owner), SALTS_EINVAL);
        check_equal(salts_local_publish(&copied, &owner), SALTS_EINVAL);
        check_equal(salts_local_reset(&copied, &owner), SALTS_EINVAL);
        check_equal(salts_local_check(&state, &owner), SALTS_OK);
    }
    it("rejects invalid phases without silently repairing the binding") {
        enum { INVALID_PHASE = 99 };
        check_equal(salts_local_begin(&state, &owner), SALTS_OK);
        state.phase = (salts_local_phase)INVALID_PHASE;
        check_equal(salts_local_check(&state, &owner), SALTS_EINVAL);
        check_equal(salts_local_publish(&state, &owner), SALTS_EINVAL);
        check_equal(salts_local_enter(&state, &owner), SALTS_EINVAL);
        check_equal(salts_local_reset(&state, &owner), SALTS_EINVAL);
        check_equal(state.phase, (salts_local_phase)INVALID_PHASE);
        state.phase = SALTS_LOCAL_BUSY;
    }
    it("keeps the owner live while another thread rejects access and cleans its TLS binding") {
        local_probe probe = {0};
        salts_thread_t thread = NULL;
        check_equal(salts_local_begin(&state, &owner), SALTS_OK);
        check_equal(salts_local_publish(&state, &owner), SALTS_OK);
        check_equal(salts_thread_create(&thread, probe_foreign, &probe), 0);
        check_equal(salts_thread_join(&thread), 0);
        check_equal(probe.check, SALTS_EINVAL);
        check_equal(probe.publish, SALTS_EINVAL);
        check_equal(probe.enter, SALTS_EINVAL);
        check_equal(probe.reset, SALTS_EINVAL);
        check_equal(probe.own_begin, SALTS_OK);
        check_equal(probe.own_publish, SALTS_OK);
        check_equal(probe.own_check, SALTS_OK);
        check_equal(probe.own_enter, SALTS_OK);
        check_equal(probe.own_reset, SALTS_OK);
        check_equal(salts_local_check(&state, &owner), SALTS_OK);
    }
}
