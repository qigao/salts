#include <salts/plugin.h>
#include <salts/thread.h>

FunctionDecl(value, int, fixture_plugin_double,
    (int, value, CMETA_PARAM_IN));

int fixture_plugin_double(int value) {
    return value * 2;
}

static bool SALTS_PLUGIN_CALL fixture_plugin_double_invoke(
    void *context,
    void *return_storage,
    void *const *params,
    size_t param_count) {
    int input;
    int result;

    if (context != NULL || return_storage == NULL || params == NULL ||
        param_count != 1u || params[0] == NULL)
        return false;

    input = *(const int *)params[0];
    result = fixture_plugin_double(input);
    *(int *)return_storage = result;
    return true;
}

static salts_plugin_export fixture_export;
static salts_once_t fixture_export_once = SALTS_ONCE_INIT;

static void fixture_export_init(void) {
    fixture_export = (salts_plugin_export){
        .struct_size = SALTS_PLUGIN_EXPORT_SIZE,
        .kind = SALTS_PLUGIN_EXPORT_FUNCTION,
        .contract_version = 1u,
        .capabilities = 1u,
        .export_id = "test.loader.math.double",
        .contract_id = "test.loader.math",
        .value.function = {
            .desc = FunctionMeta(fixture_plugin_double),
            .abi = FunctionAbi(fixture_plugin_double),
            .context = NULL,
            .invoke = fixture_plugin_double_invoke,
        },
    };
}

static const salts_plugin_manifest fixture_manifest = {
    .struct_size = SALTS_PLUGIN_MANIFEST_SIZE,
    .abi_version = SALTS_PLUGIN_ABI_VERSION,
    .plugin_id = "test.loader.c",
    .version = {1u, 0u, 0u},
    .exports = &fixture_export,
    .export_count = 1u,
};

SALTS_PLUGIN_QUERY_EXPORT
const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi) {
    if (host_abi != SALTS_PLUGIN_ABI_VERSION)
        return NULL;
    salts_once(&fixture_export_once, fixture_export_init);
    return &fixture_manifest;
}
