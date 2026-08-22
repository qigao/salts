#include <turbostl/typed.h>
#include "tinytest.hpp"

#include <type_traits>

static_assert(!std::is_same_v<set_t, hash_set_t>,
              "Set and HashSet must remain independent container types");
static_assert(!std::is_same_v<map_t, btree_t>,
              "Map and BTree must remain independent container types");

spec("TurboSTL typed C++ public header") {
  it("exposes ordinary self-describing handle types") {
    vec_t vec{};
    list_t list{};
    map_t map{};

    check_true(sizeof(vec) > 0);
    check_true(sizeof(list) > 0);
    check_true(sizeof(map) > 0);
  }
}
