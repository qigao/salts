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
  size_t producer_count;
  uint64_t capacity_per_producer;
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
 * One callback job. Jobs from the same producer use the same worker lane and
 * are invoked in accepted publication order. Publication copies event data
 * into the bounded producer channel before returning.
 */
typedef struct cnet_callback_job {
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
bool cnet_callback_workers_get_config(const cnet_callback_workers *workers,
                                      cnet_callback_workers_config *out_config);

/**
 * SPSC and nonblocking for one fixed producer index. The same owner thread
 * must perform every publication for that index. Success moves any release
 * obligation to the assigned callback worker; failure leaves it with the caller.
 * The optional release callback runs after user delivery.
 */
int cnet_callback_workers_publish_from(cnet_callback_workers *workers, uint32_t producer,
                                       const cnet_callback_job *job);

/** Stops admission, drains accepted jobs, and waits up to the supplied deadline. */
int cnet_callback_workers_stop(cnet_callback_workers *workers, uint32_t timeout_ms);

/** Requires a completed stop and no live publisher or callback. */
int cnet_callback_workers_destroy(cnet_callback_workers *workers);

#endif /* CNET_CALLBACK_H */
