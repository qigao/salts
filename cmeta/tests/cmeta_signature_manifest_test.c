#include <cmeta/cmeta.h>
#include "tinytest.h"

#define CMETA_TEST_COUNT_U(in, ret) +1
#define CMETA_TEST_COUNT_B(left, right, ret) +1
#define CMETA_TEST_COUNT_G(in, out) +1
enum {
    CMETA_TEST_VALUE_SIGNATURE_COUNT =
        0 CMETA_VALUE_SIGNATURES(CMETA_TEST_COUNT_U, CMETA_TEST_COUNT_B),
    CMETA_TEST_GENERATOR_SIGNATURE_COUNT =
        0 CMETA_GENERATOR_SIGNATURES(CMETA_TEST_COUNT_G)
};
#undef CMETA_TEST_COUNT_U
#undef CMETA_TEST_COUNT_B
#undef CMETA_TEST_COUNT_G

suite("CMeta generated signature manifest") {
    it("keeps the finite built-in manifest aligned with the default ABI") {
        check_equal(CMETA_SIGNATURE_PROFILE_NAME, "relation");
        check_equal((size_t)CMETA_BUILTIN_TYPE_COUNT, (size_t)5u);
        check_equal((size_t)CMETA_BUILTIN_UNARY_RELATION_COUNT, (size_t)8u);
        check_equal((size_t)CMETA_BUILTIN_BINARY_RELATION_COUNT, (size_t)2u);
        check_equal((size_t)CMETA_BUILTIN_GENERATOR_RELATION_COUNT, (size_t)1u);
        check_equal((size_t)CMETA_SIG_COUNT, (size_t)12u);
        check_equal(cmeta_sig_to_symbol(CMETA_SIG_B_L_L_L),
                    "CMETA_SIG_B_L_L_L");
        check_equal(cmeta_sig_to_symbol(CMETA_SIG_B_L_D_D),
                    "CMETA_SIG_B_L_D_D");
    }

    it("groups finite signatures by callable protocol") {
        check_equal((size_t)CMETA_TEST_VALUE_SIGNATURE_COUNT, (size_t)10u);
        check_equal((size_t)CMETA_TEST_GENERATOR_SIGNATURE_COUNT, (size_t)1u);
    }
}
