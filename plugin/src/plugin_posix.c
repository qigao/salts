#include "plugin_loader_internal.h"

#include <dlfcn.h>
#include <string.h>

_Static_assert(sizeof(void *) == sizeof(salts_plugin_query_fn),
               "POSIX dlsym representation must fit plugin query pointer");

salts_plugin_status salts_plugin_platform_close(
    salts_plugin_library *library) {
    void *handle;

    if (library == NULL || library->handle == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    handle = library->handle;
    if (dlclose(handle) != 0)
        return SALTS_PLUGIN_UNLOAD_FAILED;

    library->handle = NULL;
    return SALTS_PLUGIN_OK;
}

salts_plugin_status salts_plugin_platform_open(
    const char *path,
    salts_plugin_library *out_library,
    salts_plugin_query_fn *out_query) {
    void *handle;
    void *symbol;
    const char *error;

    if (path == NULL || out_library == NULL || out_query == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    out_library->handle = NULL;
    *out_query = NULL;

    handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL)
        return SALTS_PLUGIN_LOAD_FAILED;

    dlerror();
    symbol = dlsym(handle, SALTS_PLUGIN_QUERY_SYMBOL);
    error = dlerror();
    if (error != NULL || symbol == NULL) {
        salts_plugin_library cleanup = {handle};
        salts_plugin_status cleanup_status =
            salts_plugin_platform_close(&cleanup);
        return cleanup_status == SALTS_PLUGIN_OK
            ? SALTS_PLUGIN_QUERY_MISSING
            : cleanup_status;
    }

    memcpy(out_query, &symbol, sizeof(*out_query));
    out_library->handle = handle;
    return SALTS_PLUGIN_OK;
}
