#include "component_plugin_fixture.h"
#include "tinytest.h"

#include <salts/plugin.h>

static cmeta_plugin_registry registry;
static cmeta_plugin_ref ref;
static cmeta_plugin_lease lease;
static const cmeta_plugin_manifest *manifest;
static salts_component_provider *provider;
static const salts_component_provider_binding *binding;

suite("ComponentPlugin provider DSO") {
    before_each() {
        const cmeta_plugin_registry_config config = {1};
        const cmeta_plugin_export *entry = NULL;

        manifest = NULL;
        provider = NULL;
        binding = NULL;

        check_equal(cmeta_plugin_registry_init(&registry, &config),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_load(
            &registry, COMPONENT_PROVIDER_PLUGIN_PATH, &ref),
            CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_start(&registry, ref),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_acquire(
            &registry, ref, &lease, &manifest),
            CMETA_PLUGIN_OK);

        check_equal(cmeta_plugin_manifest_find_export(
            manifest, COMPONENT_PROVIDER_EXPORT_ID, &entry),
            CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_export_require_interface(
            entry,
            SALTS_COMPONENT_PROVIDER_CONTRACT_ID,
            SALTS_COMPONENT_PROVIDER_CONTRACT_VERSION,
            0u,
            salts_component_provider_interface()),
            CMETA_PLUGIN_OK);

        provider =
            (salts_component_provider *)entry->value.interface.value;
        check_not_null(provider);
        check_true(salts_component_provider_valid(provider));

        binding = salts_component_provider_get_binding(provider);
        check_not_null(binding);
        check_true(salts_component_provider_binding_valid(binding));
        check_equal(binding->component->stable_id, "ComponentPluginFixture");
    }

    after_each() {
        cmeta_plugin_lifecycle_info info;
        bool quiet = false;

        binding = NULL;
        provider = NULL;
        manifest = NULL;

        check_equal(cmeta_plugin_registry_get_lifecycle(
            &registry, ref, &info), CMETA_PLUGIN_OK);
        if (info.state == CMETA_PLUGIN_LIFECYCLE_STARTED)
            check_equal(cmeta_plugin_registry_request_stop(
                &registry, ref), CMETA_PLUGIN_OK);

        check_equal(cmeta_plugin_registry_release(
            &registry, &lease), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_poll_quiescent(
            &registry, ref, &quiet), CMETA_PLUGIN_OK);
        check_true(quiet);
        check_equal(cmeta_plugin_registry_unload(
            &registry, ref), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_destroy(&registry), CMETA_PLUGIN_OK);
    }

    it("keeps the provider binding borrowed under the Plugin lease") {
        cmeta_plugin_lifecycle_info info;

        check_equal(cmeta_plugin_registry_get_lifecycle(
            &registry, ref, &info), CMETA_PLUGIN_OK);
        check_equal(info.active_leases, (size_t)1u);

        check_equal(cmeta_plugin_registry_request_stop(
            &registry, ref), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_unload(
            &registry, ref), CMETA_PLUGIN_BUSY);
        check_true(salts_component_provider_binding_valid(binding));
    }
}
