#include <turbo/container/typed.h>
#include "tinytest.hpp"

#ifndef CMETA_GENERIC_KIND_Vec
#error "typed.h must expose the Vec generic-kind registration"
#endif

spec("Container typed C++ public header") {
  it("directly exposes raw typed facade prerequisites") {
    turbo_vec_t vec{};

    check_true(sizeof(vec) > 0);
  }
}
