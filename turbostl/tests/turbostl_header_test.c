#include <turbo/stl.h>
#include "tinytest.h"

suite("Container public header") {
    it("exposes raw standard container handles and status") {
        turbo_vec_t vec = {0};
        turbo_hash_map_t map = {0};

        check_true(sizeof(vec) > 0);
        check_true(sizeof(map) > 0);
        check_equal(TURBO_STL_OK, 0);
        check_equal(turbo_vec_init_bytes(&vec, sizeof(int), _Alignof(int), 1u), TURBO_STL_OK);
        turbo_vec_destroy(&vec);
    }
}
