#include "cmeta_plugin_fixture.h"
#include "tinytest.hpp"

suite("C++ static Plugin manifest") {
    it("shares canonical metadata and the same fingerprint with C") {
        uint64_t actual;
        check_equal(cmeta_contract_fingerprint_plugin(cmeta_plugin_meta(FingerprintProvider), &fingerprint_limits, &actual), CMETA_OK);
        check_equal(actual, CMETA_PLUGIN_FIXTURE_GOLDEN);
        check_equal(cmeta_plugin_peer_fingerprint(&actual), CMETA_OK);
        check_equal(actual, CMETA_PLUGIN_FIXTURE_GOLDEN);
        const cmeta_plugin_desc *provider = nullptr;
        check_equal(cmeta_manifest_get_plugin(cmeta_plugin_peer_manifest(), 0u, &plugin_view_limits, &provider), CMETA_OK);
        check_true(provider != cmeta_plugin_meta(FingerprintProvider));
        const cmeta_interface_desc *capability = nullptr;
        check_equal(cmeta_plugin_get_capability(provider, 0u, CMETA_PLUGIN_PROVIDES, &plugin_view_limits, &capability), CMETA_OK);
        check_true(cmeta_interface_desc_equal(capability, &FingerprintService_interface_meta));
    }
}
