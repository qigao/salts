#include <cmeta/meta.h>
#include <cmeta/manifest_view.h>
#include "tinytest.hpp"

#if defined(SALTS_FASTPATH_H) || defined(CMETA_TRACE_H) || defined(CMETA_SCOPE_H) || \
    defined(CMETA_POOL_H) || defined(CMETA_LOCAL_H) || defined(SALTS_THREAD_PRIMITIVES_H) || \
    defined(SALTS_OBJECT_POOL_H) || defined(SALTS_ATOMIC_H) || defined(SALTS_RCU_H)
#error "Reflection-only package consumers must not include optional runtime facades"
#endif
cmeta_registry(installed_cpp_manifest, cmeta_manifest_type_entry("int", &cmeta_type_int));
#define INSTALLED_CAPABILITY_METHODS(X,I) \
    X(I,FV0,void,reset,stateful,&cmeta_type_void,CMETA_ABI_VOID)
CMETA_INTERFACE(InstalledCapability, INSTALLED_CAPABILITY_METHODS);
cmeta_plugin(InstalledProvider, cmeta_provides(InstalledCapability));
cmeta_registry(installed_plugin_discovery,
    cmeta_manifest_plugin_entry("provider", cmeta_plugin_meta(InstalledProvider)));
suite("Installed C++ CMeta Reflection without runtime adapters") {
    it("borrows semantic type metadata from the installed C ABI") {
        const cmeta_manifest_limits limits = {CMETA_MANIFEST_DEFAULT_ITEMS,
            CMETA_MANIFEST_DEFAULT_DEPTH, CMETA_MANIFEST_DEFAULT_NODES};
        const cmeta_type_desc *type = nullptr;
        check_equal(cmeta_manifest_get_type(&installed_cpp_manifest, 0u, &limits, &type), CMETA_OK);
        check_true(cmeta_type_equal(type, &cmeta_type_int));
        check_equal(type->size, sizeof(int));
    }
    it("computes a canonical type contract through the installed core archive") {
        const cmeta_fingerprint_limits limits = {CMETA_FINGERPRINT_DEFAULT_DEPTH,
            CMETA_FINGERPRINT_DEFAULT_NODES, CMETA_FINGERPRINT_DEFAULT_ROWS,
            CMETA_FINGERPRINT_DEFAULT_STRING_BYTES};
        uint64_t original, renamed;
        check_equal(cmeta_contract_fingerprint_type(&cmeta_type_int, &limits, &original), CMETA_OK);
        cmeta_type_desc type = cmeta_type_int;
        type.name = "InstalledDisplayName";
        check_equal(cmeta_contract_fingerprint_type(&type, &limits, &renamed), CMETA_OK);
        check_equal(original, renamed);
    }
    it("borrows and fingerprints a static Plugin declaration through core CMeta") {
        const cmeta_manifest_limits view_limits = {CMETA_MANIFEST_DEFAULT_ITEMS,
            CMETA_MANIFEST_DEFAULT_DEPTH, CMETA_MANIFEST_DEFAULT_NODES};
        const cmeta_fingerprint_limits limits = {CMETA_FINGERPRINT_DEFAULT_DEPTH,
            CMETA_FINGERPRINT_DEFAULT_NODES, CMETA_FINGERPRINT_DEFAULT_ROWS,
            CMETA_FINGERPRINT_DEFAULT_STRING_BYTES};
        const cmeta_plugin_desc *provider = nullptr;
        check_equal(cmeta_manifest_get_plugin(&installed_plugin_discovery, 0u,
            &view_limits, &provider), CMETA_OK);
        check_equal(provider->count, size_t{1u});
        const cmeta_interface_desc *capability = nullptr;
        check_equal(cmeta_plugin_get_capability(provider, 0u, CMETA_PLUGIN_PROVIDES,
            &view_limits, &capability), CMETA_OK);
        check_true(capability == &InstalledCapability_interface_meta);
        uint64_t fingerprint;
        check_equal(cmeta_contract_fingerprint_plugin(provider, &limits, &fingerprint), CMETA_OK);
    }

}
