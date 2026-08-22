#include <turbostl.h>
#include "tinytest.h"

suite("TurboSTL public header") {
    it("exposes raw standard container handles and status") {
        vec_t vec = {0};
        hash_map_t map = {0};

        check_true(sizeof(vec) > 0);
        check_true(sizeof(map) > 0);
        check_equal(TURBO_STL_OK, 0);
        check_equal(vec_init_bytes(&vec, sizeof(int), _Alignof(int), 1u), TURBO_STL_OK);
        vec_destroy(&vec);
    }
}
