#ifndef CFLOW_FS_WATCH_INTERNAL_H
#define CFLOW_FS_WATCH_INTERNAL_H

#include <cflow/fs_watch.h>

typedef struct cflow_fs_watch_impl cflow_fs_watch_impl;
typedef void (*cflow_fs_watch_ready_fn)(void *user);

int cflow_fs_watch_open_notified(cflow_fs_watch *watch, const char *path,
                                 const cflow_fs_watch_config *config,
                                 cflow_fs_watch_ready_fn ready,
                                 void *ready_user);
bool cflow_fs_watch_has_ready_or_done(const cflow_fs_watch *watch);
bool cflow_fs_watch_backend_done_and_empty(const cflow_fs_watch *watch);

int cflow_fs_watch_backend_open(cflow_fs_watch_impl *impl,
                                const char *path,
                                const cflow_fs_watch_config *config);
void cflow_fs_watch_backend_request_close(cflow_fs_watch_impl *impl);
int cflow_fs_watch_backend_destroy(cflow_fs_watch_impl *impl);

int cflow_fs_watch_publish(cflow_fs_watch_impl *impl,
                           cflow_fs_watch_event_kind kind,
                           const char *path,
                           const char *old_path,
                           cflow_fs_watch_entry_type entry_type);
void cflow_fs_watch_publish_loss(cflow_fs_watch_impl *impl);
void cflow_fs_watch_backend_mark_done(cflow_fs_watch_impl *impl);
void cflow_fs_watch_backend_set(cflow_fs_watch_impl *impl, void *backend);
void *cflow_fs_watch_backend_get(cflow_fs_watch_impl *impl);
bool cflow_fs_watch_close_requested(const cflow_fs_watch_impl *impl);

#endif /* CFLOW_FS_WATCH_INTERNAL_H */
