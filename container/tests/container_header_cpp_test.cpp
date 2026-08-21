#include <turbo/container.h>
#include "tinytest.hpp"

#include <cstddef>

static_assert(CONTAINER_OK == 0, "Container status must remain zero-success");

spec("Container C++ public header") {
  it("exposes zero-initializable raw standard container handles") {
    turbo_vec_t vec{};
    turbo_hash_map_t map{};

    check_true(sizeof(vec) > 0);
    check_true(sizeof(map) > 0);
  }
}
