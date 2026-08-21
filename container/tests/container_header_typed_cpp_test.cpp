#include <turbo/container/typed.h>
#include "tinytest.hpp"

#include <type_traits>

#ifndef CMETA_GENERIC_KIND_Vec
#error "typed.h must expose the Vec generic-kind registration"
#endif

static_assert(!std::is_same_v<turbo_set_t, turbo_hash_set_t>,
              "Set and HashSet must be independent container types");

spec("Container typed C++ public header") {
  it("directly exposes raw typed facade prerequisites") {
    turbo_vec_t vec{};

    check_true(sizeof(vec) > 0);
  }
}
