#include <turbo/stl/typed.h>
#include "tinytest.h"

#ifndef CMETA_GENERIC_KIND_Vec
#error "typed.h must expose the Vec generic-kind registration"
#endif

suite("Container typed public header") {
    it("directly exposes raw typed facade prerequisites") {
        turbo_vec_t vec = {0};

        check_true(sizeof(vec) > 0);
    }
}
