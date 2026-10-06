#include "cmeta_fastpath_fixture.h"
#include "tinytest.hpp"

suite("CMeta C++ opaque fastpath") {
    it("controls C-owned atomic storage without assuming C++ atomic layout") {
        check_equal(cmeta_static_disable(&fastpath_shared_key), CMETA_OK);
        check_false(cmeta_static_branch(&fastpath_shared_key));
        check_equal(cmeta_static_enable(&fastpath_shared_key), CMETA_OK);
        check_true(cmeta_static_branch(&fastpath_shared_key));
#if CMETA_NATIVE_FASTPATH
        check_true(cmeta_static_branch_native(&fastpath_shared_key));
#endif
        check_equal(fastpath_cpp_invoke(1), 4);
        check_equal(cmeta_static_disable(&fastpath_shared_key), CMETA_OK);
        check_equal(cmeta_static_enable(nullptr), CMETA_INVALID_ARGUMENT);
    }
}
