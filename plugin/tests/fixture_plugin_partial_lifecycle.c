#include <salts/plugin.h>

static int fixture_state;

static cmeta_plugin_status SALTS_PLUGIN_CALL
fixture_start(void *self) {
    (void)self;
    return SALTS_PLUGIN_OK;
}

static const cmeta_plugin_manifest fixture_manifest = {
    .struct_size = SALTS_PLUGIN_MANIFEST_SIZE,
    .abi_version = SALTS_PLUGIN_ABI_VERSION,
    .plugin_id = "test.lifecycle.partial",
    .version = {1u, 0u, 0u},
    .self = &fixture_state,
    .start = fixture_start,
};

SALTS_PLUGIN_QUERY_EXPORT
const cmeta_plugin_manifest *SALTS_PLUGIN_CALL
cmeta_plugin_query(uint32_t host_abi) {
    return host_abi == SALTS_PLUGIN_ABI_VERSION ? &fixture_manifest : NULL;
}
