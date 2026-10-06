#include "plugin_loader_internal.h"

#include <dlfcn.h>
#include <string.h>

_Static_assert(sizeof(void *) == sizeof(cmeta_plugin_query_fn),
               "POSIX dlsym representation must fit plugin query pointer");

cmeta_plugin_status cmeta_plugin_platform_close(
    cmeta_plugin_library *library) {
    void *handle;

    if (library == NULL || library->handle == NULL)
        return CMETA_PLUGIN_INVALID_ARGUMENT;

    handle = library->handle;
    if (dlclose(handle) != 0)
        return CMETA_PLUGIN_UNLOAD_FAILED;

    library->handle = NULL;
    return CMETA_PLUGIN_OK;
}

cmeta_plugin_status cmeta_plugin_platform_open(
    const char *path,
    cmeta_plugin_library *out_library,
    cmeta_plugin_query_fn *out_query) {
    void *handle;
    void *symbol;
    const char *error;

    if (path == NULL || out_library == NULL || out_query == NULL)
        return CMETA_PLUGIN_INVALID_ARGUMENT;

    out_library->handle = NULL;
    *out_query = NULL;

    handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL)
        return CMETA_PLUGIN_LOAD_FAILED;

    dlerror();
    symbol = dlsym(handle, CMETA_PLUGIN_QUERY_SYMBOL);
    error = dlerror();
    if (error != NULL || symbol == NULL) {
        cmeta_plugin_library cleanup = {handle};
        cmeta_plugin_status cleanup_status =
            cmeta_plugin_platform_close(&cleanup);
        return cleanup_status == CMETA_PLUGIN_OK
            ? CMETA_PLUGIN_QUERY_MISSING
            : cleanup_status;
    }

    memcpy(out_query, &symbol, sizeof(*out_query));
    out_library->handle = handle;
    return CMETA_PLUGIN_OK;
}
