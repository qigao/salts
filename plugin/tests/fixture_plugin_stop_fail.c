#include <salts/plugin.h>

static int fixture_state;

static cmeta_plugin_status CMETA_PLUGIN_CALL
fixture_start(void *self) {
    int *state = (int *)self;
    *state = 1;
    return CMETA_PLUGIN_OK;
}

static cmeta_plugin_status CMETA_PLUGIN_CALL
fixture_request_stop(void *self) {
    int *state = (int *)self;
    *state = 2;
    return CMETA_PLUGIN_BUSY;
}

static bool CMETA_PLUGIN_CALL
fixture_is_quiescent(const void *self) {
    const int *state = (const int *)self;
    return *state == 2;
}

static void CMETA_PLUGIN_CALL
fixture_destroy(void *self) {
    int *state = (int *)self;
    *state = 3;
}

static const cmeta_plugin_manifest fixture_manifest = {
    .struct_size = CMETA_PLUGIN_MANIFEST_SIZE,
    .abi_version = CMETA_PLUGIN_ABI_VERSION,
    .plugin_id = "test.lifecycle.stop_fail",
    .version = {1u, 0u, 0u},
    .self = &fixture_state,
    .start = fixture_start,
    .request_stop = fixture_request_stop,
    .is_quiescent = fixture_is_quiescent,
    .destroy = fixture_destroy,
};

CMETA_PLUGIN_QUERY_EXPORT
const cmeta_plugin_manifest *CMETA_PLUGIN_CALL
cmeta_plugin_query(uint32_t host_abi) {
    return host_abi == CMETA_PLUGIN_ABI_VERSION ? &fixture_manifest : NULL;
}
