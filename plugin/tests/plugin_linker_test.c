#include <salts/plugin_scope.h>
#include "plugin_test_interface.h"
#include "plugin_linker_fixture.h"
#include "tinytest.h"

CMETA_PLUGIN_QUERY_EXPORT const cmeta_plugin_manifest *CMETA_PLUGIN_CALL
cmeta_plugin_query(uint32_t host_abi);

typedef struct linker_scope_context {
    cmeta_plugin_registry *registry;
    cmeta_plugin_ref ref;
    unsigned calls;
    cmeta_plugin_status result;
} linker_scope_context;

static cmeta_plugin_status use_linked_exports(const cmeta_plugin_manifest *manifest, void *context) {
    linker_scope_context *state = (linker_scope_context *)context;
    const cmeta_plugin_export *entry = NULL;
    cmeta_plugin_lifecycle_info info = {0};
    int value = PLUGIN_LINKER_INPUT, result = 0;
    void *params[] = {&value};
    ++state->calls;
    if (manifest->export_count != PLUGIN_LINKER_EXPORT_COUNT ||
        cmeta_plugin_manifest_validate(manifest) != CMETA_PLUGIN_OK)
        return CMETA_PLUGIN_INVALID_MANIFEST;
    if (cmeta_plugin_registry_get_lifecycle(state->registry, state->ref, &info) != CMETA_PLUGIN_OK ||
        info.active_leases != 1u) return CMETA_PLUGIN_INVALID_STATE;
    if (cmeta_plugin_manifest_find_export(manifest,"increment",&entry) != CMETA_PLUGIN_OK ||
        !entry->value.function.invoke(entry->value.function.context,&result,params,1u) || result != value + 1)
        return CMETA_PLUGIN_UNKNOWN_EXPORT;
    if (cmeta_plugin_manifest_find_export(manifest,"double",&entry) != CMETA_PLUGIN_OK ||
        !entry->value.function.invoke(entry->value.function.context,&result,params,1u) || result != value + value)
        return CMETA_PLUGIN_UNKNOWN_EXPORT;
    if (cmeta_plugin_manifest_find_export(manifest,"codec",&entry) != CMETA_PLUGIN_OK ||
        plugin_test_codec_transform((plugin_test_codec *)entry->value.interface.value,value) !=
            value + PLUGIN_LINKER_BIAS)
        return CMETA_PLUGIN_UNKNOWN_EXPORT;
    return state->result;
}

suite("Plugin cross-TU publication and C lease scopes") {
    it("returns a stable immutable manifest only for the exact ABI") {
        const cmeta_plugin_manifest *manifest = cmeta_plugin_query(CMETA_PLUGIN_ABI_VERSION);
        check_not_null(manifest);
        check_equal(cmeta_plugin_manifest_validate(manifest),CMETA_PLUGIN_OK);
        check_equal(manifest->export_count,(size_t)PLUGIN_LINKER_EXPORT_COUNT);
        check_true(cmeta_plugin_query(CMETA_PLUGIN_ABI_VERSION) == manifest);
        check_null(cmeta_plugin_query(0u));
        check_null(cmeta_plugin_query(CMETA_PLUGIN_ABI_VERSION - 1u));
        check_null(cmeta_plugin_query(CMETA_PLUGIN_ABI_VERSION + 1u));
    }
    it("isolates discovery sets when two providers are loaded together") {
        cmeta_plugin_registry registry = {0};
        const cmeta_plugin_registry_config config = {PLUGIN_LINKER_PROVIDER_COUNT};
        cmeta_plugin_ref first = {0}, second = {0};
        cmeta_plugin_lease first_lease = {0}, second_lease = {0};
        const cmeta_plugin_manifest *first_manifest = NULL, *second_manifest = NULL;
        check_equal(cmeta_plugin_registry_init(&registry,&config),CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_load(&registry,PLUGIN_LINKER_C_PATH,&first),CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_load(&registry,PLUGIN_LINKER_CPP_PATH,&second),CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_start(&registry,first),CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_start(&registry,second),CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_acquire(&registry,first,&first_lease,&first_manifest),CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_acquire(&registry,second,&second_lease,&second_manifest),CMETA_PLUGIN_OK);
        check_true(first_manifest != second_manifest);
        check_true(first_manifest->exports != second_manifest->exports);
        check_equal(first_manifest->plugin_id,"test.plugin.linker.c");
        check_equal(second_manifest->plugin_id,"test.plugin.linker.cpp");
        check_equal(cmeta_plugin_registry_request_stop(&registry,first),CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_release(&registry,&first_lease),CMETA_PLUGIN_OK);
        first_manifest = NULL;
        bool quiet = false;
        check_equal(cmeta_plugin_registry_poll_quiescent(&registry,first,&quiet),CMETA_PLUGIN_OK);
        check_true(quiet);
        check_equal(cmeta_plugin_registry_unload(&registry,first),CMETA_PLUGIN_OK);
        linker_scope_context context = {&registry,second,0u,CMETA_PLUGIN_OK};
        check_equal(use_linked_exports(second_manifest,&context),CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_release(&registry,&second_lease),CMETA_PLUGIN_OK);
        second_manifest = NULL;
        check_equal(cmeta_plugin_registry_request_stop(&registry,second),CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_poll_quiescent(&registry,second,&quiet),CMETA_PLUGIN_OK);
        check_true(quiet);
        check_equal(cmeta_plugin_registry_unload(&registry,second),CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_destroy(&registry),CMETA_PLUGIN_OK);
    }
    it("retains C and C++ fragments under linker GC and releases early-return bodies") {
        const char *paths[] = {PLUGIN_LINKER_C_PATH,PLUGIN_LINKER_CPP_PATH};
        for (size_t index = 0u; index < sizeof(paths)/sizeof(paths[0]); ++index) {
            cmeta_plugin_registry registry = {0};
            const cmeta_plugin_registry_config config = {1u};
            cmeta_plugin_ref ref = {0};
            cmeta_plugin_lifecycle_info info = {0};
            check_equal(cmeta_plugin_registry_init(&registry,&config),CMETA_PLUGIN_OK);
            check_equal(cmeta_plugin_registry_load(&registry,paths[index],&ref),CMETA_PLUGIN_OK);
            linker_scope_context context = {&registry,ref,0u,CMETA_PLUGIN_OK};
            check_equal(cmeta_plugin_with_lease(&registry,ref,use_linked_exports,&context),
                        CMETA_PLUGIN_INVALID_STATE);
            check_equal(context.calls,0u);
            check_equal(cmeta_plugin_registry_start(&registry,ref),CMETA_PLUGIN_OK);
            check_equal(cmeta_plugin_with_lease(&registry,ref,NULL,&context),CMETA_PLUGIN_INVALID_ARGUMENT);
            check_equal(cmeta_plugin_with_lease(&registry,ref,use_linked_exports,&context),CMETA_PLUGIN_OK);
            check_equal(context.calls,1u);
            const unsigned calls_before_error = context.calls;
            context.result = CMETA_PLUGIN_INCOMPATIBLE_CONTRACT;
            check_equal(cmeta_plugin_with_lease(&registry,ref,use_linked_exports,&context),context.result);
            check_equal(context.calls,calls_before_error + 1u);
            check_equal(cmeta_plugin_registry_get_lifecycle(&registry,ref,&info),CMETA_PLUGIN_OK);
            check_equal(info.active_leases,(size_t)0u);
            check_equal(cmeta_plugin_registry_request_stop(&registry,ref),CMETA_PLUGIN_OK);
            check_equal(cmeta_plugin_with_lease(&registry,ref,use_linked_exports,&context),CMETA_PLUGIN_INVALID_STATE);
            bool quiet = false;
            check_equal(cmeta_plugin_registry_poll_quiescent(&registry,ref,&quiet),CMETA_PLUGIN_OK);
            check_true(quiet);
            check_equal(cmeta_plugin_registry_unload(&registry,ref),CMETA_PLUGIN_OK);
            check_equal(cmeta_plugin_registry_destroy(&registry),CMETA_PLUGIN_OK);
        }
    }
    it("rejects a missing fragment or wrong expected count without publishing a partial manifest") {
        const char *paths[] = {PLUGIN_LINKER_MISSING_PATH,PLUGIN_LINKER_COUNT_PATH};
        cmeta_plugin_registry registry = {0};
        const cmeta_plugin_registry_config config = {1u};
        check_equal(cmeta_plugin_registry_init(&registry,&config),CMETA_PLUGIN_OK);
        for (size_t index = 0u; index < sizeof(paths)/sizeof(paths[0]); ++index) {
            cmeta_plugin_ref ref = {0};
            check_equal(cmeta_plugin_registry_load(&registry,paths[index],&ref),CMETA_PLUGIN_QUERY_REJECTED);
            check_false(cmeta_plugin_ref_valid(ref));
            check_equal(cmeta_plugin_registry_count(&registry),(size_t)0u);
        }
        check_equal(cmeta_plugin_registry_destroy(&registry),CMETA_PLUGIN_OK);
    }
}
