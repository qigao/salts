#include <salts/plugin.h>

static int fixture_state;

static cmeta_plugin_status CMETA_PLUGIN_CALL
fixture_start(void *self) {
    (void)self;
    return CMETA_PLUGIN_BUSY;
}

static cmeta_plugin_status CMETA_PLUGIN_CALL
fixture_request_stop(void *self) {
    (void)self;
    return CMETA_PLUGIN_OK;
}

static bool CMETA_PLUGIN_CALL
fixture_is_quiescent(const void *self) {
    (void)self;
    return true;
}

static void CMETA_PLUGIN_CALL
fixture_destroy(void *self) {
    int *state = (int *)self;
    ++*state;
}

static const cmeta_plugin_manifest fixture_manifest = {
    .struct_size = CMETA_PLUGIN_MANIFEST_SIZE,
    .abi_version = CMETA_PLUGIN_ABI_VERSION,
    .plugin_id = "test.lifecycle.start_fail",
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
