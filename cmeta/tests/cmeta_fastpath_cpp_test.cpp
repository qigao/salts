#include "cmeta_fastpath_fixture.h"
#include "tinytest.hpp"

suite("CMeta C++ opaque fastpath") {
    it("controls C-owned atomic storage without assuming C++ atomic layout") {
        check_equal(salts_fast_disable(&fastpath_shared_key), SALTS_OK);
        check_false(salts_fast_branch(&fastpath_shared_key));
        check_equal(salts_fast_enable(&fastpath_shared_key), SALTS_OK);
        check_true(salts_fast_branch(&fastpath_shared_key));
#if SALTS_PLATFORM_NATIVE_FASTPATH
        check_true(salts_fast_key_read_native(&fastpath_shared_key));
#endif
        check_equal(fastpath_cpp_invoke(1), 4);
        check_equal(salts_fast_disable(&fastpath_shared_key), SALTS_OK);
        check_equal(salts_fast_enable(nullptr), SALTS_EINVAL);
    }
}
