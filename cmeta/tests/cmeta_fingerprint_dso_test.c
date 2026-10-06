#include "cmeta_fingerprint_fixture.h"
#include "tinytest.h"

suite("CMeta fingerprints across native images") {
    it("negotiates Reflection epoch before borrowing independent DSO metadata") {
        const cmeta_fingerprint_fixture *provider = cmeta_fingerprint_dso_query(CMETA_REFLECTION_ABI_VERSION);
        check_not_null(provider);
        check_true(provider->type != fingerprint_fixture.type);
        check_true(provider->structure != fingerprint_fixture.structure);
        uint64_t provider_values[FP_COUNT];
        check_equal(cmeta_fingerprint_dso_values(CMETA_REFLECTION_ABI_VERSION, provider_values), CMETA_OK);
        check_equal(provider_values, fingerprint_golden, sizeof(provider_values));
        /* The linked fixture stays loaded for the process lifetime. Metadata is
         * borrowed; hashing never performs a loader or retention operation. */
        uint64_t actual;
        check_equal(cmeta_contract_fingerprint_type(provider->type, &fingerprint_limits, &actual), CMETA_OK);
        check_equal(actual, provider_values[FP_TYPE]);
        check_equal(cmeta_contract_fingerprint_struct(provider->structure, &fingerprint_limits, &actual), CMETA_OK);
        check_equal(actual, provider_values[FP_STRUCT]);
        check_equal(cmeta_contract_fingerprint_enum(provider->enumeration, &fingerprint_limits, &actual), CMETA_OK);
        check_equal(actual, provider_values[FP_ENUM]);
        check_equal(cmeta_contract_fingerprint_function(provider->function, &fingerprint_limits, &actual), CMETA_OK);
        check_equal(actual, provider_values[FP_FUNCTION]);
        check_equal(cmeta_contract_fingerprint_interface(provider->interface_desc, &fingerprint_limits, &actual), CMETA_OK);
        check_equal(actual, provider_values[FP_INTERFACE]);
        check_true(cmeta_fingerprint_dso_query(CMETA_REFLECTION_ABI_VERSION + 1u) == NULL);
        check_equal(cmeta_fingerprint_dso_values(CMETA_REFLECTION_ABI_VERSION + 1u, provider_values), CMETA_TYPE_MISMATCH);
        check_equal(provider_values, fingerprint_golden, sizeof(provider_values));
    }
}
