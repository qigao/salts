#ifndef CNET_OWNER_H
#define CNET_OWNER_H

#include "cnet_command.h"
#include "cnet_event.h"
#include "cnet_resolver.h"
#include "cnet_tls.h"
#include "cnet_transport.h"
#include "cnet_uri.h"
#include "cnet_write_queue.h"

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
  cnet_stream_socket_options socket_options;
  bool adopted;
  bool tls_server;
} cnet_owner_connect_payload;

/** Fixed command payload whose retained context ownership transfers to the owner. */
typedef struct cnet_owner_start_tls_payload {
  cnet_tls_context *tls_context;
  char tls_server_name[CNET_TLS_SERVER_NAME_CAPACITY];
  uint32_t tls_handshake_timeout_ms;
  size_t tls_io_buffer_bytes;
  bool tls_server;
} cnet_owner_start_tls_payload;

typedef uint64_t (*cnet_owner_now_ms_fn)(void *context);
typedef int (*cnet_owner_event_publish_fn)(void *context, const cnet_event *event);

typedef struct cnet_owner_config {
  native_io_backend_kind backend_kind;
  size_t connection_capacity;
  size_t request_capacity;
  size_t completion_batch_capacity;
  size_t receive_buffer_bytes;
  size_t receive_buffer_count;
  /** Optional W1 write ownership substrate; zero disables it for direct owner fixtures. */
  size_t write_capacity;
  size_t max_write_bytes;
  size_t write_buffer_bytes;
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

#if defined(CNET_INTERNAL_PROFILING)
/** Diagnostic timings collected only during an explicit quiescent sample. */
typedef struct cnet_owner_profile {
  uint64_t owner_drive_ns;
  uint64_t receive_rearm_stage_ns;
  uint64_t receive_rearm_request_lifecycle_ns;
  uint64_t command_stage_ns;
  uint64_t command_request_lifecycle_ns;
  uint64_t request_lifecycle_ns;
  uint64_t request_start_ns;
  uint64_t request_resubmit_ns;
  uint64_t observe_ns;
  uint64_t request_completion_ns;
  uint64_t event_publish_ns;
  uint64_t owner_drive_calls;
  uint64_t receive_rearm_stage_calls;
  uint64_t receive_rearm_request_lifecycle_calls;
  uint64_t command_stage_calls;
  uint64_t command_request_lifecycle_calls;
  uint64_t request_lifecycle_calls;
  uint64_t request_start_calls;
  uint64_t request_resubmit_calls;
  uint64_t observe_calls;
  uint64_t request_completion_calls;
  uint64_t event_publish_calls;
  /** Producer-side command queue timing; payload publish includes payload copy. */
  uint64_t command_queue_publish_ns;
  uint64_t command_queue_payload_publish_ns;
  uint64_t command_queue_payload_copy_ns;
  uint64_t command_queue_publish_calls;
  uint64_t command_queue_payload_publish_calls;
  uint64_t command_queue_payload_copy_calls;
} cnet_owner_profile;
#endif

int cnet_owner_init(cnet_owner *owner, const cnet_owner_config *config);

/** Processes bounded commands and directly settles one NativeIO completion batch. */
int cnet_owner_drive(cnet_owner *owner, uint32_t timeout_ms);

/**
 * Owner-thread direct receive admission.
 *
 * Adds bounded receive demand and schedules one owner-local rearm for the next
 * drive when no read is active. It never publishes a callback synchronously.
 * Callback reentrancy while a completion batch is being routed must stay on
 * the deferred command path.
 */
int cnet_owner_receive_direct(cnet_owner *owner, cnet_session_handle session, size_t demand);

/**
 * Owner-thread send ownership admission. These copy/retain into the bounded
 * write queue and schedule owner-local work; they never publish a callback
 * synchronously. TLS remains on the existing command/TLS path in W2.
 */
int cnet_owner_send_copy_direct(cnet_owner *owner, cnet_session_handle session,
                                const void *data, size_t size);
int cnet_owner_send_buffer_direct(cnet_owner *owner, cnet_session_handle session,
                                  mem_buffer_t *buffer);
int cnet_owner_sendv_direct(cnet_owner *owner, cnet_session_handle session,
                            const cnet_const_buffer *segments, size_t segment_count);

/**
 * Owner-thread quiescent close admission.
 *
 * Commits the canonical session to DRAINING and schedules owner-local close
 * progress for the next drive. It publishes no callback synchronously.
 * Non-quiescent sessions return SALTS_EBUSY so the caller can preserve FIFO
 * ordering through the deferred command path.
 */
int cnet_owner_close_direct(cnet_owner *owner, cnet_session_handle session);

/** Thread-safe advisory wake for an owner blocked in drive. */
int cnet_owner_wake(cnet_owner *owner);

#if defined(CNET_INTERNAL_TESTING)
typedef struct cnet_owner_test_request_snapshot {
  uintptr_t token;
  native_io_request native_request;
  native_io_endpoint endpoint;
  bool active;
} cnet_owner_test_request_snapshot;

/** Test-only view of the NativeIO request and coroutine ownership beneath this owner. */
bool cnet_owner_test_backend_stats(const cnet_owner *owner,
                                   native_io_backend_stats *out_native,
                                   native_io_coroutine_stats *out_coroutine);
/** Captures the CNet routing token plus authoritative NativeIO identity for one request record. */
bool cnet_owner_test_get_request_snapshot(const cnet_owner *owner, size_t request_index,
                                          cnet_owner_test_request_snapshot *out_snapshot);
/** Observes NativeIO without routing the returned completion through the owner. */
int cnet_owner_test_observe_raw(cnet_owner *owner, native_io_completion *events,
                                size_t event_capacity, uint32_t timeout_ms,
                                size_t *out_count);
/** Routes every supplied completion and returns the first routing error after the whole batch. */
int cnet_owner_test_process_completion_batch(cnet_owner *owner,
                                             const native_io_completion *events,
                                             size_t count);
/** Expires deadlines independently of routing a previously observed native completion. */
int cnet_owner_test_process_deadlines(cnet_owner *owner);
/** Makes the next successful/already-pending native cancellation report SALTS_EALREADY. */
int cnet_owner_test_force_cancel_ealready_once(cnet_owner *owner);
/** Caps each test-build stream-send submission without changing logical send ownership. */
int cnet_owner_test_set_send_chunk_bytes(cnet_owner *owner, size_t bytes);
#endif

#if defined(CNET_INTERNAL_PROFILING)
/** Begins/takes a quiescent, single-owner diagnostic sample. */
int cnet_owner_profile_begin(cnet_owner *owner);
int cnet_owner_profile_take(cnet_owner *owner, cnet_owner_profile *out_profile);
#endif
int cnet_owner_tls_peer_certificate_sha256(
    cnet_owner *owner, cnet_session_handle session,
    char buffer[CNET_TLS_PEER_CERTIFICATE_SHA256_CAPACITY]);
int cnet_owner_tls_export_channel_binding(
    cnet_owner *owner, cnet_session_handle session,
    uint8_t output[CNET_TLS_CHANNEL_BINDING_BYTES]);

/** Clears owner metadata after the terminal notification recycled the handle. */
int cnet_owner_release_session(cnet_owner *owner, cnet_session_handle session);

/** Closes NativeIO admission after every session and request has settled. */
int cnet_owner_close(cnet_owner *owner);
int cnet_owner_destroy(cnet_owner *owner);

#endif /* CNET_OWNER_H */
