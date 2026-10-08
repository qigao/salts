#include "component_plugin_publication_fixture.h"
#include "tinytest.h"
#include <salts/native_io_ace_token.h>

/* The ACT has NO authority to own/release this borrowed Plugin Scope.
 * The component runtime's lease stays authoritative until observed terminal
 * settlement AND explicit application release of the stable scope. */
NATIVE_IO_ACE_TOKEN_TYPE(ace_plugin_scope_act, salts_component_plugin_scope);


suite("ComponentPlugin generation publication") {

    it("pins a real provider generation across typed ACT terminal settlement") {
        const cmeta_plugin_registry_config registry_config = {1};
        cmeta_plugin_registry registry = {0};
        cmeta_plugin_ref ref;
        cmeta_plugin_lifecycle_info info;
        publication_generation_fixture generation1 = {0};
        salts_component_plugin_runtime runtime = {0};
        salts_component_plugin_scope scope = {0};
        salts_component_plugin_scope *settled_scope = NULL;
        salts_component_plugin_generation *retired = NULL;
        ace_plugin_scope_act token = {0};
        bool quiet = false;

        /* Synthetic terminal identity for this scope test. The real NativeIO
         * read/cancel terminal behavior is separately exercised by
         * native_io_test.c; this fixture tests Plugin lease ordering only. */
        const native_io_request request = {1u, 3u};
        const native_io_endpoint endpoint = {1u, 7u};
        native_io_completion completion = {0};
        completion.request = request;
        completion.endpoint = endpoint;
        completion.kind = NATIVE_IO_COMPLETION_CANCELLED;
        completion.status = SALTS_ECANCELED;
        completion.user_data = (uintptr_t)17u;

        check_equal(cmeta_plugin_registry_init(
            &registry, &registry_config), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_load(
            &registry, COMPONENT_PROVIDER_PLUGIN_PATH, &ref), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_start(&registry, ref), CMETA_PLUGIN_OK);
        check_equal(build_generation(
            &generation1, UINT64_C(11), &registry, ref), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_init(
            &runtime), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_publish(
            &runtime, &generation1.generation, &retired), SALTS_COMPONENT_PLUGIN_OK);
        check_null(retired);

        check_equal(salts_component_plugin_scope_acquire(
            &runtime, &scope), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(scope_value(&scope), COMPONENT_PROVIDER_VALUE);
        check_equal(ace_plugin_scope_act_bind(
            &token, request, endpoint, (uintptr_t)17u, &scope), SALTS_OK);
        check_equal(cmeta_plugin_registry_get_lifecycle(
            &registry, ref, &info), CMETA_PLUGIN_OK);
        check_equal(info.active_leases, (size_t)1u);

        /* No new generation admission; the existing work remains authorized.
         * Closing admission or asking for cancellation does NOT settle ACT. */
        check_equal(salts_component_plugin_runtime_close(
            &runtime, &retired), SALTS_COMPONENT_PLUGIN_OK);
        check_true(retired == &generation1.generation);
        check_equal(salts_component_plugin_generation_drain(
            &runtime, retired), SALTS_COMPONENT_PLUGIN_BUSY);
        native_io_completion stale = completion;
        ++stale.request.generation;
        check_equal(ace_plugin_scope_act_settle(
            &token, &stale, &settled_scope), SALTS_ENOENT);
        check_null(settled_scope);
        check_true(token.active);
        check_equal(scope_value(&scope), COMPONENT_PROVIDER_VALUE);
        check_equal(cmeta_plugin_registry_get_lifecycle(
            &registry, ref, &info), CMETA_PLUGIN_OK);
        check_equal(info.active_leases, (size_t)1u);

        check_equal(ace_plugin_scope_act_settle(
            &token, &completion, &settled_scope), SALTS_OK);
        check_true(settled_scope == &scope);
        check_equal(ace_plugin_scope_act_settle(
            &token, &completion, &settled_scope), SALTS_EALREADY);
        check_null(settled_scope);
        /* ACT settlement does not manufacture or release a Plugin lease. */
        check_equal(salts_component_plugin_generation_drain(
            &runtime, retired), SALTS_COMPONENT_PLUGIN_BUSY);
        check_equal(salts_component_plugin_scope_release(
            &scope), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_generation_drain(
            &runtime, retired), SALTS_COMPONENT_PLUGIN_OK);
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


    it("publishes N+1 atomically while admitted N drains") {
        const cmeta_plugin_registry_config registry_config = {1};
        cmeta_plugin_registry registry = {0};
        cmeta_plugin_ref ref;
        cmeta_plugin_lifecycle_info info;
        publication_generation_fixture g1 = {0};
        publication_generation_fixture g2 = {0};
        publication_generation_fixture g3 = {0};
        publication_generation_fixture g4 = {0};
        salts_component_plugin_runtime runtime = {0};
        salts_component_plugin_scope scope1 = {0};
        salts_component_plugin_scope scope2 = {0};
        salts_component_plugin_scope scope3 = {0};
        salts_component_plugin_scope copied_scope = {0};
        salts_component_plugin_scope reopened_scope = {0};
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

        /* A copied scope is only a stale byte-value, not a second owner. */
        copied_scope = scope1;
        check_equal(salts_component_plugin_scope_generation_id(&copied_scope),
                    UINT64_C(0));
        {
            salts_component_service copied_service;
            check_equal(salts_component_plugin_scope_find_service(
                &copied_scope, component_plugin_value_interface(), &copied_service),
                SALTS_COMPONENT_PLUGIN_INVALID_STATE);
            check_equal(salts_component_plugin_scope_find_service_from(
                &copied_scope, "ComponentPluginFixture",
                component_plugin_value_interface(), &copied_service),
                SALTS_COMPONENT_PLUGIN_INVALID_STATE);
        }
        check_equal(salts_component_plugin_scope_release(
            &copied_scope), SALTS_COMPONENT_PLUGIN_INVALID_STATE);
        check_equal(g1.generation.active_scopes, (size_t)1u);
        check_equal(runtime.active_scopes, (size_t)1u);
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
        check_equal(salts_component_plugin_scope_release(
            &copied_scope), SALTS_COMPONENT_PLUGIN_INVALID_STATE);
        check_equal(salts_component_plugin_scope_release(
            &scope1), SALTS_COMPONENT_PLUGIN_INVALID_ARGUMENT);
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

        /* close() unpublishes admission; it is not runtime_destroy(). An
         * explicit higher generation may be published after a full drain. */
        check_equal(salts_component_plugin_scope_acquire(
            &runtime, &reopened_scope), SALTS_COMPONENT_PLUGIN_INVALID_STATE);
        check_equal(build_generation(
            &g4, UINT64_C(4), &registry, ref), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_publish(
            &runtime, &g4.generation, &previous), SALTS_COMPONENT_PLUGIN_OK);
        check_null(previous);
        check_equal(salts_component_plugin_scope_acquire(
            &runtime, &reopened_scope), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_scope_generation_id(
            &reopened_scope), UINT64_C(4));
        check_equal(scope_value(&reopened_scope), COMPONENT_PROVIDER_VALUE);
        check_equal(salts_component_plugin_runtime_close(
            &runtime, &previous), SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &g4.generation);
        check_equal(salts_component_plugin_generation_drain(
            &runtime, &g4.generation), SALTS_COMPONENT_PLUGIN_BUSY);
        check_equal(salts_component_plugin_scope_release(
            &reopened_scope), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_generation_drain(
            &runtime, &g4.generation), SALTS_COMPONENT_PLUGIN_OK);

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
