#ifndef CNET_EVENT_H
#define CNET_EVENT_H

#include "cnet_session.h"

#include <stddef.h>
#include <stdint.h>

typedef struct cnet_event_queue {
  void *impl;
} cnet_event_queue;

typedef struct cnet_event_queue_config {
  uint64_t capacity;
  size_t data_capacity;
  size_t max_payload_bytes;
} cnet_event_queue_config;

typedef enum cnet_event_kind {
  CNET_EVENT_NONE = 0,
  CNET_EVENT_RECEIVE,
  CNET_EVENT_STATE
} cnet_event_kind;

typedef enum cnet_event_state {
  CNET_EVENT_STATE_NONE = 0,
  CNET_EVENT_STATE_CONNECTED,
  CNET_EVENT_STATE_CLOSING,
  CNET_EVENT_STATE_CLOSED,
  CNET_EVENT_STATE_FAILED
} cnet_event_state;

/**
 * One owner-produced callback event. `data` is borrowed only during publish;
 * successful publication copies `size` bytes into bounded queue storage.
 */
typedef struct cnet_event {
  cnet_event_kind kind;
  cnet_session_handle session;
  cnet_event_state state;
  int status;
  cnet_session_stage stage;
  const void *data;
  size_t size;
} cnet_event;

/** Single-consumer borrowed view; release invalidates `data`. */
typedef struct cnet_event_view {
  cnet_event_kind kind;
  cnet_session_handle session;
  cnet_event_state state;
  int status;
  cnet_session_stage stage;
  const void *data;
  size_t size;
  uint64_t _sequence;
} cnet_event_view;

typedef int (*cnet_event_keep_waiting_fn)(void *context);

int cnet_event_queue_init(cnet_event_queue *queue, const cnet_event_queue_config *config);
bool cnet_event_queue_get_config(const cnet_event_queue *queue,
                                 cnet_event_queue_config *out_config);

/** MPSC, nonblocking; data and total capacity exhaustion return `TURBO_ENOBUFS`. */
int cnet_event_queue_publish(cnet_event_queue *queue, const cnet_event *event);

/** Single-consumer take; empty-open returns `TURBO_ETIMEDOUT`. */
int cnet_event_queue_take(cnet_event_queue *queue, cnet_event_view *out_view);
/** Sleeps without polling until an event arrives or `keep_waiting` becomes false and wakes. */
int cnet_event_queue_take_wait(cnet_event_queue *queue, cnet_event_view *out_view,
                               cnet_event_keep_waiting_fn keep_waiting, void *context);
/** Wakes blocked takers so they can re-check their stop predicate. */
int cnet_event_queue_wake(cnet_event_queue *queue);
/** A taken view may be released exactly once by any callback worker. */
int cnet_event_queue_release(cnet_event_queue *queue, cnet_event_view *view);

int cnet_event_queue_close(cnet_event_queue *queue);

/** Requires closed admission, no borrowed views, and a fully drained queue. */
int cnet_event_queue_destroy(cnet_event_queue *queue);

#endif /* CNET_EVENT_H */
