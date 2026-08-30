# CFlowFS

`TurboUtils::CFlowFS` is the filesystem control-plane adapter between the
synchronous `turbo_fs` implementation and CFlow's bounded execution model. It
is separate from `TurboUtils::CFlow` so the portable kernel has no reverse
dependency on filesystem policy or native watcher backends.

The distinction is intentional:

- `turbo_fs_*` calls are synchronous and run on their caller;
- `cflow_io_file` offset reads/writes use explicitly selected native IOCP or
  io_uring data-plane backends;
- `cflow_fs_service` runs pathname operations on an explicitly bounded worker
  backend and never presents them as kernel-native asynchronous file I/O.

## Example

```c
#include <cflow/fs.h>
#include <turbo/error_codes.h>
#include <turbo/thread.h>

static void completed(void *user, uint64_t id,
                      cflow_fs_operation_kind operation, int result) {
    int *done = (int *)user;
    (void)id;
    (void)operation;
    *done = result == TURBO_OK ? 1 : -1;
}

int inspect_path(const char *path, turbo_fs_stat_t *out) {
    cflow_fs_service service = {0};
    int done = 0;
    cflow_fs_config config = {1u, 8u, 1024u, completed, &done};
    cflow_fs_submit_result submitted;

    if (cflow_fs_service_init(&service, &config) != TURBO_OK)
        return -1;
    submitted = cflow_fs_try_stat(&service, path, out);
    if (submitted.status != CFLOW_FS_SUBMIT_ACCEPTED)
        return cflow_fs_close(&service), cflow_fs_destroy(&service), -1;
    while (done == 0) {
        size_t count = 0u;
        if (cflow_fs_run_ready(&service, 8u, &count) != TURBO_OK)
            return -1;
        if (count == 0u)
            turbo_sleep_ms(1u);
    }
    if (cflow_fs_close(&service) != TURBO_OK)
        return -1;
    while (!cflow_fs_is_quiescent(&service)) {
        size_t count = 0u;
        if (cflow_fs_run_ready(&service, 8u, &count) != TURBO_OK)
            return -1;
    }
    return cflow_fs_destroy(&service) == TURBO_OK && done > 0 ? 0 : -1;
}
```

Accepted paths are copied. Stat and directory result storage remains borrowed
until callback return. `close` cancels queued requests but does not interrupt a
blocking syscall already executing; callers must keep driving callbacks until
the service is quiescent before destruction.

## Filesystem watch

`<cflow/fs_watch.h>` exposes a native event source with fixed event, watch,
path, and kernel-buffer capacities. Windows uses overlapped
`ReadDirectoryChangesW`, Linux uses one bounded inotify registration per
directory for recursive mode, and macOS uses per-item FSEvents on a private
serial dispatch queue. A full detailed queue, native registration capacity,
kernel overflow, invalid UTF-8, or an
unpairable rename produces one `CFLOW_FS_WATCH_RESCAN_REQUIRED` marker and
suppresses further detail until `cflow_fs_watch_acknowledge_rescan()`.
If changes are suppressed after a marker is delivered while the caller rebuilds
its view, acknowledgement schedules another marker; callers repeat the snapshot
and acknowledgement cycle until `awaiting_rescan` becomes false.

Callbacks run only from `cflow_fs_watch_run_ready()`. Event paths are normalized
UTF-8 paths borrowed until callback return. Rename events contain `old_path`
only when the backend proved the pair with its native correlation mechanism.
FSEvents can mark rename and unlink observations ambiguously; those observations
request a rescan rather than fabricating a precise rename or removal event.

## Typed watch Publisher

`<cflow/fs_watch_publisher.h>` adapts the same bounded native watcher directly to
a `cflow_publisher`. Publication wakes an armed CFlow waitable from the backend
thread; no polling loop and no per-watch helper thread are introduced. The
Publisher can therefore be moved into `cflow_subscribe()` and used by identity or
operator Graphs. A Subscriber may also convert the resulting value to a
`cflow_event_view` and send it through an existing `cflow_actor_ref`; the Actor
does not need a second filesystem-specific input implementation.

The output type and encoder are application-defined:

```c
#include <cflow/fs_watch_publisher.h>
#include <stdio.h>

typedef struct file_change {
    cflow_fs_watch_event_kind kind;
    char path[256];
} file_change;

static bool encode_change(void *user, const cflow_fs_watch_event *event,
                          void *out_value) {
    file_change *out = (file_change *)out_value;
    (void)user;
    out->kind = event->kind;
    if (event->path != NULL)
        snprintf(out->path, sizeof(out->path), "%s", event->path);
    return true;
}
```

`output_type` must advertise trivial copy and trivial destruction. `name`,
`output_type`, and `encode_user` are borrowed until Publisher destruction. The
encoder runs on the CFlow driver thread and must copy every required path because
the watch event strings expire when it returns. `event_capacity`, `watch_capacity`,
`path_capacity`, and `native_buffer_capacity` remain hard bounds; overflow is
still represented by the generation-safe `CFLOW_FS_WATCH_RESCAN_REQUIRED`
value. After rebuilding its authoritative view, the consumer calls
`cflow_fs_watch_publisher_owner_acknowledge_rescan()`.

Ownership is deliberately split. Subscription owns the moved Publisher, while the caller
retains `cflow_fs_watch_publisher_owner`. Close Subscription (or destroy the standalone
Publisher) first, then retry `cflow_fs_watch_publisher_owner_close()` while it returns
`TURBO_EBUSY`; success means the backend stopped, queued events were drained,
native handles were released, and the owner was cleared. Owner operations are
control-plane calls and must not race each other or Publisher destruction.
