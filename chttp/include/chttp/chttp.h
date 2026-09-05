#ifndef CHTTP_CHTTP_H
#define CHTTP_CHTTP_H

#include <cnet/cnet.h>
#include <salts/error_codes.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Blocking requests-style HTTP/1 client; one instance admits one call at a time. */
typedef struct chttp_client {
  void *impl;
} chttp_client;

/** Advanced caller-driven client for RPC, executors, and event-loop adapters. */
typedef struct chttp_async_client {
  void *impl;
} chttp_async_client;

/** Reusable immutable HTTPS policy and connection-pool identity. */
typedef struct chttp_tls_profile {
  void *impl;
} chttp_tls_profile;

/** Background HTTP/1.1 and optional HTTP/2 server owner; callers never drive a poller. */
typedef struct chttp_server {
  void *impl;
} chttp_server;

/**
 * One generation-checked deferred HTTP/1.1 response. The handle is completed
 * exactly once by `chttp_server_deferred_reply()` and must not outlive the
 * server's successful stop.
 */
typedef struct chttp_server_deferred {
  void *impl;
  uint32_t generation;
  uint32_t reserved;
} chttp_server_deferred;

#define CHTTP_SERVER_DEFERRED_INIT {NULL, 0u, 0u}

/** Handler-scoped server-side session. */
typedef struct chttp_session {
  void *impl;
} chttp_session;

/** Handler-scoped continuation for one middleware invocation. */
typedef struct chttp_server_next {
  void *impl;
} chttp_server_next;

/** Callback-scoped server WebSocket peer. The wrapper becomes invalid when its callback returns. */
typedef struct chttp_websocket {
  void *impl;
} chttp_websocket;

/**
 * Generation-checked server WebSocket session captured from a callback-scoped
 * peer. The value does not keep the connection alive and must not outlive its
 * server.
 */
typedef struct chttp_server_websocket_session {
  void *impl;
  uint32_t connection_slot;
  uint32_t connection_generation;
  int32_t stream_id;
} chttp_server_websocket_session;

/** Single-owner requests-style WebSocket client; public calls drive CNet internally. */
typedef struct chttp_websocket_client {
  void *impl;
} chttp_websocket_client;

/** Single-owner RFC 8441 session pool; public calls drive one shared H2 connection internally. */
typedef struct chttp_websocket_pool {
  void *impl;
} chttp_websocket_pool;

/** Generation-checked WebSocket stream handle; never a pointer or CNet handle. */
typedef struct chttp_websocket_session {
  uint32_t slot;
  uint32_t generation;
} chttp_websocket_session;

/** Generation-checked request handle; never a pointer or CNet handle. */
typedef struct chttp_request {
  uint32_t slot;
  uint32_t generation;
} chttp_request;

/** Wire protocol selected for one request. Zero preserves the HTTP/1.1 default. */
typedef enum chttp_protocol { CHTTP_HTTP_1_1 = 0, CHTTP_HTTP_2 = 1 } chttp_protocol;

typedef enum chttp_method {
  CHTTP_METHOD_GET = 1,
  CHTTP_METHOD_HEAD,
  CHTTP_METHOD_POST,
  CHTTP_METHOD_PUT,
  CHTTP_METHOD_DELETE,
  CHTTP_METHOD_PATCH,
  CHTTP_METHOD_OPTIONS,
  /** Observed by RFC 8441 WebSocket callbacks; not a generic routable method. */
  CHTTP_METHOD_CONNECT
} chttp_method;

/** Input strings are borrowed until submit returns; response strings are callback-scoped. */
typedef struct chttp_header {
  const char *name;
  const char *value;
} chttp_header;

/** Optional registered claims used to issue one HS256 JWT. Zero time values are omitted. */
typedef struct chttp_jwt_claims {
  const char *issuer;
  const char *subject;
  const char *audience;
  const char *jwt_id;
  int64_t issued_at;
  int64_t not_before;
  int64_t expires_at;
} chttp_jwt_claims;

/**
 * Verified JWT claims borrowed from the active server callback. Time pointers
 * are NULL when the corresponding registered claim was not present.
 */
typedef struct chttp_jwt_claims_view {
  const char *issuer;
  const char *subject;
  const char *jwt_id;
  const char *const *audiences;
  size_t audience_count;
  const int64_t *issued_at;
  const int64_t *not_before;
  const int64_t *expires_at;
} chttp_jwt_claims_view;

/**
 * Configuration copied by chttp_jwt_bearer_validator_init(). Issuer and
 * audience checks are optional when their corresponding pointers are NULL.
 */
typedef struct chttp_jwt_bearer_validator_options {
  size_t size;
  const void *key;
  size_t key_size;
  int64_t clock_skew_seconds;
  const char *expected_issuer;
  const char *expected_audience;
  int allow_missing_exp;
} chttp_jwt_bearer_validator_options;

/** Owns the key and expected claim values used by Bearer middleware. */
typedef struct chttp_jwt_bearer_validator {
  void *impl;
} chttp_jwt_bearer_validator;

/** Creates an owned compact HS256 JWT. The caller releases it with chttp_jwt_token_destroy(). */
int chttp_jwt_hs256_token_create(const chttp_jwt_claims *claims, const void *key, size_t key_size,
                                  char **out_token);

/** Releases a token returned by chttp_jwt_hs256_token_create(); NULL is accepted. */
void chttp_jwt_token_destroy(char *token);

/** Formats a borrowed Authorization header in caller-owned storage. */
int chttp_jwt_bearer_header(const char *token, char *buffer, size_t buffer_size,
                             chttp_header *out_header);

/**
 * Pulls at most `capacity` bytes into callback-scoped storage. Return
 * `SALTS_OK` and set `out_size` to zero for EOF, or return a negative error.
 * The callback runs on the CHTTP owner thread and must not perform unbounded blocking or reenter
 * it.
 */
typedef int (*chttp_body_read_fn)(void *user, void *buffer, size_t capacity, size_t *out_size);

/**
 * Consumes one callback-scoped body view. A successful return transfers the
 * whole view; a negative return terminates the request without partial credit.
 * The callback runs on the CHTTP owner thread and must not perform unbounded blocking or reenter
 * it.
 */
typedef int (*chttp_body_write_fn)(void *user, const void *data, size_t size);

typedef struct chttp_body_source {
  chttp_body_read_fn read;
  void *user;
  size_t content_length;
  int content_length_known;
} chttp_body_source;

typedef struct chttp_body_sink {
  chttp_body_write_fn write;
  void *user;
} chttp_body_sink;

typedef void (*chttp_progress_fn)(void *user, size_t transferred, size_t total);

typedef struct chttp_server_param {
  const char *name;
  const char *value;
} chttp_server_param;

/**
 * Borrowed request view. Every pointer becomes invalid when the route handler
 * returns; handlers must copy data that outlives the callback.
 */
typedef struct chttp_server_request_view {
  unsigned int http_major;
  unsigned int http_minor;
  chttp_method method;
  const char *target;
  const char *path;
  const chttp_header *headers;
  size_t header_count;
  const chttp_server_param *params;
  size_t param_count;
  const void *body;
  size_t body_size;
  /** Non-zero when body bytes were delivered to the route sink and are not retained here. */
  int body_streamed;
  int protocol_keep_alive;
  /** Borrowed portable TCP peer endpoint, available for network-backed server requests. */
  const cnet_stream_peer *peer;
  /** Verified TLS peer leaf SHA-256, or NULL for plaintext/no presented client certificate. */
  const char *peer_certificate_sha256;
  chttp_session *session;
  /** NULL unless JWT Bearer middleware authenticated this callback. */
  const chttp_jwt_claims_view *jwt_claims;
} chttp_server_request_view;

/** Handler-scoped response builder. Memory replies are copied; source descriptors are retained. */
typedef struct chttp_server_response {
  void *impl;
} chttp_server_response;

/** Borrowed deferred response input copied before submission returns. */
typedef struct chttp_server_deferred_response {
  size_t size;
  unsigned int status_code;
  const char *content_type;
  const chttp_header *headers;
  size_t header_count;
  const void *body;
  size_t body_size;
} chttp_server_deferred_response;

typedef int (*chttp_server_handler_fn)(void *user, const chttp_server_request_view *request,
                                       chttp_server_response *response);

/**
 * Opens a bounded request-body sink after headers and route parameters are available.
 * Request pointers are borrowed only for this call; the returned sink descriptor is copied.
 */
typedef int (*chttp_server_body_open_fn)(void *user, const chttp_server_request_view *request,
                                         chttp_body_sink *out_sink);

/** Closes a route sink exactly once; status is SALTS_OK only after the complete body arrived. */
typedef void (*chttp_server_body_close_fn)(void *user, chttp_body_sink *sink, int status);

typedef int (*chttp_server_middleware_fn)(void *user, const chttp_server_request_view *request,
                                          chttp_server_response *response, chttp_server_next *next);

/** Copies Bearer validation configuration. Stop all users before destroying the validator. */
int chttp_jwt_bearer_validator_init(
    chttp_jwt_bearer_validator *validator,
    const chttp_jwt_bearer_validator_options *options);

/** Releases copied validation material. The validator must no longer be registered on a server. */
int chttp_jwt_bearer_validator_destroy(chttp_jwt_bearer_validator *validator);

/** Validates one HS256 Authorization: Bearer header and exposes claims to subsequent handlers. */
int chttp_jwt_bearer_middleware(void *user, const chttp_server_request_view *request,
                                 chttp_server_response *response, chttp_server_next *next);

typedef struct chttp_server_middleware {
  chttp_server_middleware_fn handler;
  void *user;
} chttp_server_middleware;

typedef struct chttp_server_route_options {
  chttp_method method;
  const char *path;
  const chttp_server_middleware *middleware;
  size_t middleware_count;
  chttp_server_handler_fn handler;
  void *user;
  chttp_server_body_open_fn body_open;
  chttp_server_body_close_fn body_close;
} chttp_server_route_options;

typedef enum chttp_websocket_state {
  CHTTP_WEBSOCKET_OPEN = 1,
  CHTTP_WEBSOCKET_CLOSING,
  CHTTP_WEBSOCKET_CLOSED,
  CHTTP_WEBSOCKET_FAILED
} chttp_websocket_state;

typedef enum chttp_websocket_message_type {
  CHTTP_WEBSOCKET_MESSAGE_NONE = 0,
  CHTTP_WEBSOCKET_MESSAGE_TEXT,
  CHTTP_WEBSOCKET_MESSAGE_BINARY
} chttp_websocket_message_type;

typedef enum chttp_websocket_event_kind {
  CHTTP_WEBSOCKET_EVENT_MESSAGE = 1,
  CHTTP_WEBSOCKET_EVENT_PING,
  CHTTP_WEBSOCKET_EVENT_PONG,
  CHTTP_WEBSOCKET_EVENT_CLOSE
} chttp_websocket_event_kind;

/** Server event data is borrowed only until the callback returns; client lifetime is documented
 * below. */
typedef struct chttp_websocket_event {
  chttp_websocket_event_kind kind;
  chttp_websocket_message_type message_type;
  const uint8_t *data;
  size_t size;
  uint16_t close_code;
} chttp_websocket_event;

/**
 * Runs as the terminal handler after global and route middleware approve an
 * HTTP/1.1 Upgrade or HTTP/2 Extended CONNECT. The request method is GET for
 * H1 and CONNECT for H2. Returning an error rejects with 500. Calling
 * `chttp_server_reply()` rejects with that in-memory reply; streamed and file
 * replies are invalid during admission. Otherwise the framework sends 101 for
 * H1 or 200 for H2. One frame may be admitted here and is retained until the
 * handshake response write completes.
 */
typedef int (*chttp_websocket_open_fn)(void *user, chttp_websocket *websocket,
                                       const chttp_server_request_view *request,
                                       chttp_server_response *response);

/** Runs serially on the server owner thread; event and peer are callback-scoped. */
typedef void (*chttp_websocket_event_fn)(void *user, chttp_websocket *websocket,
                                         const chttp_websocket_event *event);

typedef struct chttp_server_websocket_options {
  size_t size;
  const char *path;
  const chttp_server_middleware *middleware;
  size_t middleware_count;
  /** Zero selects the largest payload that fits one bounded CNet send, capped at 64 KiB. */
  size_t max_frame_bytes;
  /** Zero selects max_frame_bytes. Must be at least max_frame_bytes. */
  size_t max_message_bytes;
  /** Zero selects max_frame_bytes plus the maximum RFC 6455 frame header. */
  size_t max_buffered_input_bytes;
  chttp_websocket_open_fn on_open;
  chttp_websocket_event_fn on_event;
  void *user;
} chttp_server_websocket_options;

typedef struct chttp_websocket_client_config {
  size_t size;
  cnet_client_config network;
  size_t max_frame_bytes;
  size_t max_message_bytes;
  size_t max_buffered_input_bytes;
  size_t max_handshake_header_bytes;
  /** Completed event queue hard bound; zero selects network.event_capacity. */
  size_t event_capacity;
  /** HTTP/2 parser input hard bound; zero selects 128 KiB. */
  size_t h2_input_buffer_bytes;
  /** HTTP/2 HPACK dynamic table hard bound; zero selects 4096 bytes. */
  size_t h2_hpack_dynamic_table_bytes;
  /** Maximum SETTINGS entries accepted per frame; zero selects 16. */
  size_t h2_max_settings_count;
  /** Optional CNet socket policy; zeroed size preserves platform defaults. */
  cnet_stream_socket_options socket_options;
} chttp_websocket_client_config;

typedef struct chttp_websocket_connect_options {
  size_t size;
  /** Complete ws:// or wss:// URI, including target path and optional query. */
  const char *uri;
  /** Extra copied HTTP headers; handshake-owned fields cannot be overridden. */
  const chttp_header *headers;
  size_t header_count;
  /** Required for custom WSS trust; invalid for ws:// and ALPN must match `protocol`. */
  const chttp_tls_profile *tls;
  uint32_t timeout_ms;
  /** HTTP/1.1 Upgrade by default; HTTP/2 selects RFC 8441 Extended CONNECT. */
  chttp_protocol protocol;
  /** Optional single protocol token; the server must select it exactly. */
  const char *subprotocol;
} chttp_websocket_connect_options;

typedef struct chttp_websocket_pool_config {
  size_t size;
  /** Shared transport limits plus the per-session WebSocket and event bounds. */
  chttp_websocket_client_config client;
  /** Concurrent RFC 8441 streams on the one physical H2 connection. */
  size_t session_capacity;
} chttp_websocket_pool_config;

/**
 * Every capacity is a hard bound. `network.connection_capacity` bounds active
 * accepted connections. When HTTP/2 is enabled, its stream, parser, output,
 * HPACK and SETTINGS limits are per connection. Routes are origin-form paths, may contain named
 * `:segment` parameters, and exclude the query string. The server invokes
 * handlers serially on its owner thread, so a handler must not block or call
 * stop/destroy. Session capacity zero disables Sessions; otherwise the Cookie
 * contains only a CSPRNG id and values stay in the bounded in-memory store.
 */
typedef struct chttp_server_config {
  const char *host;
  uint16_t port;
  size_t backlog;
  cnet_client_config network;
  size_t route_capacity;
  size_t middleware_capacity;
  size_t max_route_middleware_count;
  size_t max_route_param_count;
  size_t max_route_param_bytes;
  size_t max_target_bytes;
  size_t max_header_count;
  size_t max_header_bytes;
  size_t max_request_body_bytes;
  size_t max_response_header_count;
  size_t max_response_header_bytes;
  size_t max_response_body_bytes;
  size_t session_capacity;
  size_t session_entry_capacity;
  size_t max_session_key_bytes;
  size_t max_session_value_bytes;
  uint32_t session_idle_timeout_ms;
  const char *session_cookie_name;
  int session_cookie_secure;
  uint32_t poll_slice_ms;
  /** Optional HTTPS/mTLS policy consumed during server initialization. */
  const cnet_tls_server_config *tls;
  /** Zero preserves the HTTP/1.1-only listener. One also accepts HTTP/2. */
  int enable_http2;
  /** Per-connection HTTP/2 stream hard bound and advertised concurrency limit. */
  size_t h2_stream_capacity;
  /** HTTP/2 parser input hard bound; must be at least 16,393 bytes. */
  size_t h2_input_buffer_bytes;
  /** H2 output bound; >=16,468, <=max_send_bytes, and large enough for response HPACK bounds. */
  size_t h2_output_buffer_bytes;
  /** HTTP/2 HPACK dynamic-table hard bound. */
  size_t h2_hpack_dynamic_table_bytes;
  /** Maximum SETTINGS entries accepted in one HTTP/2 frame. */
  size_t h2_max_settings_count;
  /** Per-response source chunk bound; zero selects up to 64 KiB within transport limits. */
  size_t stream_chunk_bytes;
  /** In-memory reply bound; zero derives the largest value fitting one transport send. */
  size_t max_buffered_response_body_bytes;
  /**
   * Aggregate bytes available to live request, response, transport and
   * WebSocket payload buffers. Zero preserves the legacy logical maximum but
   * allocates it only on demand. Exhaustion returns `SALTS_ENOBUFS`.
   */
  size_t buffer_capacity_bytes;
} chttp_server_config;

/** Socket policy copied into a stopped HTTP/WebSocket server before start. */
typedef struct chttp_server_socket_options {
  size_t size;
  cnet_stream_socket_options stream;
  cnet_listener_options listener;
} chttp_server_socket_options;

#define CHTTP_SERVER_SOCKET_OPTIONS_INIT                                                          \
  {sizeof(chttp_server_socket_options), CNET_STREAM_SOCKET_OPTIONS_INIT,                           \
   CNET_LISTENER_OPTIONS_INIT}

/** Thread-safe snapshot of server lifecycle and bounded admission counters. */
typedef struct chttp_server_stats {
  uint16_t port;
  size_t active_connections;
  uint64_t accepted_connections;
  uint64_t rejected_connections;
  uint64_t requests;
  uint64_t responses;
  uint64_t protocol_errors;
  uint64_t handler_errors;
  size_t buffer_bytes;
  size_t peak_buffer_bytes;
  uint64_t rejected_buffer_allocations;
  int running;
  int stopping;
  int terminal_status;
} chttp_server_stats;

/** Borrowed response view valid only for the duration of an advanced completion callback. */
typedef struct chttp_response_view {
  unsigned int http_major;
  unsigned int http_minor;
  unsigned int status_code;
  const char *reason;
  const chttp_header *headers;
  size_t header_count;
  const void *body;
  size_t body_size;
  int protocol_keep_alive;
} chttp_response_view;

/** Owning response returned by a blocking requests-style method. */
typedef struct chttp_response {
  unsigned int http_major;
  unsigned int http_minor;
  unsigned int status_code;
  char *reason;
  chttp_header *headers;
  size_t header_count;
  void *body;
  size_t body_size;
  int protocol_keep_alive;
} chttp_response;

/** `stage` is a stable library-owned string. */
typedef struct chttp_error {
  int status;
  int native_status;
  const char *stage;
} chttp_error;

typedef void (*chttp_complete_fn)(void *user, chttp_request request,
                                  const chttp_response_view *response, const chttp_error *error);

/**
 * One admitted request owns one bounded response state. HTTP/1.1 exclusively
 * leases a pooled CNet connection; HTTP/2 leases one stream from a multiplexed
 * session. `connection_uri` accepts `tcp://` and `tls://` for both protocols,
 * plus `pipe://` for HTTP/1.1. `authority` becomes Host or `:authority`, while
 * `target` is an origin-form target beginning with `/` (or `*` for OPTIONS).
 * Header/body/options are copied before submit succeeds. `user` remains
 * borrowed until the terminal callback returns.
 */
typedef struct chttp_request_options {
  const char *connection_uri;
  const char *authority;
  const char *target;
  chttp_method method;
  const chttp_header *headers;
  size_t header_count;
  const void *body;
  size_t body_size;
  /** Borrowed through the terminal callback; mutually exclusive with `body/body_size`. */
  const chttp_body_source *body_source;
  /** Borrowed through the terminal callback; streamed responses expose `body == NULL`. */
  const chttp_body_sink *body_sink;
  chttp_complete_fn on_complete;
  void *user;
  /** Optional reusable TLS profile; valid only with a `tls://` connection URI. */
  const chttp_tls_profile *tls;
  /** Explicit wire protocol. HTTP/2 never falls back to HTTP/1.1. */
  chttp_protocol protocol;
} chttp_request_options;

/**
 * Inputs for one blocking requests-style call. All inputs are copied before asynchronous
 * progress begins. `timeout_ms` bounds the wait for an HTTP result; zero
 * disables it and leaves termination to the configured transport deadlines.
 * After that deadline, terminal cancellation/drain may continue before the
 * function returns so the client is safe to reuse or destroy.
 */
typedef struct chttp_options {
  const char *connection_uri;
  const char *authority;
  const char *target;
  const chttp_header *headers;
  size_t header_count;
  const void *body;
  size_t body_size;
  /** Borrowed for the duration of this blocking call; mutually exclusive with `body/body_size`. */
  const chttp_body_source *body_source;
  /** Borrowed for the duration of this blocking call; streamed responses expose `body == NULL`. */
  const chttp_body_sink *body_sink;
  uint32_t timeout_ms;
  /** Optional reusable TLS profile; valid only with a `tls://` connection URI. */
  const chttp_tls_profile *tls;
  /** Explicit wire protocol. HTTP/2 never falls back to HTTP/1.1. */
  chttp_protocol protocol;
} chttp_options;

/**
 * All capacities are hard bounds. CHTTP uses strict llhttp parsing, buffers one
 * complete response, and admits at most one HTTP/1.1 request at a time per
 * connection. `request_capacity` bounds request slots and HTTP/2 streams;
 * `network.connection_capacity` independently bounds live physical H1
 * connections and H2 sessions, so it may be smaller than `request_capacity`.
 * `network.read_timeout_ms` applies while an idle connection observes its
 * peer. Serialized requests are additionally bounded by
 * `network.max_send_bytes`.
 */
typedef struct chttp_client_config {
  cnet_client_config network;
  size_t request_capacity;
  size_t max_start_line_bytes;
  size_t max_header_count;
  size_t max_header_bytes;
  size_t max_request_body_bytes;
  size_t max_response_body_bytes;
  size_t max_informational_responses;
  /** Per-request source buffer bound; zero selects up to 64 KiB within transport limits. */
  size_t stream_chunk_bytes;
  /** HTTP/2 parser input hard bound; zero selects 128 KiB or the larger CNet receive buffer. */
  size_t h2_input_buffer_bytes;
  /** HTTP/2 HPACK dynamic-table hard bound; zero selects 4 KiB. */
  size_t h2_hpack_dynamic_table_bytes;
  /** Maximum SETTINGS entries accepted in one HTTP/2 frame; zero selects 32. */
  size_t h2_max_settings_count;
} chttp_client_config;

int chttp_tls_profile_init(chttp_tls_profile *profile, const cnet_tls_client_config *config);
int chttp_tls_profile_destroy(chttp_tls_profile *profile);
int chttp_async_client_init(chttp_async_client *client, const chttp_client_config *config);
int chttp_async_client_submit(chttp_async_client *client, const chttp_request_options *options,
                              chttp_request *out_request);
int chttp_async_request_cancel(chttp_async_client *client, chttp_request request);
int chttp_async_client_poll(chttp_async_client *client, uint32_t timeout_ms,
                            size_t *out_completions);
int chttp_async_client_stop(chttp_async_client *client, uint32_t timeout_ms);
int chttp_async_client_destroy(chttp_async_client *client);
const char *chttp_response_view_header(const chttp_response_view *response, const char *name);
int chttp_client_init(chttp_client *client, const chttp_client_config *config);
int chttp_get(chttp_client *client, const chttp_options *options, chttp_response *out_response,
              chttp_error *out_error);
int chttp_head(chttp_client *client, const chttp_options *options, chttp_response *out_response,
               chttp_error *out_error);
int chttp_post(chttp_client *client, const chttp_options *options, chttp_response *out_response,
               chttp_error *out_error);
int chttp_put(chttp_client *client, const chttp_options *options, chttp_response *out_response,
              chttp_error *out_error);
int chttp_delete(chttp_client *client, const chttp_options *options, chttp_response *out_response,
                 chttp_error *out_error);
int chttp_patch(chttp_client *client, const chttp_options *options, chttp_response *out_response,
                chttp_error *out_error);
int chttp_post_file(chttp_client *client, const chttp_options *options, const char *path,
                    chttp_progress_fn progress, void *progress_user, chttp_response *out_response,
                    chttp_error *out_error);
int chttp_put_file(chttp_client *client, const chttp_options *options, const char *path,
                   chttp_progress_fn progress, void *progress_user, chttp_response *out_response,
                   chttp_error *out_error);
int chttp_download_file(chttp_client *client, const chttp_options *options, const char *output_path,
                        chttp_progress_fn progress, void *progress_user,
                        chttp_response *out_response, chttp_error *out_error);
const char *chttp_response_header(const chttp_response *response, const char *name);
void chttp_response_destroy(chttp_response *response);
int chttp_client_destroy(chttp_client *client, uint32_t timeout_ms);
int chttp_server_init(chttp_server *server, const chttp_server_config *config);
int chttp_server_set_socket_options(chttp_server *server,
                                    const chttp_server_socket_options *options);
int chttp_server_route(chttp_server *server, chttp_method method, const char *path,
                       chttp_server_handler_fn handler, void *user);
int chttp_server_route_with(chttp_server *server, const chttp_server_route_options *options);
int chttp_server_route_with_jwt_bearer(chttp_server *server,
                                       const chttp_server_route_options *options,
                                       chttp_jwt_bearer_validator *validator);
int chttp_server_get(chttp_server *server, const char *path, chttp_server_handler_fn handler,
                     void *user);
int chttp_server_head(chttp_server *server, const char *path, chttp_server_handler_fn handler,
                      void *user);
int chttp_server_post(chttp_server *server, const char *path, chttp_server_handler_fn handler,
                      void *user);
int chttp_server_put(chttp_server *server, const char *path, chttp_server_handler_fn handler,
                     void *user);
int chttp_server_delete(chttp_server *server, const char *path, chttp_server_handler_fn handler,
                        void *user);
int chttp_server_patch(chttp_server *server, const char *path, chttp_server_handler_fn handler,
                       void *user);
int chttp_server_options(chttp_server *server, const char *path, chttp_server_handler_fn handler,
                         void *user);
int chttp_server_websocket_with(chttp_server *server,
                                const chttp_server_websocket_options *options);
int chttp_server_websocket(chttp_server *server, const char *path, chttp_websocket_open_fn on_open,
                           chttp_websocket_event_fn on_event, void *user);
int chttp_websocket_state_get(const chttp_websocket *websocket, chttp_websocket_state *out_state);
int chttp_websocket_send_text(chttp_websocket *websocket, const void *data, size_t size);
int chttp_websocket_send_binary(chttp_websocket *websocket, const void *data, size_t size);
int chttp_websocket_send_ping(chttp_websocket *websocket, const void *data, size_t size);
int chttp_websocket_send_pong(chttp_websocket *websocket, const void *data, size_t size);
int chttp_websocket_close(chttp_websocket *websocket, uint16_t code, const void *reason,
                          size_t reason_size);
int chttp_server_websocket_session_capture(const chttp_websocket *websocket,
                                            chttp_server_websocket_session *out_session);
int chttp_server_websocket_send_text(const chttp_server_websocket_session *session,
                                     const void *data, size_t size);
int chttp_server_websocket_send_binary(const chttp_server_websocket_session *session,
                                       const void *data, size_t size);
int chttp_server_websocket_send_ping(const chttp_server_websocket_session *session,
                                     const void *data, size_t size);
int chttp_server_websocket_send_pong(const chttp_server_websocket_session *session,
                                     const void *data, size_t size);
int chttp_server_websocket_close(const chttp_server_websocket_session *session, uint16_t code,
                                 const void *reason, size_t reason_size);
int chttp_websocket_client_init(chttp_websocket_client *client,
                                const chttp_websocket_client_config *config);
int chttp_websocket_client_connect(chttp_websocket_client *client,
                                   const chttp_websocket_connect_options *options,
                                   unsigned int *out_http_status);
int chttp_websocket_client_send_text(chttp_websocket_client *client, const void *data, size_t size,
                                     uint32_t timeout_ms);
int chttp_websocket_client_send_binary(chttp_websocket_client *client, const void *data,
                                       size_t size, uint32_t timeout_ms);
int chttp_websocket_client_send_ping(chttp_websocket_client *client, const void *data, size_t size,
                                     uint32_t timeout_ms);
int chttp_websocket_client_send_pong(chttp_websocket_client *client, const void *data, size_t size,
                                     uint32_t timeout_ms);
int chttp_websocket_client_receive(chttp_websocket_client *client, uint32_t timeout_ms,
                                   chttp_websocket_event *out_event);
int chttp_websocket_client_close(chttp_websocket_client *client, uint16_t code, const void *reason,
                                 size_t reason_size, uint32_t timeout_ms);
int chttp_websocket_client_destroy(chttp_websocket_client *client, uint32_t timeout_ms);
int chttp_websocket_pool_init(chttp_websocket_pool *pool,
                              const chttp_websocket_pool_config *config);
int chttp_websocket_pool_open(chttp_websocket_pool *pool,
                              const chttp_websocket_connect_options *options,
                              chttp_websocket_session *out_session, unsigned int *out_http_status);
int chttp_websocket_pool_send_text(chttp_websocket_pool *pool, chttp_websocket_session session,
                                   const void *data, size_t size, uint32_t timeout_ms);
int chttp_websocket_pool_send_binary(chttp_websocket_pool *pool, chttp_websocket_session session,
                                     const void *data, size_t size, uint32_t timeout_ms);
int chttp_websocket_pool_send_ping(chttp_websocket_pool *pool, chttp_websocket_session session,
                                   const void *data, size_t size, uint32_t timeout_ms);
int chttp_websocket_pool_send_pong(chttp_websocket_pool *pool, chttp_websocket_session session,
                                   const void *data, size_t size, uint32_t timeout_ms);
int chttp_websocket_pool_receive(chttp_websocket_pool *pool, chttp_websocket_session session,
                                 uint32_t timeout_ms, chttp_websocket_event *out_event);
int chttp_websocket_pool_close(chttp_websocket_pool *pool, chttp_websocket_session session,
                               uint16_t code, const void *reason, size_t reason_size,
                               uint32_t timeout_ms);
int chttp_websocket_pool_destroy(chttp_websocket_pool *pool, uint32_t timeout_ms);
int chttp_server_use(chttp_server *server, chttp_server_middleware_fn middleware, void *user);
int chttp_server_next_call(chttp_server_next *next);
int chttp_server_start(chttp_server *server);
int chttp_server_port(const chttp_server *server, uint16_t *out_port);
int chttp_server_stop(chttp_server *server, uint32_t timeout_ms);
int chttp_server_destroy(chttp_server *server);
const char *chttp_server_request_header(const chttp_server_request_view *request, const char *name);
const char *chttp_server_request_param(const chttp_server_request_view *request, const char *name);
int chttp_server_response_set_header(chttp_server_response *response, const char *name,
                                     const char *value);
int chttp_server_response_select_websocket_subprotocol(
    chttp_server_response *response, const chttp_server_request_view *request,
    const char *subprotocol);
int chttp_server_response_defer(chttp_server_response *response,
                               chttp_server_deferred *out_deferred);
int chttp_server_deferred_reply(chttp_server_deferred *deferred,
                                const chttp_server_deferred_response *response);
int chttp_server_reply(chttp_server_response *response, unsigned int status_code,
                       const char *content_type, const void *body, size_t body_size);
int chttp_server_response_source(chttp_server_response *response, unsigned int status_code,
                                 const char *content_type, const chttp_body_source *source);
int chttp_server_response_file(chttp_server_response *response, unsigned int status_code,
                               const char *content_type, const char *path);
const char *chttp_session_get(const chttp_session *session, const char *key);
int chttp_session_set(chttp_session *session, const char *key, const char *value);
int chttp_session_remove(chttp_session *session, const char *key);
int chttp_session_clear(chttp_session *session);
int chttp_session_invalidate(chttp_session *session);
int chttp_server_get_stats(const chttp_server *server, chttp_server_stats *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* CHTTP_CHTTP_H */
