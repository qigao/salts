#include <salts/plugin.h>

static int fixture_state;

static salts_plugin_status SALTS_PLUGIN_CALL
fixture_start(void *self) {
    (void)self;
    return SALTS_PLUGIN_BUSY;
}

static salts_plugin_status SALTS_PLUGIN_CALL
fixture_request_stop(void *self) {
    (void)self;
    return SALTS_PLUGIN_OK;
}

static bool SALTS_PLUGIN_CALL
fixture_is_quiescent(const void *self) {
    (void)self;
    return true;
}

static void SALTS_PLUGIN_CALL
fixture_destroy(void *self) {
    int *state = (int *)self;
    ++*state;
}

static const salts_plugin_manifest fixture_manifest = {
    .struct_size = SALTS_PLUGIN_MANIFEST_SIZE,
    .abi_version = SALTS_PLUGIN_ABI_VERSION,
    .plugin_id = "test.lifecycle.start_fail",
    .version = {1u, 0u, 0u},
    .self = &fixture_state,
    .start = fixture_start,
    .request_stop = fixture_request_stop,
    .is_quiescent = fixture_is_quiescent,
    .destroy = fixture_destroy,
};

SALTS_PLUGIN_QUERY_EXPORT
const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi) {
    return host_abi == SALTS_PLUGIN_ABI_VERSION ? &fixture_manifest : NULL;
}
