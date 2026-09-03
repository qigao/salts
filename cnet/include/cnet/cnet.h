#ifndef CNET_CNET_H
#define CNET_CNET_H

#include <salts/error_codes.h>
#include <salts/native_io.h>

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

/** Caller-driven bound UDP socket over NativeIO. The wrapper address stays stable until destroy. */
typedef struct cnet_datagram {
  void *impl;
} cnet_datagram;

/** Caller-driven bounded KCP protocol session. The wrapper address stays stable until destroy. */
typedef struct cnet_kcp {
  void *impl;
} cnet_kcp;

/** Caller-driven authenticated KCP v1 session with Reed-Solomon FEC. */
typedef struct cnet_secure_kcp {
  void *impl;
} cnet_secure_kcp;

/** Unified bound endpoint for unreliable UDP or reliable ordered KCP messages. */
typedef struct cnet_packet_endpoint {
  void *impl;
} cnet_packet_endpoint;

/** Generation-checked packet peer/session handle; never an OS descriptor or pointer. */
typedef struct cnet_packet_session {
  uint32_t slot;
  uint32_t generation;
} cnet_packet_session;

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

/** Fixed RFC 9266-style TLS exporter binding used to bind application handshakes to one connection. */
#define CNET_TLS_CHANNEL_BINDING_BYTES 32u

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

/** One immutable byte range borrowed by a synchronous CNet admission call. */
typedef struct cnet_const_buffer {
  const void *data;
  size_t size;
} cnet_const_buffer;

typedef enum cnet_datagram_address_family {
  CNET_DATAGRAM_ADDRESS_IPV4 = 4,
  CNET_DATAGRAM_ADDRESS_IPV6 = 6
} cnet_datagram_address_family;

/** Portable copied UDP peer address. Multi-byte scalar fields are in host byte order. */
typedef struct cnet_datagram_peer {
  cnet_datagram_address_family family;
  uint16_t port;
  uint32_t scope_id;
  uint8_t address[16];
} cnet_datagram_peer;

/** Portable copied TCP peer address. Multi-byte scalar fields are in host byte order. */
typedef struct cnet_stream_peer {
  cnet_datagram_address_family family;
  uint16_t port;
  uint32_t scope_id;
  uint8_t address[16];
} cnet_stream_peer;

typedef void (*cnet_datagram_receive_fn)(void *user, cnet_datagram *datagram,
                                         const cnet_datagram_peer *peer,
                                         const cnet_receive_view *view);
/** Terminal result for one successfully admitted copied datagram, including its copied tag. */
typedef void (*cnet_datagram_send_fn)(void *user, cnet_datagram *datagram,
                                      const cnet_datagram_peer *peer, size_t size, int status,
                                      uint64_t tag);

typedef struct cnet_datagram_observer {
  cnet_datagram_receive_fn on_receive;
  cnet_datagram_send_fn on_send;
  void *user;
} cnet_datagram_observer;

/** Borrowed wire packet that must be copied or synchronously admitted before return. */
typedef int (*cnet_kcp_output_fn)(void *user, cnet_kcp *session, const void *data, size_t size);
/** Borrowed complete message valid only for the duration of the callback. */
typedef void (*cnet_kcp_receive_fn)(void *user, cnet_kcp *session,
                                    const cnet_receive_view *view);

typedef struct cnet_kcp_observer {
  cnet_kcp_output_fn output;
  cnet_kcp_receive_fn on_receive;
  void *user;
} cnet_kcp_observer;

typedef enum cnet_secure_kcp_role {
  CNET_SECURE_KCP_CLIENT = 1,
  CNET_SECURE_KCP_SERVER = 2
} cnet_secure_kcp_role;

typedef enum cnet_kcp_security_mode {
  CNET_KCP_SECURITY_NONE = 0,
  /** CoroNet-compatible TKSH/TKSR/TKF1 authenticated encryption and FEC. */
  CNET_KCP_SECURITY_PSK_V1 = 1
} cnet_kcp_security_mode;

typedef enum cnet_kcp_fec_backend {
  CNET_KCP_FEC_NONE = 0,
  CNET_KCP_FEC_REED_SOLOMON = 1
} cnet_kcp_fec_backend;

typedef int (*cnet_secure_kcp_output_fn)(void *user, cnet_secure_kcp *session,
                                         const void *data, size_t size);
typedef void (*cnet_secure_kcp_receive_fn)(void *user, cnet_secure_kcp *session,
                                           const cnet_receive_view *view);
typedef void (*cnet_secure_kcp_established_fn)(void *user, cnet_secure_kcp *session);

typedef struct cnet_secure_kcp_observer {
  cnet_secure_kcp_output_fn output;
  cnet_secure_kcp_receive_fn on_receive;
  cnet_secure_kcp_established_fn on_established;
  void *user;
} cnet_secure_kcp_observer;

typedef enum cnet_packet_protocol {
  CNET_PACKET_UDP = 1,
  CNET_PACKET_KCP = 2
} cnet_packet_protocol;

typedef struct cnet_packet_session_info {
  cnet_packet_protocol protocol;
  cnet_datagram_peer peer;
  uint32_t conversation;
} cnet_packet_session_info;

typedef enum cnet_packet_session_state {
  CNET_PACKET_SESSION_OPEN = 1,
  CNET_PACKET_SESSION_CLOSED = 2,
  CNET_PACKET_SESSION_CONNECTING = 3
} cnet_packet_session_state;

/** Return SALTS_OK to admit an unknown inbound peer/session; any other value rejects it. */
typedef int (*cnet_packet_admit_fn)(void *user, cnet_packet_endpoint *endpoint,
                                    cnet_packet_protocol protocol,
                                    const cnet_datagram_peer *peer, uint32_t conversation);
typedef void (*cnet_packet_state_fn)(void *user, cnet_packet_endpoint *endpoint,
                                     cnet_packet_session session,
                                     cnet_packet_session_state state,
                                     const cnet_datagram_peer *peer, uint32_t conversation);
typedef void (*cnet_packet_receive_fn)(void *user, cnet_packet_endpoint *endpoint,
                                       cnet_packet_session session,
                                       const cnet_receive_view *view);
typedef void (*cnet_packet_error_fn)(void *user, cnet_packet_endpoint *endpoint,
                                     cnet_packet_session session, int status);

typedef struct cnet_packet_observer {
  cnet_packet_admit_fn on_admit;
  cnet_packet_state_fn on_state;
  cnet_packet_receive_fn on_receive;
  cnet_packet_error_fn on_error;
  void *user;
} cnet_packet_observer;

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
  /** Lowercase SHA-256 hexadecimal digest plus the trailing NUL. */
  CNET_TLS_PEER_CERTIFICATE_SHA256_CAPACITY = 65,
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
 * Optional OS policy for future TCP-backed connections owned by one client.
 * Zero buffer sizes preserve platform defaults. Millisecond durations are
 * rounded up when a platform exposes only whole-second socket options.
 * Keepalive detail requires `keepalive == 1`; `linger_ms == 0` with
 * `linger == 1` requests abortive close.
 */
typedef struct cnet_stream_socket_options {
  size_t size;
  size_t receive_buffer_bytes;
  size_t send_buffer_bytes;
  uint32_t keepalive_idle_ms;
  uint32_t keepalive_interval_ms;
  uint32_t keepalive_count;
  uint32_t linger_ms;
  int keepalive;
  int linger;
} cnet_stream_socket_options;

#define CNET_STREAM_SOCKET_OPTIONS_INIT                                                           \
  {sizeof(cnet_stream_socket_options), 0u, 0u, 0u, 0u, 0u, 0u, 0, 0}

/** Optional listener policy consumed synchronously by `cnet_listener_init_ex()`. */
typedef struct cnet_listener_options {
  size_t size;
  /** Request SO_REUSEPORT. Unsupported platforms return `SALTS_ENOTSUP`. */
  int reuse_port;
} cnet_listener_options;

#define CNET_LISTENER_OPTIONS_INIT {sizeof(cnet_listener_options), 0}

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

enum { CNET_DATAGRAM_MAX_PAYLOAD_BYTES = 65507u };

/**
 * Every capacity is a hard bound. One request is reserved for receive, so
 * request_capacity must be greater than send_capacity. The receive buffer must
 * cover max_datagram_bytes; truncation is never reported as a successful value.
 */
typedef struct cnet_datagram_config {
  size_t size;
  native_io_backend_kind backend;
  const char *host;
  uint16_t port;
  size_t send_capacity;
  size_t request_capacity;
  size_t completion_batch_capacity;
  size_t max_datagram_bytes;
  size_t receive_buffer_bytes;
  cnet_datagram_observer observer;
  /** Request SO_REUSEPORT before bind. Unsupported platforms return `SALTS_ENOTSUP`. */
  int reuse_port;
} cnet_datagram_config;

#define CNET_DATAGRAM_CONFIG_INIT                                                                  \
  {sizeof(cnet_datagram_config), (native_io_backend_kind)0, NULL, 0u, 0u, 0u, 0u, 0u, 0u,          \
   {NULL, NULL, NULL}, 0}

enum {
  CNET_KCP_DEFAULT_MTU = 1400,
  CNET_KCP_DEFAULT_WINDOW = 128,
  CNET_KCP_DEFAULT_INTERVAL_MS = 10,
  CNET_KCP_DEFAULT_FAST_RESEND = 2,
  CNET_KCP_DEFAULT_SEND_SEGMENT_CAPACITY = 1024,
  CNET_KCP_DEFAULT_MAX_MESSAGE_BYTES = 1024 * 1024
};

/**
 * KCP is a reliable ordered protocol over untrusted datagrams; it provides no
 * encryption or peer authentication. All protocol calls belong to one owner.
 * send_segment_capacity bounds retained outbound KCP segments, receive_window
 * bounds inbound segments, and max_message_bytes bounds callback storage.
 */
typedef struct cnet_kcp_config {
  size_t size;
  uint32_t conversation;
  uint32_t mtu;
  uint32_t send_window;
  uint32_t receive_window;
  uint32_t interval_ms;
  uint32_t fast_resend;
  bool no_congestion_window;
  bool stream_mode;
  size_t send_segment_capacity;
  size_t max_message_bytes;
  cnet_kcp_observer observer;
} cnet_kcp_config;

#define CNET_KCP_CONFIG_INIT                                                                       \
  {sizeof(cnet_kcp_config), 0u, CNET_KCP_DEFAULT_MTU, CNET_KCP_DEFAULT_WINDOW,                     \
   CNET_KCP_DEFAULT_WINDOW, CNET_KCP_DEFAULT_INTERVAL_MS, CNET_KCP_DEFAULT_FAST_RESEND, false,     \
   false, CNET_KCP_DEFAULT_SEND_SEGMENT_CAPACITY, CNET_KCP_DEFAULT_MAX_MESSAGE_BYTES,              \
   {NULL, NULL, NULL}}

enum {
  CNET_KCP_PSK_BYTES = 32,
  CNET_KCP_SECURE_RECORD_OVERHEAD = 48,
  CNET_KCP_DEFAULT_HANDSHAKE_RETRY_MS = 200,
  CNET_KCP_DEFAULT_FEC_DATA_SHARDS = 8,
  CNET_KCP_DEFAULT_FEC_PARITY_SHARDS = 2,
  CNET_KCP_DEFAULT_FEC_MAX_PAYLOAD_BYTES = 1248,
  CNET_KCP_DEFAULT_FEC_RECEIVE_GROUPS = 16
};

typedef struct cnet_kcp_fec_config {
  cnet_kcp_fec_backend backend;
  uint16_t data_shards;
  uint16_t parity_shards;
  uint16_t max_payload_bytes;
  uint16_t receive_group_count;
} cnet_kcp_fec_config;

/** Copied PSK v1 policy. A zero key, NONE FEC, or partial FEC config is rejected. */
typedef struct cnet_kcp_security_config {
  size_t size;
  cnet_kcp_security_mode mode;
  uint8_t pre_shared_key[CNET_KCP_PSK_BYTES];
  uint32_t handshake_retry_ms;
  cnet_kcp_fec_config fec;
} cnet_kcp_security_config;

#define CNET_KCP_SECURITY_CONFIG_INIT                                                              \
  {sizeof(cnet_kcp_security_config), CNET_KCP_SECURITY_NONE, {0},                                  \
   CNET_KCP_DEFAULT_HANDSHAKE_RETRY_MS,                                                            \
   {CNET_KCP_FEC_REED_SOLOMON, CNET_KCP_DEFAULT_FEC_DATA_SHARDS,                                  \
    CNET_KCP_DEFAULT_FEC_PARITY_SHARDS, CNET_KCP_DEFAULT_FEC_MAX_PAYLOAD_BYTES,                    \
    CNET_KCP_DEFAULT_FEC_RECEIVE_GROUPS}}

/**
 * Protocol-only secure session. The embedded KCP conversation and observer
 * fields must be zero because the authenticated session derives and owns them.
 */
typedef struct cnet_secure_kcp_config {
  size_t size;
  cnet_secure_kcp_role role;
  cnet_kcp_config kcp;
  cnet_kcp_security_config security;
  cnet_secure_kcp_observer observer;
} cnet_secure_kcp_config;

#define CNET_SECURE_KCP_CONFIG_INIT                                                               \
  {sizeof(cnet_secure_kcp_config), (cnet_secure_kcp_role)0, CNET_KCP_CONFIG_INIT,                  \
   CNET_KCP_SECURITY_CONFIG_INIT, {NULL, NULL, NULL, NULL}}

/**
 * Unified packet endpoint configuration. datagram and kcp observer fields
 * must remain zero because the endpoint owns their internal composition.
 * session_capacity is a hard bound for the pre-reserved peer index and slots.
 */
typedef struct cnet_packet_endpoint_config {
  size_t size;
  cnet_packet_protocol protocol;
  size_t session_capacity;
  cnet_datagram_config datagram;
  cnet_kcp_config kcp;
  cnet_kcp_security_config security;
  cnet_packet_observer observer;
} cnet_packet_endpoint_config;

#define CNET_PACKET_ENDPOINT_CONFIG_INIT                                                           \
  {sizeof(cnet_packet_endpoint_config), (cnet_packet_protocol)0, 0u, CNET_DATAGRAM_CONFIG_INIT,    \
   CNET_KCP_CONFIG_INIT, CNET_KCP_SECURITY_CONFIG_INIT, {NULL, NULL, NULL, NULL, NULL}}

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
 * @return `SALTS_OK`, `SALTS_EINVAL` for an invalid bound/backend, or a
 * resource/backend initialization error. No partial client is published.
 */
int cnet_client_init(cnet_client *client, const cnet_client_config *config);

/** Validates bounds and option dependencies without touching a socket. */
int cnet_stream_socket_options_validate(const cnet_stream_socket_options *options);

/**
 * Copies policy for future TCP/TLS connections. No active connection may
 * exist while replacing this client-global policy.
 */
int cnet_client_set_stream_socket_options(cnet_client *client,
                                          const cnet_stream_socket_options *options);

/**
 * Copies all options needed after return. On immediate failure `out_connection`
 * remains zero and no callback is delivered. `observer.on_state` is required.
 *
 * @return `SALTS_OK` for asynchronous admission, a URI/configuration error,
 * `SALTS_ENOBUFS` at the hard connection/command bound, or `SALTS_ESHUTDOWN`
 * after stop begins.
 */
int cnet_connect(cnet_client *client, const cnet_connect_options *options,
                 cnet_connection *out_connection);

/**
 * Copies `size` bytes into bounded command storage before returning success.
 * If `observer.on_send` is present it runs once after the full write completes.
 * @return `SALTS_OK`, `SALTS_EMSGSIZE`, `SALTS_ENOENT` for a stale handle,
 * `SALTS_EBUSY` before connected, while another write is pending, or while
 * closing, plus a bounded queue error.
 */
int cnet_send(cnet_client *client, cnet_connection connection, const void *data, size_t size);

/**
 * Copies the ordered concatenation of immutable, non-empty `segments` into
 * one bounded command slot before returning success. The descriptor array and
 * its backing ranges are borrowed only for this call. Completion, ordering,
 * busy-state, and queue errors are identical to `cnet_send`; `on_send` reports
 * the checked total byte count once. NULL data, empty segments, or zero count
 * return `SALTS_EINVAL`; an overflowing or oversized total returns
 * `SALTS_EMSGSIZE` without admitting a write.
 */
int cnet_sendv(cnet_client *client, cnet_connection connection,
               const cnet_const_buffer *segments, size_t segment_count);

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
 * `SALTS_EINVAL`.
 */
int cnet_receive(cnet_client *client, cnet_connection connection, size_t demand);

/**
 * Asynchronously begins an idempotence-checked close transition.
 * @return `SALTS_OK`, `SALTS_EALREADY`, `SALTS_ENOENT`, `SALTS_ESHUTDOWN`, or
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
 * @return `SALTS_OK`, `SALTS_EINVAL`, `SALTS_EBUSY` for concurrent or
 * recursive polling, `SALTS_ESHUTDOWN` after stop begins, or the first owner
 * progress error.
 */
int cnet_client_poll(cnet_client *client, uint32_t timeout_ms, size_t *out_events);

/**
 * Wakes a thread blocked in cnet_client_poll without publishing a callback or
 * changing connection state. This is the only progress-control operation that
 * may be called concurrently from a non-owner thread. Concurrent wakes are
 * coalesced. Wake callers must stop before client destroy.
 *
 * @return SALTS_OK, SALTS_EINVAL, SALTS_ESHUTDOWN, or a native wake error.
 */
int cnet_client_wake(cnet_client *client);

/**
 * Closes admission and live connections, then drives terminal callbacks and
 * coroutine completions to quiescence within `timeout_ms`. Retry after
 * `SALTS_ETIMEDOUT`.
 * Calling from this client's callback returns `SALTS_EBUSY`.
 * @return `SALTS_OK` only after quiescence, or the first drain/progress error.
 * A non-timeout progress error may be returned after quiescence was reached;
 * the caller must still attempt `cnet_client_destroy()` to release the client.
 */
int cnet_client_stop(cnet_client *client, uint32_t timeout_ms);

/**
 * Requires a completed stop. Calling from this client's callback is rejected.
 * A NULL internal implementation is already destroyed and returns `SALTS_OK`.
 */
int cnet_client_destroy(cnet_client *client);

/**
 * Builds one reusable fail-closed TLS 1.2+ client context. Configuration,
 * credentials, and ALPN are consumed synchronously and are not retained.
 * Peer and hostname verification remain mandatory.
 *
 * @param client Zero-initialized reusable output profile.
 * @param config Explicit trust, client identity, SNI, and ALPN policy.
 * @return `SALTS_OK`, `SALTS_EINVAL`, `SALTS_EALREADY`, `SALTS_ERANGE`,
 * `SALTS_ENOMEM`, or `SALTS_EIO` for trust/certificate/BoringSSL setup failure.
 */
int cnet_tls_client_init(cnet_tls_client *client, const cnet_tls_client_config *config);

/**
 * Releases the public context reference. Connections already admitted with
 * this profile retain their own reference. Repeated destroy is successful.
 * This control-plane call must not overlap init or connect using the same
 * wrapper.
 * @return `SALTS_OK`, or `SALTS_EINVAL` for a NULL wrapper.
 */
int cnet_tls_client_destroy(cnet_tls_client *client);

/**
 * Builds one fail-closed TLS 1.2+ server context. Certificate/key and ALPN
 * input are copied by BoringSSL or CNet before return. No partial context is
 * published on failure.
 *
 * @param server Zero-initialized reusable output context.
 * @param config Synchronously consumed certificate, trust, client-auth, and ALPN policy.
 * @return `SALTS_OK`, `SALTS_EINVAL`, `SALTS_EALREADY`, `SALTS_ERANGE`,
 * `SALTS_ENOMEM`, or `SALTS_EIO` for certificate/BoringSSL setup failure.
 */
int cnet_tls_server_init(cnet_tls_server *server, const cnet_tls_server_config *config);

/**
 * Releases the public context reference. Existing accepted sessions retain
 * their own reference. This control-plane call must not overlap init or accept
 * using the same `server` wrapper.
 * @return `SALTS_OK` (including an already destroyed wrapper), or `SALTS_EINVAL`.
 */
int cnet_tls_server_destroy(cnet_tls_server *server);

/**
 * Copies the negotiated ALPN protocol into `buffer` after a TLS connection is
 * open. `capacity` includes the trailing NUL and `out_size` excludes it.
 * Returns `SALTS_ENOENT` when no protocol was negotiated and `SALTS_ENOTSUP`
 * for a plaintext connection. Other errors are `SALTS_ENOTCONN`,
 * `SALTS_EMSGSIZE`, and the standard invalid/stale-handle statuses.
 */
int cnet_tls_negotiated_alpn(cnet_client *client, cnet_connection connection, char *buffer,
                             size_t capacity, size_t *out_size);

/**
 * Copies the verified peer leaf certificate SHA-256 digest as 64 lowercase
 * hexadecimal characters plus a trailing NUL. The query is valid only while
 * the TLS connection remains open. A server connection without a presented
 * client certificate returns `SALTS_ENOENT`; plaintext returns `SALTS_ENOTSUP`.
 * Other errors are `SALTS_ENOTCONN` and the standard invalid/stale-handle
 * statuses.
 */
int cnet_tls_peer_certificate_sha256(
    cnet_client *client, cnet_connection connection,
    char buffer[CNET_TLS_PEER_CERTIFICATE_SHA256_CAPACITY]);

/**
 * Exports exactly CNET_TLS_CHANNEL_BINDING_BYTES using the
 * `EXPORTER-Channel-Binding` label and an explicit empty context. The query is
 * valid only while the TLS connection remains open. Plaintext returns
 * `SALTS_ENOTSUP`; incomplete TLS handshakes return `SALTS_ENOTCONN`.
 */
int cnet_tls_export_channel_binding(
    cnet_client *client, cnet_connection connection,
    uint8_t output[CNET_TLS_CHANNEL_BINDING_BYTES]);

/**
 * Creates a nonblocking TCP listener. The listener has one owner thread;
 * ownership may move from initialization to a worker before the first wait or
 * accept, but calls must not overlap.
 */
int cnet_listener_init(cnet_listener *listener, const cnet_listener_config *config);

/** Validates the versioned listener policy without creating a socket. */
int cnet_listener_options_validate(const cnet_listener_options *options);

/** Creates a listener with an explicit, synchronously copied socket policy. */
int cnet_listener_init_ex(cnet_listener *listener, const cnet_listener_config *config,
                          const cnet_listener_options *options);

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
 * callback. No pending peer returns `SALTS_ETIMEDOUT`. Admission failure closes
 * the native peer and leaves `out_connection` zero.
 */
int cnet_listener_accept(cnet_listener *listener, cnet_client *client,
                         const cnet_observer *observer, cnet_connection *out_connection);

/**
 * Accepts one plaintext TCP peer and also copies its portable remote endpoint.
 * `out_peer` is cleared on failure and remains independent of the connection lifetime.
 */
int cnet_listener_accept_peer(cnet_listener *listener, cnet_client *client,
                              const cnet_observer *observer, cnet_connection *out_connection,
                              cnet_stream_peer *out_peer);

/**
 * Accepts one TCP peer and begins a server-side TLS handshake before
 * CONNECTED. The caller must not destroy `server` concurrently with this call.
 * @return The plaintext accept statuses, plus `SALTS_ENOTSUP` when `client`
 * has no bounded TLS storage or `SALTS_EINVAL` for an invalid TLS context.
 */
int cnet_listener_accept_tls(cnet_listener *listener, cnet_client *client,
                             const cnet_tls_server *server, const cnet_observer *observer,
                             cnet_connection *out_connection);

/** TLS counterpart of `cnet_listener_accept_peer()`. */
int cnet_listener_accept_tls_peer(cnet_listener *listener, cnet_client *client,
                                  const cnet_tls_server *server,
                                  const cnet_observer *observer,
                                  cnet_connection *out_connection, cnet_stream_peer *out_peer);

/** Closes listener admission. Existing CNet connections are unaffected. */
int cnet_listener_close(cnet_listener *listener);

/** Requires a closed listener and releases its platform-module reference. */
int cnet_listener_destroy(cnet_listener *listener);

/**
 * Binds one numeric IPv4/IPv6 UDP endpoint and preallocates all request,
 * receive, send and completion storage. No worker thread is created.
 */
int cnet_datagram_init(cnet_datagram *datagram, const cnet_datagram_config *config);

/** Returns the host-order bound port, including an OS-selected ephemeral port. */
int cnet_datagram_port(const cnet_datagram *datagram, uint16_t *out_port);

/**
 * Adds exactly demand receive values. Each successful callback consumes one;
 * overflow fails without changing current demand.
 */
int cnet_datagram_receive(cnet_datagram *datagram, size_t demand);

/**
 * Copies peer and payload into one bounded send slot before returning success.
 * The opaque tag is copied with the peer and payload. A successful call
 * publishes exactly one terminal on_send callback with the same tag.
 */
int cnet_datagram_send(cnet_datagram *datagram, const cnet_datagram_peer *peer,
                       const void *data, size_t size, uint64_t tag);

/**
 * Advances NativeIO and invokes callbacks inline on the non-overlapping owner.
 * An idle timeout is successful with zero events.
 */
int cnet_datagram_poll(cnet_datagram *datagram, uint32_t timeout_ms, size_t *out_events);

/** The only datagram operation allowed concurrently from a non-owner thread. */
int cnet_datagram_wake(cnet_datagram *datagram);

/** Closes admission, cancels retained I/O and drains terminal send callbacks. */
int cnet_datagram_stop(cnet_datagram *datagram, uint32_t timeout_ms);

/** Requires a completed stop and releases all fixed storage. */
int cnet_datagram_destroy(cnet_datagram *datagram);

/**
 * Creates one protocol-only session from a zero-initialized owner and
 * preallocates its receive message buffer. A live owner returns SALTS_EALREADY.
 */
int cnet_kcp_init(cnet_kcp *session, const cnet_kcp_config *config);

/**
 * Copies one ordered message into KCP after conservative segment-capacity admission.
 * Returns SALTS_ENOBUFS without retaining input when the hard bound would be exceeded.
 */
int cnet_kcp_send(cnet_kcp *session, const void *data, size_t size);

/** Borrows one wire datagram until return and synchronously emits complete messages. */
int cnet_kcp_input(cnet_kcp *session, const void *data, size_t size);

/** Advances the KCP clock and synchronously emits wire packets through output. */
int cnet_kcp_update(cnet_kcp *session, uint32_t now_ms);

/** Returns KCP's next absolute 32-bit millisecond update time. */
int cnet_kcp_check(const cnet_kcp *session, uint32_t now_ms, uint32_t *out_next_ms);

/** Releases all queued segments and receive storage; no UDP/timer calls may overlap. */
int cnet_kcp_destroy(cnet_kcp *session);

/** Creates a zero-initialized PSK v1/FEC session without emitting wire data. */
int cnet_secure_kcp_init(cnet_secure_kcp *session, const cnet_secure_kcp_config *config);

/** Starts the role state machine; a client synchronously emits its first authenticated hello. */
int cnet_secure_kcp_start(cnet_secure_kcp *session, uint32_t now_ms);

/** Returns true only after the authenticated hello exchange and KCP activation complete. */
bool cnet_secure_kcp_established(const cnet_secure_kcp *session);

/** Copies the session-derived non-zero KCP conversation after establishment. */
int cnet_secure_kcp_conversation(const cnet_secure_kcp *session, uint32_t *out_conversation);

/** Copies one application message into bounded KCP storage; handshaking returns SALTS_EBUSY. */
int cnet_secure_kcp_send(cnet_secure_kcp *session, const void *data, size_t size);

/** Borrows one complete TKSH or TKF1 datagram and authenticates it before state mutation. */
int cnet_secure_kcp_input(cnet_secure_kcp *session, const void *data, size_t size);

/** Advances handshake retry and established KCP timers. */
int cnet_secure_kcp_update(cnet_secure_kcp *session, uint32_t now_ms);

/** Returns the next absolute millisecond deadline for handshake or KCP progress. */
int cnet_secure_kcp_check(const cnet_secure_kcp *session, uint32_t now_ms,
                          uint32_t *out_next_ms);

/** Releases KCP/FEC storage and wipes copied and derived secret material. */
int cnet_secure_kcp_destroy(cnet_secure_kcp *session);

/**
 * Initializes a zero-initialized fixed-capacity UDP/KCP endpoint and arms
 * continuous bounded receive. A live owner returns SALTS_EALREADY.
 */
int cnet_packet_endpoint_init(cnet_packet_endpoint *endpoint,
                              const cnet_packet_endpoint_config *config);

/** Returns the host-order UDP port shared by all endpoint sessions. */
int cnet_packet_endpoint_port(const cnet_packet_endpoint *endpoint, uint16_t *out_port);

bool cnet_packet_session_valid(cnet_packet_session session);

/** Copies immutable protocol, peer and conversation identity for a live session. */
int cnet_packet_session_get_info(const cnet_packet_endpoint *endpoint,
                                 cnet_packet_session session,
                                 cnet_packet_session_info *out_info);

/**
 * Explicitly opens one peer mapping. UDP requires conversation zero. Plain KCP
 * requires a non-zero conversation id; authenticated KCP requires zero because
 * its conversation id is derived during the handshake. Duplicate keys return
 * SALTS_EALREADY.
 */
int cnet_packet_session_open(cnet_packet_endpoint *endpoint, const cnet_datagram_peer *peer,
                             uint32_t conversation, cnet_packet_session *out_session);

/** Closes one generation-checked session after its copied UDP writes drain. */
int cnet_packet_session_close(cnet_packet_endpoint *endpoint, cnet_packet_session session);

/** Copies and admits one UDP datagram or one reliable ordered KCP message. */
int cnet_packet_send(cnet_packet_endpoint *endpoint, cnet_packet_session session,
                     const void *data, size_t size);

/** Drives socket completions and all due KCP timers on the caller-owned lane. */
int cnet_packet_poll(cnet_packet_endpoint *endpoint, uint32_t timeout_ms, size_t *out_events);

/** The only packet endpoint operation allowed concurrently from a non-owner thread. */
int cnet_packet_wake(cnet_packet_endpoint *endpoint);

/** Stops admission, KCP timers and UDP I/O, then drains terminal socket writes. */
int cnet_packet_endpoint_stop(cnet_packet_endpoint *endpoint, uint32_t timeout_ms);

/** Requires completed stop and releases the peer index and all fixed storage. */
int cnet_packet_endpoint_destroy(cnet_packet_endpoint *endpoint);

#ifdef __cplusplus
}
#endif

#endif /* CNET_CNET_H */
