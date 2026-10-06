#include <salts/plugin.h>

#if !defined(_WIN32)
#error "Windows private dependency fixture is Windows-only"
#endif

__declspec(dllimport) int cmeta_plugin_private_dependency_value(void);

static const cmeta_plugin_manifest manifest = {
    .struct_size = CMETA_PLUGIN_MANIFEST_SIZE,
    .abi_version = CMETA_PLUGIN_ABI_VERSION,
    .plugin_id = "test.loader.private_dependency",
    .version = {1u, 0u, 0u},
    .exports = NULL,
    .export_count = 0u};

CMETA_PLUGIN_QUERY_EXPORT const cmeta_plugin_manifest *CMETA_PLUGIN_CALL
cmeta_plugin_query(uint32_t host_abi) {
    if (host_abi != CMETA_PLUGIN_ABI_VERSION)
        return NULL;
    return cmeta_plugin_private_dependency_value() == 42 ? &manifest : NULL;
}
