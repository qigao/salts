#include "platform_fastpath_fixture.h"
#include "tinytest.hpp"

suite("Platform C++ borrows opaque C gates") {
    it("publishes and consumes without assuming std::atomic layout") {
        check_equal(salts_fast_disable(&platform_shared_key), SALTS_OK);
        check_false(salts_fast_branch(&platform_shared_key));
        check_equal(salts_fast_enable(&platform_shared_key), SALTS_OK);
#if SALTS_PLATFORM_NATIVE_FASTPATH
        check_true(salts_fast_key_read_native(&platform_shared_key));
#endif
        check_true(salts_fast_key_consume(&platform_shared_key));
        check_false(salts_fast_key_consume(&platform_shared_key));
        check_equal(salts_fast_enable(nullptr), SALTS_EINVAL);
    }
}
