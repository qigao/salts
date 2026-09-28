#ifndef SALTS_PLUGIN_LOADER_INTERNAL_H
#define SALTS_PLUGIN_LOADER_INTERNAL_H

#include <salts/plugin.h>

typedef struct salts_plugin_library {
    void *handle;
} salts_plugin_library;

salts_plugin_status salts_plugin_platform_open(
    const char *path,
    salts_plugin_library *out_library,
    salts_plugin_query_fn *out_query);

salts_plugin_status salts_plugin_platform_close(
    salts_plugin_library *library);

#endif /* SALTS_PLUGIN_LOADER_INTERNAL_H */
