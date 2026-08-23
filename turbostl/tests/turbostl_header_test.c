#include <turbostl.h>
#include "tinytest.h"

suite("TurboSTL public header") {
    it("exposes natural raw container handles and status") {
        vec_t vec = {0};
        deque_t deque = {0};
        list_t list = {0};
        stack_t stack = {0};
        queue_t queue = {0};
        heap_t heap = {0};
        hash_map_t map = {0};
        stl_status status = STL_OK;

        check_true(sizeof(vec) > 0);
        check_true(sizeof(deque) > 0);
        check_true(sizeof(list) > 0);
        check_true(sizeof(stack) > 0);
        check_true(sizeof(queue) > 0);
        check_true(sizeof(heap) > 0);
        check_true(sizeof(map) > 0);
        check_equal(status, STL_OK);
        check_equal(vec_init_bytes(&vec, sizeof(int), _Alignof(int), 1u), STL_OK);
        vec_destroy(&vec);
    }
}
