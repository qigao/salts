#include <turbo/container.h>
#include "tinytest.h"

suite("Container public header") {
    it("exposes raw standard container handles and status") {
        turbo_vec_t vec = {0};
        turbo_hash_map_t map = {0};

        check_true(sizeof(vec) > 0);
        check_true(sizeof(map) > 0);
        check_equal(CONTAINER_OK, 0);
    }
}
