#include <salts/fastpath.h>
#include "tinytest.h"

suite("Platform static gates and fault permissions") {
    it("publishes bounded gates and rejects NULL updates") {
        SALTS_FAST_KEY(key, false);
        check_false(salts_fast_branch(&key));
        check_equal(salts_fast_enable(&key), SALTS_OK);
        check_true(salts_fast_key_read(&key));
#if SALTS_PLATFORM_NATIVE_FASTPATH
        check_true(salts_fast_key_read_native(&key));
#endif
        check_equal(salts_fast_disable(&key), SALTS_OK);
        check_false(salts_fast_key_read(&key));
#if SALTS_PLATFORM_NATIVE_FASTPATH
        check_false(salts_fast_key_read_native(&key));
#endif
        check_equal(salts_fast_key_set(NULL, true), SALTS_EINVAL);
        check_equal(salts_fast_enable(NULL), SALTS_EINVAL);
        check_equal(salts_fast_disable(NULL), SALTS_EINVAL);
    }
    it("coalesces arms and consumes exactly one permission") {
        SALTS_FAST_KEY(fault, false);
        check_false(salts_fast_key_consume(&fault));
        check_equal(salts_fast_enable(&fault), SALTS_OK);
        check_equal(salts_fast_enable(&fault), SALTS_OK);
        check_true(salts_fast_key_consume(&fault));
        check_false(salts_fast_key_consume(&fault));
        check_equal(salts_fast_enable(&fault), SALTS_OK);
        check_equal(salts_fast_disable(&fault), SALTS_OK);
        check_false(salts_fast_key_consume(&fault));
    }
}
