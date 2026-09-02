#include <rocida/stl.h>
#include "tinytest.hpp"

#include <cstddef>

static_assert(STL_OK == 0, "Rocida STL status must remain zero-success");

spec("Rocida STL C++ public header") {
  it("exposes natural zero-initializable raw container handles") {
    vec_t vec{};
    deque_t deque{};
    list_t list{};
    turbostl_stack_t stack{};
    queue_t queue{};
    heap_t heap{};
    hash_map_t map{};

    check_true(sizeof(vec) > 0);
    check_true(sizeof(deque) > 0);
    check_true(sizeof(list) > 0);
    check_true(sizeof(stack) > 0);
    check_true(sizeof(queue) > 0);
    check_true(sizeof(heap) > 0);
    check_true(sizeof(map) > 0);
    check_equal(vec_init_bytes(&vec, sizeof(int), alignof(int), 1u), STL_OK);
    vec_destroy(&vec);
  }
}
