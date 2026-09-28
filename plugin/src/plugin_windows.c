#include "plugin_loader_internal.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(FARPROC) == sizeof(salts_plugin_query_fn),
               "GetProcAddress representation must fit plugin query pointer");

salts_plugin_status salts_plugin_platform_close(
    salts_plugin_library *library) {
    HMODULE module;

    if (library == NULL || library->handle == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    module = (HMODULE)library->handle;
    if (!FreeLibrary(module))
        return SALTS_PLUGIN_UNLOAD_FAILED;

    library->handle = NULL;
    return SALTS_PLUGIN_OK;
}

salts_plugin_status salts_plugin_platform_open(
    const char *path,
    salts_plugin_library *out_library,
    salts_plugin_query_fn *out_query) {
    int wide_length;
    wchar_t *wide_path;
    HMODULE module;
    FARPROC symbol;

    if (path == NULL || out_library == NULL || out_query == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    out_library->handle = NULL;
    *out_query = NULL;

    wide_length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (wide_length <= 0)
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    wide_path = (wchar_t *)malloc((size_t)wide_length * sizeof(*wide_path));
    if (wide_path == NULL)
        return SALTS_PLUGIN_ALLOCATION_FAILED;

    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
            wide_path, wide_length) != wide_length) {
        free(wide_path);
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    }

    module = LoadLibraryW(wide_path);
    free(wide_path);
    if (module == NULL)
        return SALTS_PLUGIN_LOAD_FAILED;

    symbol = GetProcAddress(module, SALTS_PLUGIN_QUERY_SYMBOL);
    if (symbol == NULL) {
        salts_plugin_library cleanup = {(void *)module};
        salts_plugin_status cleanup_status =
            salts_plugin_platform_close(&cleanup);
        return cleanup_status == SALTS_PLUGIN_OK
            ? SALTS_PLUGIN_QUERY_MISSING
            : cleanup_status;
    }

    memcpy(out_query, &symbol, sizeof(*out_query));
    out_library->handle = (void *)module;
    return SALTS_PLUGIN_OK;
}
