#include <salts/plugin.h>

static const salts_plugin_manifest fixture_manifest = {
    SALTS_PLUGIN_MANIFEST_SIZE,
    SALTS_PLUGIN_ABI_VERSION,
    "test.loader.cpp",
    {1u, 0u, 0u},
    nullptr,
    0u,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr
};

SALTS_PLUGIN_QUERY_EXPORT
const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi) {
    return host_abi == SALTS_PLUGIN_ABI_VERSION ? &fixture_manifest : nullptr;
}
