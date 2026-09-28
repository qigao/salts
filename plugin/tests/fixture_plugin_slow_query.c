#include <salts/plugin.h>
#include <salts/thread.h>

#include "plugin_slow_query_fixture.h"

#include <stdbool.h>
#include <stdio.h>

static bool marker_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return false;
    fclose(file);
    return true;
}

static void touch_marker(const char *path) {
    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return;
    fputs("entered\n", file);
    fclose(file);
}

static const salts_plugin_manifest fixture_manifest = {
    .struct_size = SALTS_PLUGIN_MANIFEST_SIZE,
    .abi_version = SALTS_PLUGIN_ABI_VERSION,
    .plugin_id = "test.loader.slow_query",
    .version = {1u, 0u, 0u},
};

SALTS_PLUGIN_QUERY_EXPORT
const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi) {
    if (host_abi != SALTS_PLUGIN_ABI_VERSION)
        return NULL;

    touch_marker(PLUGIN_SLOW_QUERY_ENTERED_MARKER);
    while (!marker_exists(PLUGIN_SLOW_QUERY_RELEASE_MARKER))
        salts_sleep_ms(1u);

    return &fixture_manifest;
}
