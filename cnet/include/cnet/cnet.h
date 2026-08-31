#ifndef CNET_CNET_H
#define CNET_CNET_H

#include <turbo/error_codes.h>
#include <turbo/native_io.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Experimental CNet client. Its ABI is not installed while CNet is gated. */
typedef struct cnet_client {
  void *impl;
} cnet_client;

/** Generation-checked value handle; never an OS descriptor or pointer. */
typedef struct cnet_connection {
  uint32_t slot;
  uint32_t generation;
} cnet_connection;

typedef enum cnet_connection_state {
  CNET_CONNECTION_CONNECTING = 1,
  CNET_CONNECTION_CONNECTED,
  CNET_CONNECTION_CLOSING,
  CNET_CONNECTION_CLOSED,
  CNET_CONNECTION_FAILED
} cnet_connection_state;

typedef enum cnet_message_kind { CNET_MESSAGE_BYTES = 1, CNET_MESSAGE_DATAGRAM } cnet_message_kind;

/** `stage` is a stable CNet-owned string. */
typedef struct cnet_error {
  int status;
  int native_status;
  const char *stage;
} cnet_error;

/** Borrowed view valid only for the duration of `on_receive`. */
typedef struct cnet_receive_view {
  const void *data;
  size_t size;
  cnet_message_kind kind;
} cnet_receive_view;

typedef void (*cnet_state_fn)(void *user, cnet_connection connection, cnet_connection_state state,
                              const cnet_error *error);
typedef void (*cnet_receive_fn)(void *user, cnet_connection connection,
                                const cnet_receive_view *view);

typedef struct cnet_observer {
  cnet_state_fn on_state;
  cnet_receive_fn on_receive;
  void *user;
} cnet_observer;

typedef struct cnet_connect_options {
  const char *uri;
  cnet_observer observer;
} cnet_connect_options;

/**
 * All capacities are hard bounds and must be positive. Command and event
 * capacities must be powers of two. Completion batch capacity cannot exceed
 * request capacity. Zero connect/read/write timeouts disable that deadline.
 */
typedef struct cnet_client_config {
  native_io_backend_kind backend;
  size_t io_shards;
  size_t callback_workers;
  size_t connection_capacity;
  size_t command_capacity_per_shard;
  size_t request_capacity_per_shard;
  size_t completion_batch_capacity;
  size_t event_capacity_per_shard;
  size_t max_send_bytes;
  size_t receive_buffer_bytes;
  uint32_t connect_timeout_ms;
  uint32_t read_timeout_ms;
  uint32_t write_timeout_ms;
} cnet_client_config;

/**
 * Starts NativeIO owner shards and their bounded SPSC callback channels.
 *
 * @param client Zero-initialized output owner.
 * @param config Borrowed configuration copied during initialization.
 * @return `TURBO_OK`, `TURBO_EINVAL` for an invalid bound/backend, or a
 * resource/backend initialization error. No partial client is published.
 */
int cnet_client_init(cnet_client *client, const cnet_client_config *config);

/**
 * Copies all options needed after return. On immediate failure `out_connection`
 * remains zero and no callback is delivered. `observer.on_state` is required.
 *
 * @return `TURBO_OK` for asynchronous admission, a URI/configuration error,
 * `TURBO_ENOBUFS` at the hard connection/command bound, or `TURBO_ESHUTDOWN`
 * after stop begins.
 */
int cnet_connect(cnet_client *client, const cnet_connect_options *options,
                 cnet_connection *out_connection);

/**
 * Copies `size` bytes into bounded command storage before returning success.
 * @return `TURBO_OK`, `TURBO_EMSGSIZE`, `TURBO_ENOENT` for a stale handle,
 * `TURBO_EBUSY` before connected/while closing, or a bounded queue error.
 */
int cnet_send(cnet_client *client, cnet_connection connection, const void *data, size_t size);

/**
 * Requests exactly `demand` future receive values. `on_receive` is required.
 * @return The same handle/state/queue errors as `cnet_send`; zero demand is
 * `TURBO_EINVAL`.
 */
int cnet_receive(cnet_client *client, cnet_connection connection, size_t demand);

/**
 * Asynchronously begins an idempotence-checked close transition.
 * @return `TURBO_OK`, `TURBO_EALREADY`, `TURBO_ENOENT`, `TURBO_ESHUTDOWN`, or
 * a bounded queue error.
 */
int cnet_close(cnet_client *client, cnet_connection connection);

/**
 * Closes admission and live connections, delivers terminal callbacks, then
 * joins all workers within `timeout_ms`. Retry after `TURBO_ETIMEDOUT`.
 * Calling from this client's callback returns `TURBO_EBUSY`.
 * @return `TURBO_OK` only after quiescence, or the first drain/worker error.
 */
int cnet_client_stop(cnet_client *client, uint32_t timeout_ms);

/**
 * Requires a completed stop. Calling from this client's callback is rejected.
 * A NULL internal implementation is already destroyed and returns `TURBO_OK`.
 */
int cnet_client_destroy(cnet_client *client);

#ifdef __cplusplus
}
#endif

#endif /* CNET_CNET_H */
