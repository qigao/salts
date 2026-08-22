#include <turbostl.h>
#include "tinytest.hpp"

#include <cstddef>

static_assert(TURBO_STL_OK == 0, "TurboSTL status must remain zero-success");

spec("TurboSTL C++ public header") {
  it("exposes zero-initializable raw standard container handles") {
    vec_t vec{};
    hash_map_t map{};

    check_true(sizeof(vec) > 0);
    check_true(sizeof(map) > 0);
    check_equal(vec_init_bytes(&vec, sizeof(int), alignof(int), 1u), TURBO_STL_OK);
    vec_destroy(&vec);
  }
}
