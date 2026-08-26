#ifndef CFLOW_FS_WATCH_H
#define CFLOW_FS_WATCH_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cflow_fs_watch {
    void *impl;
} cflow_fs_watch;

typedef enum cflow_fs_watch_event_kind {
    CFLOW_FS_WATCH_CREATED = 0,
    CFLOW_FS_WATCH_REMOVED,
    CFLOW_FS_WATCH_MODIFIED,
    CFLOW_FS_WATCH_ATTRIBUTES,
    CFLOW_FS_WATCH_RENAMED,
    CFLOW_FS_WATCH_ROOT_CHANGED,
    CFLOW_FS_WATCH_RESCAN_REQUIRED
} cflow_fs_watch_event_kind;

typedef enum cflow_fs_watch_entry_type {
    CFLOW_FS_WATCH_ENTRY_UNKNOWN = 0,
    CFLOW_FS_WATCH_ENTRY_FILE,
    CFLOW_FS_WATCH_ENTRY_DIRECTORY
} cflow_fs_watch_entry_type;

typedef struct cflow_fs_watch_event {
    /* path and old_path are normalized UTF-8 borrows valid during callback. */
    cflow_fs_watch_event_kind kind;
    const char *path;
    const char *old_path;
    cflow_fs_watch_entry_type entry_type;
} cflow_fs_watch_event;

typedef void (*cflow_fs_watch_event_fn)(
    void *user, const cflow_fs_watch_event *event);

typedef struct cflow_fs_watch_config {
    /* Recursive support is backend capability checked during open. */
    bool recursive;
    /* Detailed event slots; one rescan marker lives in control state. */
    size_t event_capacity;
    /* Maximum native directory registrations for recursive backends. */
    size_t watch_capacity;
    /* Per-event relative-path capacity, including the trailing NUL. */
    size_t path_capacity;
    /* One bounded native read buffer; accepted range is 1024..65536. */
    size_t native_buffer_capacity;
    cflow_fs_watch_event_fn event;
    void *event_user;
} cflow_fs_watch_config;

typedef enum cflow_fs_watch_lifecycle {
    CFLOW_FS_WATCH_OPEN = 0,
    CFLOW_FS_WATCH_CLOSING,
    CFLOW_FS_WATCH_CLOSED
} cflow_fs_watch_lifecycle;

typedef struct cflow_fs_watch_stats {
    size_t capacity;
    size_t queued;
    size_t delivered;
    size_t suppressed;
    size_t rescan_required;
    bool awaiting_rescan;
    cflow_fs_watch_lifecycle lifecycle;
} cflow_fs_watch_stats;

/**
 * Open and arm a native directory event source before returning.
 *
 * @param watch Zero-initialized owning destination.
 * @param path Existing directory encoded as UTF-8.
 * @param config Positive capacities and a non-NULL callback.
 * @return TURBO_OK, TURBO_EINVAL, TURBO_ENOMEM, TURBO_ENOTSUP for an
 * unsupported recursive/backend contract, or a negative native error.
 */
int cflow_fs_watch_open(cflow_fs_watch *watch, const char *path,
                        const cflow_fs_watch_config *config);
/**
 * Deliver at most max_events callbacks on the calling driver thread.
 * @return TURBO_OK, TURBO_EINVAL, or TURBO_EBUSY for concurrent/reentrant use.
 */
int cflow_fs_watch_run_ready(cflow_fs_watch *watch, size_t max_events,
                             size_t *delivered);
/**
 * Resume detailed publication after the rescan callback has been delivered
 * and the caller has rebuilt its authoritative filesystem view. If native
 * events were suppressed after marker delivery, acknowledgement retains the
 * loss state and queues another rescan marker instead of losing that race.
 */
int cflow_fs_watch_acknowledge_rescan(cflow_fs_watch *watch);
/** Stop native publication and wake the backend thread without blocking. */
int cflow_fs_watch_close(cflow_fs_watch *watch);
/** Return true after close, backend termination, and event delivery drain. */
bool cflow_fs_watch_is_quiescent(const cflow_fs_watch *watch);
/** Copy an observational capacity, loss, and lifecycle snapshot. */
bool cflow_fs_watch_get_stats(const cflow_fs_watch *watch,
                              cflow_fs_watch_stats *out);
/** Join and release a quiescent source, restoring watch to zero. */
int cflow_fs_watch_destroy(cflow_fs_watch *watch);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_FS_WATCH_H */
