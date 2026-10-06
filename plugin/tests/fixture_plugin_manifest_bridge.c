#include "plugin_manifest_fixture.h"

#ifndef PLUGIN_DECLARATION_ID
#error "PLUGIN_DECLARATION_ID is required"
#endif

enum { PLUGIN_SERVICE_VALUE = 111u };
static uint32_t service_value = PLUGIN_SERVICE_VALUE;
static uint32_t fixture_read(void *self) { return *(const uint32_t *)self; }
static const FingerprintService_vtable service_vtable = {.read = fixture_read};
static FingerprintService service_handle = {&service_value, &service_vtable};

/* Exact native bridge: metadata does not reconstruct or dispatch arbitrary ABI. */
static bool SALTS_PLUGIN_CALL declaration_invoke(void *context, void *return_storage,
    void *const *params, size_t param_count) {
    if (context != NULL || return_storage == NULL || params != NULL || param_count != 0u)
        return false;
    *(const cmeta_manifest **)return_storage = plugin_fixture_discovery();
    return true;
}
static const cmeta_plugin_export exports[] = {
    {
        .struct_size = SALTS_PLUGIN_EXPORT_SIZE,
        .kind = SALTS_PLUGIN_EXPORT_INTERFACE,
        .contract_version = PLUGIN_SERVICE_CONTRACT_VERSION,
        .capabilities = 0u,
        .export_id = PLUGIN_SERVICE_EXPORT_ID,
        .contract_id = PLUGIN_SERVICE_CONTRACT_ID,
        .value.interface = {&FingerprintService_interface_meta, &service_handle}
    },
    {
        .struct_size = SALTS_PLUGIN_EXPORT_SIZE,
        .kind = SALTS_PLUGIN_EXPORT_FUNCTION,
        .contract_version = PLUGIN_MANIFEST_CONTRACT_VERSION,
        .capabilities = 0u,
        .export_id = PLUGIN_MANIFEST_EXPORT_ID,
        .contract_id = PLUGIN_MANIFEST_CONTRACT_ID,
        .value.function = {&plugin_manifest_query__function_meta,
            &plugin_manifest_query__function_abi_meta, NULL, declaration_invoke}
    }
};
static const cmeta_plugin_manifest manifest = {
    .struct_size = SALTS_PLUGIN_MANIFEST_SIZE,
    .abi_version = SALTS_PLUGIN_ABI_VERSION,
    .plugin_id = PLUGIN_DECLARATION_ID,
    .version = {1u, 0u, 0u},
    .exports = exports,
    .export_count = sizeof(exports) / sizeof(exports[0])
};
SALTS_PLUGIN_QUERY_EXPORT
const cmeta_plugin_manifest *SALTS_PLUGIN_CALL cmeta_plugin_query(uint32_t host_abi) {
    return host_abi == SALTS_PLUGIN_ABI_VERSION ? &manifest : NULL;
}
