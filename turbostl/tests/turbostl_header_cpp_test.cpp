#include <turbo/stl.h>
#include "tinytest.hpp"

#include <cstddef>

static_assert(TURBO_STL_OK == 0, "Container status must remain zero-success");

spec("Container C++ public header") {
  it("exposes zero-initializable raw standard container handles") {
    turbo_vec_t vec{};
    turbo_hash_map_t map{};

    check_true(sizeof(vec) > 0);
    check_true(sizeof(map) > 0);
    check_equal(turbo_vec_init_bytes(&vec, sizeof(int), alignof(int), 1u), TURBO_STL_OK);
    turbo_vec_destroy(&vec);
  }
}
