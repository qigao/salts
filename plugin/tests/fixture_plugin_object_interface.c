#include <salts/plugin.h>

#include "plugin_object_interface_fixture.h"

#include <stdbool.h>

static plugin_object_fixture_state fixture_state = {7};

static int fixture_add(void *self, int delta) {
    plugin_object_fixture_state *state =
        (plugin_object_fixture_state *)self;
    state->value += delta;
    return state->value;
}

static int fixture_value(void *self) {
    return ((plugin_object_fixture_state *)self)->value;
}

CMETA_IMPLEMENTS(plugin_object_fixture_api, plugin_object_fixture_impl, 1u,
    .add = fixture_add,
    .value = fixture_value
);

plugin_object_fixture_state *plugin_object_fixture_identity(void) {
    return &fixture_state;
}

static bool CMETA_PLUGIN_CALL fixture_identity_invoke(
    void *context,
    void *return_storage,
    void *const *params,
    size_t param_count) {
    plugin_object_fixture_state *identity;

    (void)params;
    if (context != NULL || return_storage == NULL || param_count != 0u)
        return false;

    identity = plugin_object_fixture_identity();
    *(plugin_object_fixture_state **)return_storage = identity;
    return true;
}

static plugin_object_fixture_api fixture_api;
static cmeta_plugin_export fixture_exports[2];
static cmeta_plugin_manifest fixture_manifest = {
    .struct_size = CMETA_PLUGIN_MANIFEST_SIZE,
    .abi_version = CMETA_PLUGIN_ABI_VERSION,
    .plugin_id = "test.loader.object_interface",
    .version = {1u, 0u, 0u},
};
static bool fixture_initialized;

CMETA_PLUGIN_QUERY_EXPORT
const cmeta_plugin_manifest *CMETA_PLUGIN_CALL
cmeta_plugin_query(uint32_t host_abi) {
    if (host_abi != CMETA_PLUGIN_ABI_VERSION)
        return NULL;

    if (!fixture_initialized) {
        fixture_api =
            plugin_object_fixture_impl_as_plugin_object_fixture_api(
                &fixture_state);

        fixture_exports[0] = (cmeta_plugin_export){
            .struct_size = CMETA_PLUGIN_EXPORT_SIZE,
            .kind = CMETA_PLUGIN_EXPORT_INTERFACE,
            .contract_version = 1u,
            .capabilities = 1u,
            .export_id = "service",
            .contract_id = "test.object.service",
            .value.interface = {
                .desc = plugin_object_fixture_api_interface(),
                .value = &fixture_api,
            },
        };

        fixture_exports[1] = (cmeta_plugin_export){
            .struct_size = CMETA_PLUGIN_EXPORT_SIZE,
            .kind = CMETA_PLUGIN_EXPORT_FUNCTION,
            .contract_version = 1u,
            .capabilities = 1u,
            .export_id = "borrow_identity",
            .contract_id = "test.object.identity",
            .value.function = {
                .desc = FunctionMeta(plugin_object_fixture_identity),
                .abi = FunctionAbi(plugin_object_fixture_identity),
                .context = NULL,
                .invoke = fixture_identity_invoke,
            },
        };

        fixture_manifest.exports = fixture_exports;
        fixture_manifest.export_count = 2u;
        fixture_initialized = true;
    }

    return &fixture_manifest;
}
