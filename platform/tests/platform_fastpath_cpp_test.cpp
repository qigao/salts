#include "platform_fastpath_fixture.h"
#include "tinytest.hpp"

suite("Platform C++ borrows opaque C gates") {
    it("publishes and consumes without assuming std::atomic layout") {
        check_equal(cmeta_fast_disable(&platform_shared_key), SALTS_OK);
        check_false(cmeta_fast_branch(&platform_shared_key));
        check_equal(cmeta_fast_enable(&platform_shared_key), SALTS_OK);
#if SALTS_PLATFORM_NATIVE_FASTPATH
        check_true(cmeta_fast_key_read_native(&platform_shared_key));
#endif
        check_true(cmeta_fast_key_consume(&platform_shared_key));
        check_false(cmeta_fast_key_consume(&platform_shared_key));
        check_equal(cmeta_fast_enable(nullptr), SALTS_EINVAL);
    }
}
