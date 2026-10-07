#include "component_plugin_fixture.h"
#include "tinytest.h"

#include <salts/component_plugin.h>

static cmeta_plugin_registry registry;
static cmeta_plugin_ref ref;
static salts_component_plugin_generation generation;
static salts_component_deployment deployments[1];
static salts_component_instance instances[1];
static salts_component_dependency dependencies[1];
static size_t activation_order[1];
static salts_component_plugin_module modules[1];

suite("ComponentPlugin candidate generation") {
    before_each() {
        const cmeta_plugin_registry_config config = {1};

        generation = (salts_component_plugin_generation){0};

        check_equal(cmeta_plugin_registry_init(&registry, &config),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_load(
            &registry, COMPONENT_PROVIDER_PLUGIN_PATH, &ref),
            CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_start(&registry, ref),
                    CMETA_PLUGIN_OK);
    }

    after_each() {
        cmeta_plugin_lifecycle_info info;
        bool quiet = false;

        if (generation.state == SALTS_COMPONENT_PLUGIN_GENERATION_BUILT)
            check_equal(salts_component_plugin_generation_discard(
                &generation), SALTS_COMPONENT_PLUGIN_OK);

        check_equal(cmeta_plugin_registry_get_lifecycle(
            &registry, ref, &info), CMETA_PLUGIN_OK);
        if (info.state == CMETA_PLUGIN_LIFECYCLE_STARTED)
            check_equal(cmeta_plugin_registry_request_stop(
                &registry, ref), CMETA_PLUGIN_OK);

        check_equal(cmeta_plugin_registry_poll_quiescent(
            &registry, ref, &quiet), CMETA_PLUGIN_OK);
        check_true(quiet);
        check_equal(cmeta_plugin_registry_unload(
            &registry, ref), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_destroy(&registry), CMETA_PLUGIN_OK);
    }

    it("builds one typed Component graph under one Plugin lease") {
        const salts_component_plugin_generation_storage storage = {
            deployments, 1u,
            instances, 1u,
            dependencies, 1u,
            activation_order, 1u,
            modules, 1u
        };
        const salts_component_plugin_source source = {
            ref,
            COMPONENT_PROVIDER_EXPORT_ID,
            NULL,
            NULL
        };
        salts_component_service service;
        component_plugin_value value =
            component_plugin_value_bind(NULL, NULL);
        cmeta_plugin_lifecycle_info info;

        check_equal(salts_component_plugin_generation_build(
            &generation,
            UINT64_C(1),
            &registry,
            &storage,
            NULL, 0u,
            &source, 1u,
            NULL, 0u),
            SALTS_COMPONENT_PLUGIN_OK);

        check_equal(generation.state,
                    SALTS_COMPONENT_PLUGIN_GENERATION_BUILT);
        check_equal(generation.deployment_count, (size_t)1u);
        check_equal(generation.module_count, (size_t)1u);

        check_equal(salts_component_context_find_service(
            &generation.components,
            component_plugin_value_interface(),
            &service),
            SALTS_COMPONENT_OK);
        check_equal(component_plugin_value_borrow_from_object(
            service.object, service.interfaces, &value), CMETA_OK);
        check_equal(component_plugin_value_get(&value),
                    COMPONENT_PROVIDER_VALUE);

        check_equal(cmeta_plugin_registry_get_lifecycle(
            &registry, ref, &info), CMETA_PLUGIN_OK);
        check_equal(info.active_leases, (size_t)1u);

        check_equal(cmeta_plugin_registry_request_stop(
            &registry, ref), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_unload(
            &registry, ref), CMETA_PLUGIN_BUSY);

        check_equal(salts_component_plugin_generation_discard(
            &generation), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(generation.state,
                    SALTS_COMPONENT_PLUGIN_GENERATION_DRAINED);

        check_equal(cmeta_plugin_registry_get_lifecycle(
            &registry, ref, &info), CMETA_PLUGIN_OK);
        check_equal(info.active_leases, (size_t)0u);
    }
}
