#include "cmeta_plugin_fixture.h"
#include "tinytest.h"

suite("Static Plugin declaration over canonical capabilities") {
    it("borrows typed role rows and agrees with the v1 vector across TUs") {
        const cmeta_plugin_desc *provider = NULL, *peer = NULL;
        check_equal(cmeta_manifest_get_plugin(&plugin_discovery, 0u, &plugin_view_limits, &provider), CMETA_OK);
        check_equal(cmeta_manifest_get_plugin(cmeta_plugin_peer_manifest(), 0u, &plugin_view_limits, &peer), CMETA_OK);
        check_true(provider != peer);
        check_equal(provider->count, (size_t)2u);
        const cmeta_interface_desc *capability = NULL;
        check_equal(cmeta_plugin_get_capability(provider, 0u, CMETA_PLUGIN_PROVIDES, &plugin_view_limits, &capability), CMETA_OK);
        check_true(capability == &FingerprintService_interface_meta);
        check_equal(cmeta_plugin_get_capability(peer, 1u, CMETA_PLUGIN_REQUIRES, &plugin_view_limits, &capability), CMETA_OK);
        check_true(cmeta_interface_desc_equal(capability, &FingerprintService_interface_meta));
        uint64_t local, remote;
        check_equal(cmeta_contract_fingerprint_plugin(provider, &fingerprint_limits, &local), CMETA_OK);
        check_equal(cmeta_plugin_peer_fingerprint(&remote), CMETA_OK);
        check_equal(local, CMETA_PLUGIN_FIXTURE_GOLDEN);
        check_equal(remote, local);
    }
    it("uses ordered membership and semantic interface rows without display-name identity") {
        cmeta_plugin_desc provider = *cmeta_plugin_meta(FingerprintProvider);
        uint64_t out;
        provider.name = "PresentationProvider";
        check_equal(cmeta_contract_fingerprint_plugin(&provider, &fingerprint_limits, &out), CMETA_OK);
        check_equal(out, CMETA_PLUGIN_FIXTURE_GOLDEN);
        cmeta_plugin_capability swapped[] = {provider.capabilities[1], provider.capabilities[0]};
        provider.capabilities = swapped;
        check_equal(cmeta_contract_fingerprint_plugin(&provider, &fingerprint_limits, &out), CMETA_OK);
        check_not_equal(out, CMETA_PLUGIN_FIXTURE_GOLDEN);
        swapped[0].role = CMETA_PLUGIN_PROVIDES;
        swapped[1].role = CMETA_PLUGIN_REQUIRES;
        cmeta_interface_desc capability = FingerprintService_interface_meta;
        cmeta_interface_method_desc method = capability.methods[0];
        method.flags = CMETA_INTERFACE_METHOD_OWNS_SELF;
        capability.methods = &method;
        swapped[0].interface_desc = &capability;
        check_equal(cmeta_contract_fingerprint_plugin(&provider, &fingerprint_limits, &out), CMETA_OK);
        check_not_equal(out, CMETA_PLUGIN_FIXTURE_GOLDEN);
    }
    it("rejects malformed declaration, role, kind and index without publishing output") {
        cmeta_plugin_desc provider = *cmeta_plugin_meta(FingerprintProvider);
        cmeta_manifest_entry entry = plugin_discovery.entries[0];
        entry.descriptor = &provider;
        cmeta_manifest manifest = plugin_discovery;
        manifest.entries = &entry;
        const cmeta_plugin_desc *out = cmeta_plugin_meta(FingerprintProvider);
        provider.size = 0u;
        check_equal(cmeta_manifest_get_plugin(&manifest, 0u, &plugin_view_limits, &out), CMETA_INVALID_ARGUMENT);
        provider = *out;
        ++provider.format_version;
        check_equal(cmeta_manifest_get_plugin(&manifest, 0u, &plugin_view_limits, &out), CMETA_TYPE_MISMATCH);
        provider = *out;
        provider.capabilities = NULL;
        check_equal(cmeta_manifest_get_plugin(&manifest, 0u, &plugin_view_limits, &out), CMETA_INVALID_ARGUMENT);
        entry.kind = CMETA_MANIFEST_CAPABILITY;
        check_equal(cmeta_manifest_get_plugin(&manifest, 0u, &plugin_view_limits, &out), CMETA_TYPE_MISMATCH);
        const cmeta_interface_desc *capability = &FingerprintService_interface_meta;
        check_equal(cmeta_plugin_get_capability(out, 0u, CMETA_PLUGIN_REQUIRES, &plugin_view_limits, &capability), CMETA_TYPE_MISMATCH);
        check_equal(cmeta_plugin_get_capability(out, out->count, CMETA_PLUGIN_PROVIDES, &plugin_view_limits, &capability), CMETA_INVALID_ARGUMENT);
        check_equal(cmeta_plugin_get_capability(NULL, 0u, CMETA_PLUGIN_PROVIDES, &plugin_view_limits, &capability), CMETA_INVALID_ARGUMENT);
        check_equal(cmeta_plugin_get_capability(out, 0u, CMETA_PLUGIN_PROVIDES, NULL, &capability), CMETA_INVALID_ARGUMENT);
        check_equal(cmeta_plugin_get_capability(out, 0u, CMETA_PLUGIN_PROVIDES, &plugin_view_limits, NULL), CMETA_INVALID_ARGUMENT);
        check_true(capability == &FingerprintService_interface_meta);
        check_true(out == cmeta_plugin_meta(FingerprintProvider));
    }
    it("bounds aggregate fingerprints and canonical identity traversal across all capabilities") {
        uint64_t out = CMETA_PLUGIN_FIXTURE_GOLDEN;
        cmeta_plugin_desc provider = *cmeta_plugin_meta(FingerprintProvider);
        cmeta_fingerprint_limits limits = fingerprint_limits;
        limits.max_rows = provider.count;
        check_equal(cmeta_contract_fingerprint_plugin(&provider, &limits, &out), CMETA_CAPACITY_EXCEEDED);
        limits = fingerprint_limits;
        limits.max_string_bytes = sizeof("FingerprintProvider");
        check_equal(cmeta_contract_fingerprint_plugin(&provider, &limits, &out), CMETA_CAPACITY_EXCEEDED);
        cmeta_plugin_capability row = provider.capabilities[0];
        provider.capabilities = &row;
        provider.count = 1u;
        row.role = (cmeta_plugin_role)0;
        check_equal(cmeta_contract_fingerprint_plugin(&provider, &fingerprint_limits, &out), CMETA_INVALID_ARGUMENT);
        row = cmeta_plugin_meta(FingerprintProvider)->capabilities[0];
        row.interface_desc = NULL;
        check_equal(cmeta_contract_fingerprint_plugin(&provider, &fingerprint_limits, &out), CMETA_INVALID_ARGUMENT);
        row = cmeta_plugin_meta(FingerprintProvider)->capabilities[0];
        cmeta_interface_desc interface_desc = *row.interface_desc;
        cmeta_interface_method_desc method = interface_desc.methods[0];
        method.function = NULL;
        method.abi = NULL;
        interface_desc.methods = &method;
        row.interface_desc = &interface_desc;
        check_equal(cmeta_contract_fingerprint_plugin(&provider, &fingerprint_limits, &out), CMETA_TYPE_MISMATCH);
        ++provider.format_version;
        check_equal(cmeta_contract_fingerprint_plugin(&provider, &fingerprint_limits, &out), CMETA_TYPE_MISMATCH);
        check_equal(cmeta_contract_fingerprint_plugin(NULL, &fingerprint_limits, &out), CMETA_INVALID_ARGUMENT);
        check_equal(out, CMETA_PLUGIN_FIXTURE_GOLDEN);
    }
    it("bounds complete role views and does not publish malformed interface rows") {
        const cmeta_interface_desc *out = &FingerprintService_interface_meta;
        cmeta_manifest_limits limits = plugin_view_limits;
        limits.max_identity_nodes = 1u;
        check_equal(cmeta_plugin_get_capability(cmeta_plugin_meta(FingerprintProvider),
            0u, CMETA_PLUGIN_PROVIDES, &limits, &out), CMETA_CAPACITY_EXCEEDED);
        cmeta_plugin_desc provider = *cmeta_plugin_meta(FingerprintProvider);
        limits = plugin_view_limits;
        limits.max_items = 1u;
        check_equal(cmeta_plugin_get_capability(&provider, 0u, CMETA_PLUGIN_PROVIDES,
            &limits, &out), CMETA_CAPACITY_EXCEEDED);
        limits = plugin_view_limits;
        limits.max_items = SIZE_MAX;
        provider.count = SIZE_MAX;
        check_equal(cmeta_plugin_get_capability(&provider, 0u, CMETA_PLUGIN_PROVIDES,
            &limits, &out), CMETA_INVALID_ARGUMENT);
        provider = *cmeta_plugin_meta(FingerprintProvider);
        cmeta_plugin_capability row = provider.capabilities[0];
        provider.capabilities = &row;
        provider.count = 1u;
        row.interface_desc = NULL;
        check_equal(cmeta_plugin_get_capability(&provider, 0u, CMETA_PLUGIN_PROVIDES,
            &plugin_view_limits, &out), CMETA_INVALID_ARGUMENT);
        check_true(out == &FingerprintService_interface_meta);
    }
}
