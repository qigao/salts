#include <salts/local.h>
#include "tinytest.hpp"

static salts_local_state state;
static int owner;
suite("Platform local binding in C++") {
    before_each() { state = {}; }
    after_each() {
        if (state.phase == SALTS_LOCAL_READY)
            check_equal(salts_local_enter(&state, &owner), SALTS_OK);
        if (state.phase == SALTS_LOCAL_BUSY)
            check_equal(salts_local_reset(&state, &owner), SALTS_OK);
    }
    it("borrows the C owner mechanism with explicit publication and cleanup") {
        check_equal(salts_local_begin(&state, &owner), SALTS_OK);
        check_equal(salts_local_check(&state, &owner), SALTS_EBUSY);
        check_equal(salts_local_publish(&state, &owner), SALTS_OK);
        check_equal(salts_local_check(&state, &owner), SALTS_OK);
        auto copy = state;
        check_equal(salts_local_enter(&copy, &owner), SALTS_EINVAL);
        check_equal(salts_local_enter(&state, &owner), SALTS_OK);
        check_equal(salts_local_reset(&state, &owner), SALTS_OK);
        check_true(state.self == nullptr);
        check_equal(salts_local_begin(&state, nullptr), SALTS_EINVAL);
    }
}
