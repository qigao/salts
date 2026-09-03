#include "fs_watch_internal.h"

#include <salts/error_codes.h>

int cflow_fs_watch_backend_open(cflow_fs_watch_impl *impl,
                                const char *path,
                                const cflow_fs_watch_config *config) {
    (void)impl;
    (void)path;
    (void)config;
    return SALTS_ENOTSUP;
}

void cflow_fs_watch_backend_request_close(cflow_fs_watch_impl *impl) {
    (void)impl;
}

int cflow_fs_watch_backend_destroy(cflow_fs_watch_impl *impl) {
    (void)impl;
    return SALTS_ENOTSUP;
}
