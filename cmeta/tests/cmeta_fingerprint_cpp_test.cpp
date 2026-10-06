#include "cmeta_fingerprint_fixture.h"
#include "tinytest.hpp"

suite("C++ canonical fingerprint projection") {
    it("agrees with C metadata and the published v1 vectors") {
        uint64_t c_values[FP_COUNT];
        check_equal(cmeta_fingerprint_peer_values(c_values), CMETA_OK);
        check_equal(c_values, fingerprint_golden, sizeof(c_values));
        check_true(cmeta_fingerprint_peer()->type != &fingerprint_word);
        uint64_t actual;
        check_equal(cmeta_contract_fingerprint_type(&fingerprint_word, &fingerprint_limits, &actual), CMETA_OK);
        check_equal(actual, c_values[FP_TYPE]);
        check_equal(cmeta_contract_fingerprint_struct(&fingerprint_record_meta, &fingerprint_limits, &actual), CMETA_OK);
        check_equal(actual, c_values[FP_STRUCT]);
        check_equal(cmeta_contract_fingerprint_enum(FingerprintFlags_meta(), &fingerprint_limits, &actual), CMETA_OK);
        check_equal(actual, c_values[FP_ENUM]);
        check_equal(cmeta_contract_fingerprint_function((&fingerprint_transform__function_abi_meta), &fingerprint_limits, &actual), CMETA_OK);
        check_equal(actual, c_values[FP_FUNCTION]);
        check_equal(cmeta_contract_fingerprint_interface(&FingerprintService_interface_meta, &fingerprint_limits, &actual), CMETA_OK);
        check_equal(actual, c_values[FP_INTERFACE]);
        const cmeta_manifest_limits limits = {CMETA_MANIFEST_DEFAULT_ITEMS,
            CMETA_MANIFEST_DEFAULT_DEPTH, CMETA_MANIFEST_DEFAULT_NODES};
        const cmeta_enum_domain *domain = nullptr;
        check_equal(cmeta_manifest_get_enum(&fingerprint_manifest, 0u, &limits, &domain), CMETA_OK);
        check_true(domain == FingerprintFlags_meta());
    }
}
