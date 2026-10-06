#include <salts/plugin.h>

static const cmeta_plugin_manifest manifest = {
    .struct_size = CMETA_PLUGIN_MANIFEST_SIZE,
    .abi_version = CMETA_PLUGIN_ABI_VERSION,
    .plugin_id = "installed.consumer",
    .version = {1u, 0u, 0u},
};

CMETA_PLUGIN_QUERY_EXPORT
const cmeta_plugin_manifest *CMETA_PLUGIN_CALL
cmeta_plugin_query(uint32_t host_abi) {
    return host_abi == CMETA_PLUGIN_ABI_VERSION ? &manifest : NULL;
}

int main(void) {
    const cmeta_plugin_manifest *published =
        cmeta_plugin_query(CMETA_PLUGIN_ABI_VERSION);
    return published != NULL &&
           published->struct_size == CMETA_PLUGIN_MANIFEST_SIZE
               ? 0
               : 1;
}
