#include <salts/plugin.h>

#include "plugin_generic_graph_fixture.h"

static bool SALTS_PLUGIN_CALL plugin_generic_graph_invoke(
    void *context,
    void *return_storage,
    void *const *params,
    size_t param_count) {
    const plugin_generic_graph_value *value;

    if (context != NULL || return_storage == NULL || params == NULL ||
        param_count != 1u || params[0] == NULL)
        return false;

    value = (const plugin_generic_graph_value *)params[0];
    *(int *)return_storage = value->value + 1;
    return true;
}

static const salts_plugin_export plugin_generic_graph_export = {
    .struct_size = SALTS_PLUGIN_EXPORT_SIZE,
    .kind = SALTS_PLUGIN_EXPORT_FUNCTION,
    .contract_version = 1u,
    .capabilities = 1u,
    .export_id = "generic.probe",
    .contract_id = "test.plugin.generic",
    .value.function = {
        .desc = &plugin_generic_graph_probe__function_meta,
        .abi = &plugin_generic_graph_probe__function_abi_meta,
        .context = NULL,
        .invoke = plugin_generic_graph_invoke,
    },
};

static const salts_plugin_manifest plugin_generic_graph_manifest = {
    .struct_size = SALTS_PLUGIN_MANIFEST_SIZE,
    .abi_version = SALTS_PLUGIN_ABI_VERSION,
    .plugin_id = "test.loader.generic_graph",
    .version = {1u, 0u, 0u},
    .exports = &plugin_generic_graph_export,
    .export_count = 1u,
};

SALTS_PLUGIN_QUERY_EXPORT
const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi) {
    return host_abi == SALTS_PLUGIN_ABI_VERSION
               ? &plugin_generic_graph_manifest
               : NULL;
}
