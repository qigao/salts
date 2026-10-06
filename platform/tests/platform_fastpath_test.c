#include <salts/fastpath.h>
#include "tinytest.h"

suite("Platform static gates and fault permissions") {
    it("publishes bounded gates and rejects NULL updates") {
        salts_static_key(key, false);
        check_false(salts_static_branch(&key));
        check_equal(salts_static_enable(&key), SALTS_OK);
        check_true(salts_static_key_read(&key));
#if SALTS_NATIVE_FASTPATH
        check_true(salts_static_branch_native(&key));
#endif
        check_equal(salts_static_disable(&key), SALTS_OK);
        check_false(salts_static_key_read(&key));
#if SALTS_NATIVE_FASTPATH
        check_false(salts_static_branch_native(&key));
#endif
        check_equal(salts_static_key_set(NULL, true), SALTS_EINVAL);
        check_equal(salts_static_enable(NULL), SALTS_EINVAL);
        check_equal(salts_static_disable(NULL), SALTS_EINVAL);
    }
    it("coalesces arms and consumes exactly one permission") {
        salts_fault_point(fault);
        check_false(salts_fault_hit(&fault));
        check_equal(salts_fault_arm(fault), SALTS_OK);
        check_equal(salts_fault_arm(fault), SALTS_OK);
        check_true(salts_fault_consume(&fault));
        check_false(salts_fault_hit(&fault));
        check_equal(salts_fault_arm(fault), SALTS_OK);
        check_equal(salts_fault_disarm(fault), SALTS_OK);
        check_false(salts_fault_consume(&fault));
    }
}
