#include <salts/plugin_scope.h>
#include "plugin_test_interface.h"
#include "plugin_linker_fixture.h"
#include "tinytest.h"

SALTS_PLUGIN_QUERY_EXPORT const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi);

typedef struct linker_scope_context {
    salts_plugin_registry *registry;
    salts_plugin_ref ref;
    unsigned calls;
    salts_plugin_status result;
} linker_scope_context;

static salts_plugin_status use_linked_exports(const salts_plugin_manifest *manifest, void *context) {
    linker_scope_context *state = (linker_scope_context *)context;
    const salts_plugin_export *entry = NULL;
    salts_plugin_lifecycle_info info = {0};
    int value = PLUGIN_LINKER_INPUT, result = 0;
    void *params[] = {&value};
    ++state->calls;
    if (manifest->export_count != PLUGIN_LINKER_EXPORT_COUNT ||
        salts_plugin_manifest_validate(manifest) != SALTS_PLUGIN_OK)
        return SALTS_PLUGIN_INVALID_MANIFEST;
    if (salts_plugin_registry_get_lifecycle(state->registry, state->ref, &info) != SALTS_PLUGIN_OK ||
        info.active_leases != 1u) return SALTS_PLUGIN_INVALID_STATE;
    if (salts_plugin_manifest_find_export(manifest,"increment",&entry) != SALTS_PLUGIN_OK ||
        !entry->value.function.invoke(entry->value.function.context,&result,params,1u) || result != value + 1)
        return SALTS_PLUGIN_UNKNOWN_EXPORT;
    if (salts_plugin_manifest_find_export(manifest,"double",&entry) != SALTS_PLUGIN_OK ||
        !entry->value.function.invoke(entry->value.function.context,&result,params,1u) || result != value + value)
        return SALTS_PLUGIN_UNKNOWN_EXPORT;
    if (salts_plugin_manifest_find_export(manifest,"codec",&entry) != SALTS_PLUGIN_OK ||
        plugin_test_codec_transform((plugin_test_codec *)entry->value.interface.value,value) !=
            value + PLUGIN_LINKER_BIAS)
        return SALTS_PLUGIN_UNKNOWN_EXPORT;
    return state->result;
}

suite("Plugin cross-TU publication and C lease scopes") {
    it("returns a stable immutable manifest only for the exact ABI") {
        const salts_plugin_manifest *manifest = salts_plugin_query(SALTS_PLUGIN_ABI_VERSION);
        check_not_null(manifest);
        check_equal(salts_plugin_manifest_validate(manifest),SALTS_PLUGIN_OK);
        check_equal(manifest->export_count,(size_t)PLUGIN_LINKER_EXPORT_COUNT);
        check_equal(salts_plugin_query(SALTS_PLUGIN_ABI_VERSION),manifest);
        check_null(salts_plugin_query(0u));
        check_null(salts_plugin_query(SALTS_PLUGIN_ABI_VERSION - 1u));
        check_null(salts_plugin_query(SALTS_PLUGIN_ABI_VERSION + 1u));
    }
    it("isolates discovery sets when two providers are loaded together") {
        salts_plugin_registry registry = {0};
        const salts_plugin_registry_config config = {PLUGIN_LINKER_PROVIDER_COUNT};
        salts_plugin_ref first = {0}, second = {0};
        salts_plugin_lease first_lease = {0}, second_lease = {0};
        const salts_plugin_manifest *first_manifest = NULL, *second_manifest = NULL;
        check_equal(salts_plugin_registry_init(&registry,&config),SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_load(&registry,PLUGIN_LINKER_C_PATH,&first),SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_load(&registry,PLUGIN_LINKER_CPP_PATH,&second),SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_start(&registry,first),SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_start(&registry,second),SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_acquire(&registry,first,&first_lease,&first_manifest),SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_acquire(&registry,second,&second_lease,&second_manifest),SALTS_PLUGIN_OK);
        check_not_equal(first_manifest,second_manifest);
        check_not_equal(first_manifest->exports,second_manifest->exports);
        check_equal(first_manifest->plugin_id,"test.plugin.linker.c");
        check_equal(second_manifest->plugin_id,"test.plugin.linker.cpp");
        check_equal(salts_plugin_registry_request_stop(&registry,first),SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_release(&registry,&first_lease),SALTS_PLUGIN_OK);
        first_manifest = NULL;
        bool quiet = false;
        check_equal(salts_plugin_registry_poll_quiescent(&registry,first,&quiet),SALTS_PLUGIN_OK);
        check_true(quiet);
        check_equal(salts_plugin_registry_unload(&registry,first),SALTS_PLUGIN_OK);
        linker_scope_context context = {&registry,second,0u,SALTS_PLUGIN_OK};
        check_equal(use_linked_exports(second_manifest,&context),SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_release(&registry,&second_lease),SALTS_PLUGIN_OK);
        second_manifest = NULL;
        check_equal(salts_plugin_registry_request_stop(&registry,second),SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_poll_quiescent(&registry,second,&quiet),SALTS_PLUGIN_OK);
        check_true(quiet);
        check_equal(salts_plugin_registry_unload(&registry,second),SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_destroy(&registry),SALTS_PLUGIN_OK);
    }
    it("retains C and C++ fragments under linker GC and releases early-return bodies") {
        const char *paths[] = {PLUGIN_LINKER_C_PATH,PLUGIN_LINKER_CPP_PATH};
        for (size_t index = 0u; index < sizeof(paths)/sizeof(paths[0]); ++index) {
            salts_plugin_registry registry = {0};
            const salts_plugin_registry_config config = {1u};
            salts_plugin_ref ref = {0};
            salts_plugin_lifecycle_info info = {0};
            check_equal(salts_plugin_registry_init(&registry,&config),SALTS_PLUGIN_OK);
            check_equal(salts_plugin_registry_load(&registry,paths[index],&ref),SALTS_PLUGIN_OK);
            linker_scope_context context = {&registry,ref,0u,SALTS_PLUGIN_OK};
            check_equal(salts_plugin_with_lease(&registry,ref,use_linked_exports,&context),
                        SALTS_PLUGIN_INVALID_STATE);
            check_equal(context.calls,0u);
            check_equal(salts_plugin_registry_start(&registry,ref),SALTS_PLUGIN_OK);
            check_equal(salts_plugin_with_lease(&registry,ref,NULL,&context),SALTS_PLUGIN_INVALID_ARGUMENT);
            check_equal(salts_plugin_with_lease(&registry,ref,use_linked_exports,&context),SALTS_PLUGIN_OK);
            check_equal(context.calls,1u);
            const unsigned calls_before_error = context.calls;
            context.result = SALTS_PLUGIN_INCOMPATIBLE_CONTRACT;
            check_equal(salts_plugin_with_lease(&registry,ref,use_linked_exports,&context),context.result);
            check_equal(context.calls,calls_before_error + 1u);
            check_equal(salts_plugin_registry_get_lifecycle(&registry,ref,&info),SALTS_PLUGIN_OK);
            check_equal(info.active_leases,(size_t)0u);
            check_equal(salts_plugin_registry_request_stop(&registry,ref),SALTS_PLUGIN_OK);
            check_equal(salts_plugin_with_lease(&registry,ref,use_linked_exports,&context),SALTS_PLUGIN_INVALID_STATE);
            bool quiet = false;
            check_equal(salts_plugin_registry_poll_quiescent(&registry,ref,&quiet),SALTS_PLUGIN_OK);
            check_true(quiet);
            check_equal(salts_plugin_registry_unload(&registry,ref),SALTS_PLUGIN_OK);
            check_equal(salts_plugin_registry_destroy(&registry),SALTS_PLUGIN_OK);
        }
    }
    it("rejects a missing fragment or wrong expected count without publishing a partial manifest") {
        const char *paths[] = {PLUGIN_LINKER_MISSING_PATH,PLUGIN_LINKER_COUNT_PATH};
        salts_plugin_registry registry = {0};
        const salts_plugin_registry_config config = {1u};
        check_equal(salts_plugin_registry_init(&registry,&config),SALTS_PLUGIN_OK);
        for (size_t index = 0u; index < sizeof(paths)/sizeof(paths[0]); ++index) {
            salts_plugin_ref ref = {0};
            check_equal(salts_plugin_registry_load(&registry,paths[index],&ref),SALTS_PLUGIN_QUERY_REJECTED);
            check_false(salts_plugin_ref_valid(ref));
            check_equal(salts_plugin_registry_count(&registry),(size_t)0u);
        }
        check_equal(salts_plugin_registry_destroy(&registry),SALTS_PLUGIN_OK);
    }
}
