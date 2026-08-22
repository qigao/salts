#include <turbostl/typed.h>
#include "tinytest.hpp"

#include <type_traits>

#ifndef CMETA_GENERIC_KIND_Vec
#error "typed.h must expose the Vec generic-kind registration"
#endif

static_assert(!std::is_same_v<set_t, hash_set_t>,
              "Set and HashSet must be independent container types");
static_assert(!std::is_same_v<map_t, btree_t>,
              "Map and BTree must be independent container types");

spec("TurboSTL typed C++ public header") {
  it("directly exposes raw typed facade prerequisites") {
    vec_t vec{};

    check_true(sizeof(vec) > 0);
  }
}
