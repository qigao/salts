#ifndef CNET_COMMAND_H
#define CNET_COMMAND_H

#include "cnet_session.h"

#include <stddef.h>
#include <stdint.h>

typedef struct cnet_command_queue {
  void *impl;
} cnet_command_queue;

typedef struct cnet_command_queue_config {
  uint64_t capacity;
  size_t max_payload_bytes;
} cnet_command_queue_config;

typedef struct cnet_command_queue_stats {
  size_t live_commands;
  size_t peak_commands;
  size_t queued_bytes;
  size_t peak_queued_bytes;
  uint64_t rejected_commands;
  uint64_t rejected_bytes;
  bool admission_open;
} cnet_command_queue_stats;

typedef enum cnet_command_kind {
  CNET_COMMAND_NONE = 0,
  CNET_COMMAND_CONNECT,
  CNET_COMMAND_SEND,
  CNET_COMMAND_RECEIVE,
  CNET_COMMAND_CLOSE,
  CNET_COMMAND_STOP
} cnet_command_kind;

/**
 * One producer-owned descriptor. `data` is borrowed only for the duration of
 * `cnet_command_queue_publish`; successful publication copies `size` bytes.
 */
typedef struct cnet_command {
  cnet_command_kind kind;
  cnet_session_handle connection;
  const void *data;
  size_t size;
  size_t argument;
} cnet_command;

/**
 * One owner-only borrowed queue view. `data` becomes invalid when the view is
 * released. The underscored sequence is an opaque release token.
 */
typedef struct cnet_command_view {
  cnet_command_kind kind;
  cnet_session_handle connection;
  const void *data;
  size_t size;
  size_t argument;
  uint64_t _sequence;
} cnet_command_view;

int cnet_command_queue_init(cnet_command_queue *queue, const cnet_command_queue_config *config);

/** Single-owner, nonblocking; full capacity returns `TURBO_ENOBUFS`. */
int cnet_command_queue_publish(cnet_command_queue *queue, const cnet_command *command);

/** Single-owner, nonblocking; empty-open returns `TURBO_ETIMEDOUT`. */
int cnet_command_queue_take(cnet_command_queue *queue, cnet_command_view *out_view);
int cnet_command_queue_release(cnet_command_queue *queue, cnet_command_view *view);

/**
 * Closes admission on the owner thread. A repeated close returns
 * `TURBO_EALREADY`.
 */
int cnet_command_queue_close(cnet_command_queue *queue);

/** Returns one owner-thread diagnostic snapshot. */
bool cnet_command_queue_get_stats(const cnet_command_queue *queue,
                                  cnet_command_queue_stats *out_stats);

/** Requires closed admission, no borrowed view, and a fully drained queue. */
int cnet_command_queue_destroy(cnet_command_queue *queue);

#endif /* CNET_COMMAND_H */
