#include <turbostl.h>
#include "tinytest.h"

suite("TurboSTL public header") {
    it("infers a vector type without exposing a generated container type") {
        Vec(int, vec);
        int input = 7;
        int output = 0;

        check_equal(vec_init(&vec, 1u), STL_OK);
        check_equal(vec_push(&vec, &input), STL_OK);
        check_equal(vec_pop(&vec, &output), STL_OK);
        check_equal(output, 7);
        vec_destroy(&vec);

        check_equal(vec_init(&vec, 2u), STL_OK);
        vec_destroy(&vec);
    }
}
