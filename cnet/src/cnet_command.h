#ifndef CNET_COMMAND_H
#define CNET_COMMAND_H

#include "cnet_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct cnet_command_queue { void *impl; } cnet_command_queue;

typedef struct cnet_command_queue_config {
  uint64_t capacity;
  size_t max_payload_bytes;
  size_t payload_capacity_bytes;
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

#if defined(CNET_INTERNAL_PROFILING)
typedef struct cnet_command_queue_profile {
  uint64_t publish_ns;
  uint64_t payload_publish_ns;
  uint64_t payload_copy_ns;
  uint64_t publish_calls;
  uint64_t payload_publish_calls;
  uint64_t payload_copy_calls;
} cnet_command_queue_profile;
#endif

typedef enum cnet_command_kind {
  CNET_COMMAND_NONE = 0,
  CNET_COMMAND_CONNECT,
  CNET_COMMAND_RECEIVE,
  CNET_COMMAND_START_TLS,
  CNET_COMMAND_CLOSE,
  CNET_COMMAND_STOP
} cnet_command_kind;

/* CONNECT/START_TLS payload bytes are copied during publication. */
typedef struct cnet_command {
  cnet_command_kind kind;
  cnet_session_handle connection;
  const void *data;
  size_t size;
  size_t argument;
} cnet_command;

typedef struct cnet_command_view {
  cnet_command_kind kind;
  cnet_session_handle connection;
  const void *data;
  size_t size;
  size_t argument;
  uint64_t _sequence;
} cnet_command_view;

int cnet_command_queue_init(cnet_command_queue *queue, const cnet_command_queue_config *config);
int cnet_command_queue_publish(cnet_command_queue *queue, const cnet_command *command);
int cnet_command_queue_take(cnet_command_queue *queue, cnet_command_view *out_view);
int cnet_command_queue_release(cnet_command_queue *queue, cnet_command_view *view);
int cnet_command_queue_close(cnet_command_queue *queue);
bool cnet_command_queue_get_stats(const cnet_command_queue *queue,
                                  cnet_command_queue_stats *out_stats);
#if defined(CNET_INTERNAL_PROFILING)
int cnet_command_queue_profile_begin(cnet_command_queue *queue);
int cnet_command_queue_profile_take(cnet_command_queue *queue,
                                    cnet_command_queue_profile *out_profile);
#endif
int cnet_command_queue_destroy(cnet_command_queue *queue);

#endif
