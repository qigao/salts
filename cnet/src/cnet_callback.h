#ifndef CNET_CALLBACK_H
#define CNET_CALLBACK_H

#include "cnet_event.h"

#include <stddef.h>
#include <stdint.h>

typedef struct cnet_callback_workers {
  void *impl;
} cnet_callback_workers;

typedef struct cnet_callback_workers_config {
  size_t worker_count;
  uint64_t capacity_per_worker;
  size_t max_payload_bytes;
} cnet_callback_workers_config;

/** Borrowed view valid only until both invoke and finish return. */
typedef struct cnet_callback_view {
  cnet_event_kind kind;
  cnet_session_handle session;
  cnet_event_state state;
  int status;
  cnet_session_stage stage;
  const void *data;
  size_t size;
} cnet_callback_view;

typedef void (*cnet_callback_fn)(void *context, const cnet_callback_view *view);
typedef int (*cnet_callback_release_fn)(void *context, const cnet_callback_view *view,
                                        uint64_t token);

/**
 * One leased callback job. Jobs with the same serialization key always use
 * the same worker lane and are invoked in accepted publication order. Event
 * data must remain immutable and address-stable until release returns.
 */
typedef struct cnet_callback_job {
  uint64_t serialization_key;
  cnet_callback_fn invoke;
  cnet_callback_fn finish;
  void *context;
  cnet_event event;
  cnet_callback_release_fn release;
  void *release_context;
  uint64_t release_token;
} cnet_callback_job;

/** Starts exactly one long-lived callback task per configured worker lane. */
int cnet_callback_workers_init(cnet_callback_workers *workers,
                               const cnet_callback_workers_config *config);

/**
 * MPSC and nonblocking. Success moves the data lease and exactly one release
 * obligation to the selected lane; failure leaves both with the caller.
 * Receive jobs therefore require a non-NULL release callback.
 */
int cnet_callback_workers_publish(cnet_callback_workers *workers, const cnet_callback_job *job);

/** Stops admission, drains accepted jobs, and waits up to the supplied deadline. */
int cnet_callback_workers_stop(cnet_callback_workers *workers, uint32_t timeout_ms);

/** Requires a completed stop and no live publisher or callback. */
int cnet_callback_workers_destroy(cnet_callback_workers *workers);

#endif /* CNET_CALLBACK_H */
