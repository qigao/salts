#include "cmeta_fastpath_fixture.h"
#include "tinytest.h"
#include <salts_coro.h>

enum { FASTPATH_YIELDS = 8, FASTPATH_CORO_ARGUMENT = 5 };
SALTS_FAST_KEY(fastpath_coro_key, false);
cmeta_static_call(fastpath_coro_slot, fastpath_add);
cmeta_static_call(fastpath_coro_pair_slot, fastpath_aggregate);
typedef struct fastpath_coro_state { int rounds; int errors; } fastpath_coro_state;

static void fastpath_coro_entry(coro_t *co, void *arg) {
    fastpath_coro_state *state = (fastpath_coro_state *)arg;
    fastpath_pair retained = {1.5, 1};
    (void)co;
    for (int round = 0; round < FASTPATH_YIELDS; ++round) {
        int before = cmeta_static_invoke(fastpath_coro_slot, FASTPATH_CORO_ARGUMENT);
        fastpath_pair reference = cmeta_static_invoke(fastpath_coro_pair_slot, retained, 0.5);
#if SALTS_PLATFORM_NATIVE_FASTPATH
        fastpath_pair native = cmeta_static_native_invoke(fastpath_coro_pair_slot, retained, 0.5);
        if (native.value != reference.value || native.count != reference.count ||
            cmeta_static_native_invoke(fastpath_coro_slot, FASTPATH_CORO_ARGUMENT) != before ||
            salts_fast_key_read_native(&fastpath_coro_key) != salts_fast_branch(&fastpath_coro_key))
            ++state->errors;
#endif
        if (coro_yield() != 0) { ++state->errors; return; }
        /* Locals cross the existing minicoro switch; the control plane changes
         * the target while suspended, without releasing either code provider. */
        if (reference.value != retained.value + 0.5 || reference.count != retained.count + 1)
            ++state->errors;
        retained = reference;
        if (cmeta_static_invoke(fastpath_coro_slot, FASTPATH_CORO_ARGUMENT) == before)
            ++state->errors;
        ++state->rounds;
    }
}

suite("CMeta fastpath coroutine ABI") {
    it("matches reference semantics across native context yields and reset") {
        fastpath_coro_state state = {0};
        coro_t *co = coro_create(fastpath_coro_entry, &state, NULL);
        check_not_null(co);
        for (int generation = 0; generation < 2; ++generation) {
            check_equal(cmeta_static_update(fastpath_coro_slot, fastpath_add), CMETA_OK);
            check_equal(salts_fast_disable(&fastpath_coro_key), SALTS_OK);
            for (int round = 0; round <= FASTPATH_YIELDS; ++round) {
                check_equal(coro_resume(co), 0);
                if (round == FASTPATH_YIELDS) break;
                check_equal(coro_state(co), coro_SUSPENDED);
                check_equal(salts_fast_key_set(&fastpath_coro_key, round % 2 == 0), SALTS_OK);
                check_equal(round % 2 == 0
                    ? cmeta_static_update(fastpath_coro_slot, fastpath_other)
                    : cmeta_static_update(fastpath_coro_slot, fastpath_add), CMETA_OK);
            }
            check_equal(coro_state(co), coro_DEAD);
            if (generation == 0) check_equal(coro_reset(co, fastpath_coro_entry, &state), 0);
        }
        coro_destroy(co);
        check_equal(state.rounds, FASTPATH_YIELDS * 2);
        check_equal(state.errors, 0);
    }
}
