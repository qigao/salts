#include <turbostl/typed.h>
#include "tinytest.h"

suite("TurboSTL typed public header") {
    it("exposes self-describing declarations without generated type names") {
        Vec(int, vec);
        List(int, list);
        Map(int, int, map);

        check_equal(vec_init(&vec, 1u), STL_OK);
        check_equal(list_init(&list, 1u), STL_OK);
        check_equal(map_init(&map, 1u), STL_OK);

        map_destroy(&map);
        list_destroy(&list);
        vec_destroy(&vec);
    }
}
