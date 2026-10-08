#include "component_plugin_publication_fixture.h"
#include "tinytest.h"

suite("ComponentPlugin generation publication") {
    it("publishes N+1 atomically while admitted N drains") {
        const cmeta_plugin_registry_config registry_config = {1};
        cmeta_plugin_registry registry = {0};
        cmeta_plugin_ref ref;
        cmeta_plugin_lifecycle_info info;
        publication_generation_fixture g1 = {0};
        publication_generation_fixture g2 = {0};
        publication_generation_fixture g3 = {0};
        salts_component_plugin_runtime runtime = {0};
        salts_component_plugin_scope scope1 = {0};
        salts_component_plugin_scope scope2 = {0};
        salts_component_plugin_scope scope3 = {0};
        salts_component_plugin_generation *previous = NULL;
        bool quiet = false;

        check_equal(cmeta_plugin_registry_init(
            &registry, &registry_config), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_load(
            &registry, COMPONENT_PROVIDER_PLUGIN_PATH, &ref),
            CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_start(
            &registry, ref), CMETA_PLUGIN_OK);

        check_equal(build_generation(
            &g1, UINT64_C(1), &registry, ref),
            SALTS_COMPONENT_PLUGIN_OK);
        check_equal(build_generation(
            &g2, UINT64_C(2), &registry, ref),
            SALTS_COMPONENT_PLUGIN_OK);

        check_equal(cmeta_plugin_registry_get_lifecycle(
            &registry, ref, &info), CMETA_PLUGIN_OK);
        check_equal(info.active_leases, (size_t)2u);

        check_equal(salts_component_plugin_runtime_init(
            &runtime), SALTS_COMPONENT_PLUGIN_OK);

        check_equal(salts_component_plugin_runtime_publish(
            &runtime, &g1.generation, &previous),
            SALTS_COMPONENT_PLUGIN_OK);
        check_null(previous);

        {
            salts_component_plugin_generation stale =
                SALTS_COMPONENT_PLUGIN_GENERATION_INIT;
            const salts_component_plugin_generation_storage empty_storage = {0};

            check_equal(salts_component_plugin_generation_build(
                &stale,
                UINT64_C(1),
                NULL,
                &empty_storage,
                NULL, 0u,
                NULL, 0u,
                NULL, 0u),
                SALTS_COMPONENT_PLUGIN_OK);
            check_equal(salts_component_plugin_runtime_publish(
                &runtime, &stale, &previous),
                SALTS_COMPONENT_PLUGIN_INVALID_STATE);
            check_equal(salts_component_plugin_generation_discard(
                &stale), SALTS_COMPONENT_PLUGIN_OK);
        }

        check_equal(salts_component_plugin_scope_acquire(
            &runtime, &scope1), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_scope_generation_id(&scope1),
                    UINT64_C(1));
        check_equal(scope_value(&scope1), COMPONENT_PROVIDER_VALUE);

        check_equal(salts_component_plugin_runtime_publish(
            &runtime, &g2.generation, &previous),
            SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &g1.generation);
        check_equal(g1.generation.state,
                    SALTS_COMPONENT_PLUGIN_GENERATION_DRAINING);

        check_equal(salts_component_plugin_scope_acquire(
            &runtime, &scope2), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_scope_generation_id(&scope2),
                    UINT64_C(2));
        check_equal(scope_value(&scope2), COMPONENT_PROVIDER_VALUE);

        check_equal(build_generation(
            &g3, UINT64_C(3), &registry, ref),
            SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_publish(
            &runtime, &g3.generation, &previous),
            SALTS_COMPONENT_PLUGIN_BUSY);
        check_true(previous == &g1.generation);
        check_equal(g2.generation.state,
                    SALTS_COMPONENT_PLUGIN_GENERATION_PUBLISHED);
        check_equal(g3.generation.state,
                    SALTS_COMPONENT_PLUGIN_GENERATION_BUILT);

        /* Existing admitted work remains on generation 1. */
        check_equal(salts_component_plugin_scope_generation_id(&scope1),
                    UINT64_C(1));
        check_equal(scope_value(&scope1), COMPONENT_PROVIDER_VALUE);

        check_equal(salts_component_plugin_generation_drain(
            &runtime, &g1.generation), SALTS_COMPONENT_PLUGIN_BUSY);
        check_equal(salts_component_plugin_scope_release(
            &scope1), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_generation_drain(
            &runtime, &g1.generation), SALTS_COMPONENT_PLUGIN_OK);

        check_equal(cmeta_plugin_registry_get_lifecycle(
            &registry, ref, &info), CMETA_PLUGIN_OK);
        check_equal(info.active_leases, (size_t)2u);

        check_equal(salts_component_plugin_runtime_publish(
            &runtime, &g3.generation, &previous),
            SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &g2.generation);
        check_equal(g2.generation.state,
                    SALTS_COMPONENT_PLUGIN_GENERATION_DRAINING);

        check_equal(salts_component_plugin_scope_acquire(
            &runtime, &scope3), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_scope_generation_id(&scope3),
                    UINT64_C(3));
        check_equal(scope_value(&scope3), COMPONENT_PROVIDER_VALUE);

        check_equal(salts_component_plugin_scope_generation_id(&scope2),
                    UINT64_C(2));
        check_equal(scope_value(&scope2), COMPONENT_PROVIDER_VALUE);

        check_equal(salts_component_plugin_generation_drain(
            &runtime, &g2.generation), SALTS_COMPONENT_PLUGIN_BUSY);
        check_equal(salts_component_plugin_scope_release(
            &scope2), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_generation_drain(
            &runtime, &g2.generation), SALTS_COMPONENT_PLUGIN_OK);

        check_equal(cmeta_plugin_registry_get_lifecycle(
            &registry, ref, &info), CMETA_PLUGIN_OK);
        check_equal(info.active_leases, (size_t)1u);

        check_equal(salts_component_plugin_runtime_close(
            &runtime, &previous), SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &g3.generation);
        check_equal(salts_component_plugin_generation_drain(
            &runtime, &g3.generation), SALTS_COMPONENT_PLUGIN_BUSY);

        check_equal(salts_component_plugin_scope_release(
            &scope3), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_generation_drain(
            &runtime, &g3.generation), SALTS_COMPONENT_PLUGIN_OK);

        check_equal(cmeta_plugin_registry_get_lifecycle(
            &registry, ref, &info), CMETA_PLUGIN_OK);
        check_equal(info.active_leases, (size_t)0u);

        check_equal(salts_component_plugin_runtime_destroy(
            &runtime), SALTS_COMPONENT_PLUGIN_OK);

        check_equal(cmeta_plugin_registry_request_stop(
            &registry, ref), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_poll_quiescent(
            &registry, ref, &quiet), CMETA_PLUGIN_OK);
        check_true(quiet);
        check_equal(cmeta_plugin_registry_unload(
            &registry, ref), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_destroy(
            &registry), CMETA_PLUGIN_OK);
    }
}
