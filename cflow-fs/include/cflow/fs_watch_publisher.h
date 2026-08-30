#ifndef CFLOW_FS_WATCH_PUBLISHER_H
#define CFLOW_FS_WATCH_PUBLISHER_H

#include <cflow/fs_watch.h>
#include <cflow/reactive.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Move-only cleanup owner. Keep it alive until the Publisher is destroyed. */
typedef struct cflow_fs_watch_publisher_owner {
  void *impl;
} cflow_fs_watch_publisher_owner;

/* event paths are borrows valid only during encode; copy required data out. */
typedef bool (*cflow_fs_watch_encode_fn)(void *user, const cflow_fs_watch_event *event,
                                         void *out_value);

typedef struct cflow_fs_watch_publisher_config {
  bool recursive;
  size_t event_capacity;
  size_t watch_capacity;
  size_t path_capacity;
  size_t native_buffer_capacity;
  /* name, output_type, and encode_user remain borrowed through Publisher destruction. */
  const char *name;
  const cmeta_type_desc *output_type;
  cflow_fs_watch_encode_fn encode;
  void *encode_user;
} cflow_fs_watch_publisher_config;

/**
 * Open a native watcher and expose its events as a typed CFlow Publisher.
 *
 * output_type must support trivial copy and trivial destruction. encode is
 * called on the CFlow driver thread and must fully initialize out_value
 * without retaining event path pointers.
 *
 * Success moves cleanup responsibility into Publisher plus owner. Destroy the
 * Publisher first, then retry owner_close while it returns TURBO_EBUSY. Failure
 * leaves both caller-owned outputs unchanged.
 *
 * @param out Zero-initialized Publisher destination, moved into a Subscription.
 * @param owner Zero-initialized external cleanup owner.
 * @param path Existing directory encoded as UTF-8.
 * @param config Positive bounded capacities, output type, and encoder.
 * @return TURBO_OK; TURBO_EINVAL for malformed input; TURBO_ENOTSUP for a
 * non-trivial output type/backend contract; TURBO_ENOMEM; or a native error.
 */
int cflow_fs_watch_publisher_open(cflow_publisher *out, cflow_fs_watch_publisher_owner *owner,
                               const char *path, const cflow_fs_watch_publisher_config *config);

/**
 * Acknowledge a delivered RESCAN_REQUIRED after rebuilding caller state.
 * @return TURBO_OK, TURBO_EINVAL, or TURBO_EALREADY.
 */
int cflow_fs_watch_publisher_owner_acknowledge_rescan(cflow_fs_watch_publisher_owner *owner);

/**
 * Copy the underlying bounded watcher statistics.
 * @return true on a live owner and non-NULL output; otherwise false.
 */
bool cflow_fs_watch_publisher_owner_get_stats(const cflow_fs_watch_publisher_owner *owner,
                                           cflow_fs_watch_stats *out);

/**
 * Drain and release native state after Publisher destruction.
 * Returns TURBO_EBUSY while Publisher is live or native shutdown is incomplete.
 * Once native destruction consumes the watcher, owner is cleared even if the
 * function returns a native cleanup error.
 * @return TURBO_OK, TURBO_EINVAL, TURBO_EBUSY, or a native cleanup error.
 */
int cflow_fs_watch_publisher_owner_close(cflow_fs_watch_publisher_owner *owner);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_FS_WATCH_PUBLISHER_H */
