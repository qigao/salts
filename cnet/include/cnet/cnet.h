#ifndef CNET_CNET_H
#define CNET_CNET_H

#include <turbo/error_codes.h>
#include <turbo/native_io.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Caller-driven CNet client over NativeIO. */
typedef struct cnet_client {
  void *impl;
} cnet_client;

/** Single-owner nonblocking TCP listener used by CNet-based servers. */
typedef struct cnet_listener {
  void *impl;
} cnet_listener;

/** Reusable immutable TLS client context. */
typedef struct cnet_tls_client {
  void *impl;
} cnet_tls_client;

/** Reusable TLS server context. */
typedef struct cnet_tls_server {
  void *impl;
} cnet_tls_server;

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
/** Reports one successfully completed ordered write. */
typedef void (*cnet_send_fn)(void *user, cnet_connection connection, size_t size);

typedef struct cnet_observer {
  cnet_state_fn on_state;
  cnet_receive_fn on_receive;
  void *user;
  cnet_send_fn on_send;
} cnet_observer;

enum {
  CNET_TLS_ALPN_NAME_MAX_BYTES = 255,
  /** Minimum fixed capacity of each TLS network BIO and scratch buffer. */
  CNET_TLS_MIN_IO_BUFFER_BYTES = 17 * 1024
};

typedef enum cnet_tls_client_auth {
  CNET_TLS_CLIENT_AUTH_NONE = 0,
  CNET_TLS_CLIENT_AUTH_REQUIRED = 1
} cnet_tls_client_auth;

/**
 * Per-connection client policy. The configuration is consumed synchronously
 * by `cnet_connect()` and is not retained. Peer and hostname verification are
 * always enabled. NULL CA fields select the platform trust store.
 */
typedef struct cnet_tls_client_config {
  size_t size;
  /** Optional PEM trust file and hashed trust directory. */
  const char *ca_file;
  const char *ca_path;
  /** Optional PEM client certificate chain and private key; configure both or neither. */
  const char *cert_file;
  const char *key_file;
  const char *key_password;
  /** Optional verified identity and SNI override; NULL uses the URI host. */
  const char *server_name;
  /** Optional ordered ALPN offer; each non-empty name is at most 255 bytes. */
  const char *const *alpn_protocols;
  size_t alpn_protocol_count;
} cnet_tls_client_config;

/** Configuration copied into a reusable server context during initialization. */
typedef struct cnet_tls_server_config {
  size_t size;
  /** Required PEM server certificate chain and private key. */
  const char *cert_file;
  const char *key_file;
  const char *key_password;
  /** Required only when `client_auth` is REQUIRED. */
  const char *ca_file;
  const char *ca_path;
  cnet_tls_client_auth client_auth;
  /** Optional server-preference ALPN list. */
  const char *const *alpn_protocols;
  size_t alpn_protocol_count;
} cnet_tls_server_config;

typedef struct cnet_connect_options {
  const char *uri;
  cnet_observer observer;
  /** Optional TLS policy; valid only with `tls://`. NULL selects verified defaults. */
  const cnet_tls_client_config *tls;
  /** Optional reusable TLS policy; mutually exclusive with `tls`. */
  const cnet_tls_client *tls_client;
} cnet_connect_options;

/**
 * All capacities are hard bounds and must be positive. Command and event
 * capacities must be powers of two. Completion batch capacity cannot exceed
 * request capacity. Zero connect/read/write timeouts disable that deadline.
 */
typedef struct cnet_client_config {
  native_io_backend_kind backend;
  size_t connection_capacity;
  size_t command_capacity;
  size_t request_capacity;
  size_t completion_batch_capacity;
  size_t event_capacity;
  size_t max_send_bytes;
  size_t receive_buffer_bytes;
  uint32_t connect_timeout_ms;
  uint32_t read_timeout_ms;
  uint32_t write_timeout_ms;
  /** Zero disables TLS admission. Otherwise this is each TLS BIO/I/O hard bound. */
  size_t tls_io_buffer_bytes;
  /** Required with non-zero TLS storage; bounds only the TLS handshake. */
  uint32_t tls_handshake_timeout_ms;
} cnet_client_config;

/**
 * Listener configuration copied by `cnet_listener_init()`. `host` must be a
 * numeric IPv4 or IPv6 address. Port zero requests an ephemeral port. Backlog
 * is a positive hard bound accepted by the platform listener.
 */
typedef struct cnet_listener_config {
  native_io_backend_kind backend;
  const char *host;
  uint16_t port;
  size_t backlog;
} cnet_listener_config;

/**
 * Initializes one caller-driven NativeIO owner without creating an I/O worker
 * thread. Callbacks execute inline from `cnet_client_poll()` after a coroutine
 * observes its terminal NativeIO completion. All callbacks are ordered and
 * non-concurrent. After initialization, client operations and progress belong
 * to one calling thread; cross-thread admission requires an external mailbox.
 * A callback may issue send/receive/close but must not block.
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
 * If `observer.on_send` is present it runs once after the full write completes.
 * @return `TURBO_OK`, `TURBO_EMSGSIZE`, `TURBO_ENOENT` for a stale handle,
 * `TURBO_EBUSY` before connected, while another write is pending, or while
 * closing, plus a bounded queue error.
 */
int cnet_send(cnet_client *client, cnet_connection connection, const void *data, size_t size);

/**
 * Copies one final byte message and closes the stream only after that write
 * reaches its terminal completion. Once admitted, the connection immediately
 * rejects further send/receive work.
 */
int cnet_send_and_close(cnet_client *client, cnet_connection connection, const void *data,
                        size_t size);

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
 * Advances bounded CNet and NativeIO work on the calling thread.
 *
 * This client has one progress owner. Calls must not overlap and poll cannot
 * be entered recursively from a callback. State and receive callbacks execute
 * inline before this function returns. The call continues through internal
 * command and completion progress until it delivers at least one callback or
 * reaches the timeout. `timeout_ms == 0` performs one non-blocking progress
 * pass. An idle timeout is successful and reports zero events.
 *
 * @param client Initialized client owned by the calling progress thread.
 * @param timeout_ms Maximum time to wait for progress.
 * @param out_events Receives the number of callbacks delivered by this call.
 * @return `TURBO_OK`, `TURBO_EINVAL`, `TURBO_EBUSY` for concurrent or
 * recursive polling, `TURBO_ESHUTDOWN` after stop begins, or the first owner
 * progress error.
 */
int cnet_client_poll(cnet_client *client, uint32_t timeout_ms, size_t *out_events);

/**
 * Wakes a thread blocked in cnet_client_poll without publishing a callback or
 * changing connection state. This is the only progress-control operation that
 * may be called concurrently from a non-owner thread. Concurrent wakes are
 * coalesced. Wake callers must stop before client destroy.
 *
 * @return TURBO_OK, TURBO_EINVAL, TURBO_ESHUTDOWN, or a native wake error.
 */
int cnet_client_wake(cnet_client *client);

/**
 * Closes admission and live connections, then drives terminal callbacks and
 * coroutine completions to quiescence within `timeout_ms`. Retry after
 * `TURBO_ETIMEDOUT`.
 * Calling from this client's callback returns `TURBO_EBUSY`.
 * @return `TURBO_OK` only after quiescence, or the first drain/progress error.
 * A non-timeout progress error may be returned after quiescence was reached;
 * the caller must still attempt `cnet_client_destroy()` to release the client.
 */
int cnet_client_stop(cnet_client *client, uint32_t timeout_ms);

/**
 * Requires a completed stop. Calling from this client's callback is rejected.
 * A NULL internal implementation is already destroyed and returns `TURBO_OK`.
 */
int cnet_client_destroy(cnet_client *client);

/**
 * Builds one reusable fail-closed TLS 1.2+ client context. Configuration,
 * credentials, and ALPN are consumed synchronously and are not retained.
 * Peer and hostname verification remain mandatory.
 *
 * @param client Zero-initialized reusable output profile.
 * @param config Explicit trust, client identity, SNI, and ALPN policy.
 * @return `TURBO_OK`, `TURBO_EINVAL`, `TURBO_EALREADY`, `TURBO_ERANGE`,
 * `TURBO_ENOMEM`, or `TURBO_EIO` for trust/certificate/BoringSSL setup failure.
 */
int cnet_tls_client_init(cnet_tls_client *client, const cnet_tls_client_config *config);

/**
 * Releases the public context reference. Connections already admitted with
 * this profile retain their own reference. Repeated destroy is successful.
 * This control-plane call must not overlap init or connect using the same
 * wrapper.
 * @return `TURBO_OK`, or `TURBO_EINVAL` for a NULL wrapper.
 */
int cnet_tls_client_destroy(cnet_tls_client *client);

/**
 * Builds one fail-closed TLS 1.2+ server context. Certificate/key and ALPN
 * input are copied by BoringSSL or CNet before return. No partial context is
 * published on failure.
 *
 * @param server Zero-initialized reusable output context.
 * @param config Synchronously consumed certificate, trust, client-auth, and ALPN policy.
 * @return `TURBO_OK`, `TURBO_EINVAL`, `TURBO_EALREADY`, `TURBO_ERANGE`,
 * `TURBO_ENOMEM`, or `TURBO_EIO` for certificate/BoringSSL setup failure.
 */
int cnet_tls_server_init(cnet_tls_server *server, const cnet_tls_server_config *config);

/**
 * Releases the public context reference. Existing accepted sessions retain
 * their own reference. This control-plane call must not overlap init or accept
 * using the same `server` wrapper.
 * @return `TURBO_OK` (including an already destroyed wrapper), or `TURBO_EINVAL`.
 */
int cnet_tls_server_destroy(cnet_tls_server *server);

/**
 * Copies the negotiated ALPN protocol into `buffer` after a TLS connection is
 * open. `capacity` includes the trailing NUL and `out_size` excludes it.
 * Returns `TURBO_ENOENT` when no protocol was negotiated and `TURBO_ENOTSUP`
 * for a plaintext connection. Other errors are `TURBO_ENOTCONN`,
 * `TURBO_EMSGSIZE`, and the standard invalid/stale-handle statuses.
 */
int cnet_tls_negotiated_alpn(cnet_client *client, cnet_connection connection, char *buffer,
                             size_t capacity, size_t *out_size);

/**
 * Creates a nonblocking TCP listener. The listener has one owner thread;
 * ownership may move from initialization to a worker before the first wait or
 * accept, but calls must not overlap.
 */
int cnet_listener_init(cnet_listener *listener, const cnet_listener_config *config);

/** Returns the bound host-order port, including an OS-selected ephemeral port. */
int cnet_listener_port(const cnet_listener *listener, uint16_t *out_port);

/**
 * Waits for accept readiness. Timeout is successful with `out_ready == 0`.
 * This function does not accept or allocate a connection.
 */
int cnet_listener_wait(cnet_listener *listener, uint32_t timeout_ms, int *out_ready);

/**
 * Accepts at most one pending TCP peer and transfers its socket into `client`.
 * Success publishes a generation-checked handle and guarantees a later state
 * callback. No pending peer returns `TURBO_ETIMEDOUT`. Admission failure closes
 * the native peer and leaves `out_connection` zero.
 */
int cnet_listener_accept(cnet_listener *listener, cnet_client *client,
                         const cnet_observer *observer, cnet_connection *out_connection);

/**
 * Accepts one TCP peer and begins a server-side TLS handshake before
 * CONNECTED. The caller must not destroy `server` concurrently with this call.
 * @return The plaintext accept statuses, plus `TURBO_ENOTSUP` when `client`
 * has no bounded TLS storage or `TURBO_EINVAL` for an invalid TLS context.
 */
int cnet_listener_accept_tls(cnet_listener *listener, cnet_client *client,
                             const cnet_tls_server *server, const cnet_observer *observer,
                             cnet_connection *out_connection);

/** Closes listener admission. Existing CNet connections are unaffected. */
int cnet_listener_close(cnet_listener *listener);

/** Requires a closed listener and releases its platform-module reference. */
int cnet_listener_destroy(cnet_listener *listener);

#ifdef __cplusplus
}
#endif

#endif /* CNET_CNET_H */
