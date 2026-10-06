#include "platform_fastpath_fixture.h"
#include "tinytest.hpp"

suite("Platform C++ borrows opaque C gates") {
    it("publishes and consumes without assuming std::atomic layout") {
        check_equal(salts_static_disable(&platform_shared_key), SALTS_OK);
        check_false(salts_static_branch(&platform_shared_key));
        check_equal(salts_static_enable(&platform_shared_key), SALTS_OK);
#if SALTS_NATIVE_FASTPATH
        check_true(salts_static_branch_native(&platform_shared_key));
#endif
        check_true(salts_fault_hit(&platform_shared_key));
        check_false(salts_fault_hit(&platform_shared_key));
        check_equal(salts_static_enable(nullptr), SALTS_EINVAL);
    }
}
