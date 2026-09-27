#include <salts/plugin.h>

static const salts_plugin_manifest manifest = {
    .struct_size = SALTS_PLUGIN_MANIFEST_SIZE,
    .abi_version = SALTS_PLUGIN_ABI_VERSION,
    .plugin_id = "installed.consumer",
    .version = {1u, 0u, 0u},
};

SALTS_PLUGIN_QUERY_EXPORT
const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi) {
    return host_abi == SALTS_PLUGIN_ABI_VERSION ? &manifest : NULL;
}

int main(void) {
    const salts_plugin_manifest *published =
        salts_plugin_query(SALTS_PLUGIN_ABI_VERSION);
    return published != NULL &&
           published->struct_size == SALTS_PLUGIN_MANIFEST_SIZE
               ? 0
               : 1;
}
