#ifndef CNET_OWNER_H
#define CNET_OWNER_H

#include "cnet_command.h"
#include "cnet_event.h"
#include "cnet_resolver.h"
#include "cnet_transport.h"
#include "cnet_uri.h"

enum { CNET_OWNER_ADDRESS_CAPACITY = 128 };

typedef struct cnet_owner {
  void *impl;
} cnet_owner;

typedef struct cnet_owner_connect_payload {
  cnet_uri_scheme scheme;
  size_t address_length;
  unsigned char address[CNET_OWNER_ADDRESS_CAPACITY];
  char host[CNET_RESOLVER_HOST_CAPACITY];
  uint16_t port;
  char pipe_name[CNET_URI_PATH_CAPACITY];
  /** Zero disables the deadline. Connect covers resolution plus transport admission. */
  uint32_t connect_timeout_ms;
  /** Zero disables the per-accepted-read deadline. */
  uint32_t read_timeout_ms;
  /** Zero disables the per-accepted-write deadline. */
  uint32_t write_timeout_ms;
} cnet_owner_connect_payload;

typedef uint64_t (*cnet_owner_now_ms_fn)(void *context);

typedef struct cnet_owner_config {
  native_io_backend_kind backend_kind;
  size_t connection_capacity;
  size_t request_capacity;
  size_t completion_batch_capacity;
  size_t receive_buffer_bytes;
  size_t receive_buffer_count;
  cnet_session_table *sessions;
  cnet_command_queue *commands;
  cnet_event_queue *events;
  /** Optional single-owner monotonic clock seam; NULL uses turbo_monotonic_ms(). */
  cnet_owner_now_ms_fn now_ms;
  void *clock_context;
} cnet_owner_config;

int cnet_owner_init(cnet_owner *owner, const cnet_owner_config *config);

/** Processes bounded commands and one NativeIO completion batch. */
int cnet_owner_drive(cnet_owner *owner, uint32_t timeout_ms);

/** The only operation allowed from a producer thread. */
int cnet_owner_wake(cnet_owner *owner);

/** Clears owner metadata after the terminal notification recycled the handle. */
int cnet_owner_release_session(cnet_owner *owner, cnet_session_handle session);

/** Closes NativeIO admission after every session and request has settled. */
int cnet_owner_close(cnet_owner *owner);
int cnet_owner_destroy(cnet_owner *owner);

#endif /* CNET_OWNER_H */
