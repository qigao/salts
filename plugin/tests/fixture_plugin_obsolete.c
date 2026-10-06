#include <salts/plugin.h>

enum { OBSOLETE_PLUGIN_ABI = 1u };

static const cmeta_plugin_manifest obsolete_manifest = {
    .struct_size = SALTS_PLUGIN_MANIFEST_SIZE,
    .abi_version = OBSOLETE_PLUGIN_ABI,
    .plugin_id = "test.loader.obsolete",
    .version = {1u, 0u, 0u},
};

SALTS_PLUGIN_QUERY_EXPORT
const cmeta_plugin_manifest *SALTS_PLUGIN_CALL
cmeta_plugin_query(uint32_t host_abi) {
    return host_abi == OBSOLETE_PLUGIN_ABI ? &obsolete_manifest : NULL;
}
