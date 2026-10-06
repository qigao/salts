#ifndef SALTS_PLUGIN_LOADER_INTERNAL_H
#define SALTS_PLUGIN_LOADER_INTERNAL_H

#include <salts/plugin.h>

typedef struct cmeta_plugin_library {
    void *handle;
} cmeta_plugin_library;

cmeta_plugin_status cmeta_plugin_platform_open(
    const char *path,
    cmeta_plugin_library *out_library,
    cmeta_plugin_query_fn *out_query);

cmeta_plugin_status cmeta_plugin_platform_close(
    cmeta_plugin_library *library);

#endif /* SALTS_PLUGIN_LOADER_INTERNAL_H */
