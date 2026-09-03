#ifndef CNET_OWNER_H
#define CNET_OWNER_H

#include "cnet_command.h"
#include "cnet_event.h"
#include "cnet_resolver.h"
#include "cnet_tls.h"
#include "cnet_transport.h"
#include "cnet_uri.h"

enum { CNET_OWNER_ADDRESS_CAPACITY = 128 };

typedef struct cnet_owner {
  void *impl;
} cnet_owner;

typedef struct cnet_owner_connect_payload {
  cnet_uri_scheme scheme;
  uintptr_t adopted_socket;
  size_t address_length;
  unsigned char address[CNET_OWNER_ADDRESS_CAPACITY];
  char host[CNET_RESOLVER_HOST_CAPACITY];
  uint16_t port;
  char pipe_name[CNET_URI_PATH_CAPACITY];
  cnet_tls_context *tls_context;
  char tls_server_name[CNET_TLS_SERVER_NAME_CAPACITY];
  /** Zero disables the deadline. Connect covers resolution plus transport admission. */
  uint32_t connect_timeout_ms;
  /** Zero disables the per-accepted-read deadline. */
  uint32_t read_timeout_ms;
  /** Zero disables the per-accepted-write deadline. */
  uint32_t write_timeout_ms;
  uint32_t tls_handshake_timeout_ms;
  size_t tls_io_buffer_bytes;
  bool adopted;
  bool tls_server;
} cnet_owner_connect_payload;

typedef uint64_t (*cnet_owner_now_ms_fn)(void *context);
typedef int (*cnet_owner_event_publish_fn)(void *context, const cnet_event *event);

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
  /** Optional poll-owner fast path; NULL publishes to events. */
  cnet_owner_event_publish_fn publish_event;
  void *event_context;
  /** Optional single-owner monotonic clock seam; NULL uses salts_monotonic_ms(). */
  cnet_owner_now_ms_fn now_ms;
  void *clock_context;
} cnet_owner_config;

int cnet_owner_init(cnet_owner *owner, const cnet_owner_config *config);

/** Processes bounded commands and one NativeIO completion batch. */
int cnet_owner_drive(cnet_owner *owner, uint32_t timeout_ms);

/** Thread-safe advisory wake for an owner blocked in drive. */
int cnet_owner_wake(cnet_owner *owner);

/** Reports the bounded NativeIO coroutine state owned by this shard. */
bool cnet_owner_get_coroutine_stats(const cnet_owner *owner, native_io_coroutine_stats *out_stats);

/** Clears owner metadata after the terminal notification recycled the handle. */
int cnet_owner_release_session(cnet_owner *owner, cnet_session_handle session);

/** Closes NativeIO admission after every session and request has settled. */
int cnet_owner_close(cnet_owner *owner);
int cnet_owner_destroy(cnet_owner *owner);

#endif /* CNET_OWNER_H */
