#include "plugin_manifest_fixture.h"
#include "tinytest.h"

enum { PLUGIN_FIXTURE_COUNT = 2u, PLUGIN_SERVICE_EXPECTED_VALUE = 111u };
static salts_plugin_registry registry;
static salts_plugin_ref refs[PLUGIN_FIXTURE_COUNT];
static salts_plugin_lease leases[PLUGIN_FIXTURE_COUNT];
static const salts_plugin_manifest *publications[PLUGIN_FIXTURE_COUNT];

static const cmeta_plugin_desc *borrow_declaration(const salts_plugin_manifest *publication) {
    const salts_plugin_export *entry = NULL;
    check_equal(salts_plugin_manifest_find_export(publication, PLUGIN_MANIFEST_EXPORT_ID, &entry), SALTS_PLUGIN_OK);
    check_equal(salts_plugin_export_require_function(entry, PLUGIN_MANIFEST_CONTRACT_ID,
        PLUGIN_MANIFEST_CONTRACT_VERSION, 0u), SALTS_PLUGIN_OK);
    check_true(cmeta_function_abi_contract_compatible(&plugin_manifest_query__function_abi_meta, entry->value.function.abi));
    const cmeta_manifest *discovery = NULL;
    check_true(entry->value.function.invoke(entry->value.function.context, &discovery, NULL, 0u));
    const cmeta_plugin_desc *declaration = NULL;
    check_equal(cmeta_manifest_get_plugin(discovery, 0u, &plugin_view_limits, &declaration), CMETA_OK);
    return declaration;
}

suite("Static declarations under Plugin-owned DSO leases") {
    before_each() {
        registry = (salts_plugin_registry){0};
        const salts_plugin_registry_config config = {PLUGIN_FIXTURE_COUNT};
        check_equal(salts_plugin_registry_init(&registry, &config), SALTS_PLUGIN_OK);
        const char *paths[] = {PLUGIN_MANIFEST_C_PATH, PLUGIN_MANIFEST_CPP_PATH};
        for (size_t i = 0u; i < PLUGIN_FIXTURE_COUNT; ++i) {
            leases[i] = (salts_plugin_lease){0};
            check_equal(salts_plugin_registry_load(&registry, paths[i], &refs[i]), SALTS_PLUGIN_OK);
            check_equal(salts_plugin_registry_start(&registry, refs[i]), SALTS_PLUGIN_OK);
            check_equal(salts_plugin_registry_acquire(&registry, refs[i], &leases[i], &publications[i]), SALTS_PLUGIN_OK);
        }
    }
    after_each() {
        for (size_t i = 0u; i < PLUGIN_FIXTURE_COUNT; ++i) {
            if (!salts_plugin_lease_valid(leases[i])) continue;
            salts_plugin_lifecycle_info info;
            check_equal(salts_plugin_registry_get_lifecycle(&registry, refs[i], &info), SALTS_PLUGIN_OK);
            if (info.state == SALTS_PLUGIN_LIFECYCLE_STARTED)
                check_equal(salts_plugin_registry_request_stop(&registry, refs[i]), SALTS_PLUGIN_OK);
            check_equal(salts_plugin_registry_release(&registry, &leases[i]), SALTS_PLUGIN_OK);
            publications[i] = NULL;
            bool quiescent = false;
            check_equal(salts_plugin_registry_poll_quiescent(&registry, refs[i], &quiescent), SALTS_PLUGIN_OK);
            check_true(quiescent);
            check_equal(salts_plugin_registry_unload(&registry, refs[i]), SALTS_PLUGIN_OK);
        }
        check_equal(salts_plugin_registry_destroy(&registry), SALTS_PLUGIN_OK);
    }
    it("reproduces the canonical vector from independent C and C++ modules") {
        const cmeta_plugin_desc *c = borrow_declaration(publications[0]);
        const cmeta_plugin_desc *cpp = borrow_declaration(publications[1]);
        check_true(c != cpp);
        check_true(c->capabilities[0].interface_desc != cpp->capabilities[0].interface_desc);
        uint64_t c_hash, cpp_hash;
        check_equal(cmeta_contract_fingerprint_plugin(c, &fingerprint_limits, &c_hash), CMETA_OK);
        check_equal(cmeta_contract_fingerprint_plugin(cpp, &fingerprint_limits, &cpp_hash), CMETA_OK);
        check_equal(c_hash, CMETA_PLUGIN_FIXTURE_GOLDEN);
        check_equal(cpp_hash, c_hash);
        for (size_t i = 0u; i < PLUGIN_FIXTURE_COUNT; ++i) {
            salts_plugin_lifecycle_info info;
            check_equal(salts_plugin_registry_get_lifecycle(&registry, refs[i], &info), SALTS_PLUGIN_OK);
            check_equal(info.active_leases, (size_t)1u);
            check_equal(info.callbacks_inflight, (size_t)0u);
            check_equal(info.state, SALTS_PLUGIN_LIFECYCLE_STARTED);
        }
    }
    it("lets the owner validate a required capability against an explicitly chosen provider") {
        const cmeta_plugin_desc *consumer = borrow_declaration(publications[0]);
        const cmeta_interface_desc *required = NULL;
        check_equal(cmeta_plugin_get_capability(consumer, 1u, CMETA_PLUGIN_REQUIRES, &plugin_view_limits, &required), CMETA_OK);
        const salts_plugin_export *entry = NULL;
        check_equal(salts_plugin_manifest_find_export(publications[1], "missing", &entry), SALTS_PLUGIN_UNKNOWN_EXPORT);
        check_true(entry == NULL);
        check_equal(salts_plugin_manifest_find_export(publications[1], PLUGIN_SERVICE_EXPORT_ID, &entry), SALTS_PLUGIN_OK);
        check_equal(salts_plugin_export_require_interface(entry, PLUGIN_SERVICE_CONTRACT_ID,
            PLUGIN_SERVICE_CONTRACT_VERSION + 1u, 0u, required), SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);
        check_equal(salts_plugin_export_require_interface(entry, PLUGIN_SERVICE_CONTRACT_ID,
            PLUGIN_SERVICE_CONTRACT_VERSION, 0u, required), SALTS_PLUGIN_OK);
        FingerprintService *handle = (FingerprintService *)entry->value.interface.value;
        check_true(FingerprintService_valid(handle));
        check_equal(FingerprintService_read(handle), (uint32_t)PLUGIN_SERVICE_EXPECTED_VALUE);
    }
    it("holds unload busy only through the original owner lease") {
        const cmeta_plugin_desc *declaration = borrow_declaration(publications[0]);
        check_equal(salts_plugin_registry_request_stop(&registry, refs[0]), SALTS_PLUGIN_OK);
        uint64_t fingerprint;
        check_equal(cmeta_contract_fingerprint_plugin(declaration, &fingerprint_limits, &fingerprint), CMETA_OK);
        check_equal(fingerprint, CMETA_PLUGIN_FIXTURE_GOLDEN);
        check_equal(salts_plugin_registry_unload(&registry, refs[0]), SALTS_PLUGIN_BUSY);
        salts_plugin_lifecycle_info info;
        check_equal(salts_plugin_registry_get_lifecycle(&registry, refs[0], &info), SALTS_PLUGIN_OK);
        check_equal(info.active_leases, (size_t)1u);
        check_equal(salts_plugin_registry_release(&registry, &leases[0]), SALTS_PLUGIN_OK);
        publications[0] = NULL;
        bool quiescent = false;
        check_equal(salts_plugin_registry_poll_quiescent(&registry, refs[0], &quiescent), SALTS_PLUGIN_OK);
        check_true(quiescent);
        check_equal(salts_plugin_registry_unload(&registry, refs[0]), SALTS_PLUGIN_OK);
        /* Only the derived value remains usable after releasing the DSO borrow. */
        check_equal(fingerprint, CMETA_PLUGIN_FIXTURE_GOLDEN);
    }
}
