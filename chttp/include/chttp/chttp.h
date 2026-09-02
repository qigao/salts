#ifndef CHTTP_CHTTP_H
#define CHTTP_CHTTP_H

#include <cnet/cnet.h>
#include <turbo/error_codes.h>

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

/** Background HTTP/1.1 server owner; ordinary callers never drive a poller. */
typedef struct chttp_server {
  void *impl;
} chttp_server;

/** Handler-scoped server-side session. */
typedef struct chttp_session {
  void *impl;
} chttp_session;

/** Handler-scoped continuation for one middleware invocation. */
typedef struct chttp_server_next {
  void *impl;
} chttp_server_next;

/** Generation-checked request handle; never a pointer or CNet handle. */
typedef struct chttp_request {
  uint32_t slot;
  uint32_t generation;
} chttp_request;

typedef enum chttp_method {
  CHTTP_METHOD_GET = 1,
  CHTTP_METHOD_HEAD,
  CHTTP_METHOD_POST,
  CHTTP_METHOD_PUT,
  CHTTP_METHOD_DELETE,
  CHTTP_METHOD_PATCH,
  CHTTP_METHOD_OPTIONS
} chttp_method;

/** Input strings are borrowed until submit returns; response strings are callback-scoped. */
typedef struct chttp_header {
  const char *name;
  const char *value;
} chttp_header;

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
  int protocol_keep_alive;
  chttp_session *session;
} chttp_server_request_view;

/** Handler-scoped response builder. Response data is copied before the handler returns. */
typedef struct chttp_server_response {
  void *impl;
} chttp_server_response;

typedef int (*chttp_server_handler_fn)(void *user, const chttp_server_request_view *request,
                                       chttp_server_response *response);

typedef int (*chttp_server_middleware_fn)(void *user, const chttp_server_request_view *request,
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
} chttp_server_route_options;

/**
 * Every capacity is a hard bound. `network.connection_capacity` bounds active
 * accepted connections. Routes are origin-form paths, may contain named
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
} chttp_server_config;

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
 * One admitted request owns one bounded parser state and either opens or
 * exclusively leases one CNet connection from this client's bounded pool.
 * `connection_uri` accepts `tcp://`, `tls://`, and `pipe://`. `authority`
 * becomes the Host header, while `target` is an origin-form target beginning
 * with `/` (or `*` for OPTIONS). Header/body/options are copied before submit
 * succeeds. `user` remains borrowed until the terminal callback returns.
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
  chttp_complete_fn on_complete;
  void *user;
  /** Optional reusable TLS profile; valid only with a `tls://` connection URI. */
  const chttp_tls_profile *tls;
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
  uint32_t timeout_ms;
  /** Optional reusable TLS profile; valid only with a `tls://` connection URI. */
  const chttp_tls_profile *tls;
} chttp_options;

/**
 * All capacities are hard bounds. CHTTP uses strict llhttp parsing, buffers one
 * complete response, and admits at most one request at a time per connection.
 * `request_capacity` also bounds retained idle connections and cannot exceed
 * `network.connection_capacity`. `network.read_timeout_ms` applies while an
 * idle connection observes its peer. Serialized requests are additionally
 * bounded by `network.max_send_bytes`.
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
} chttp_client_config;

/**
 * Builds a reusable verified TLS profile. Configuration is consumed before
 * return. ALPN must be absent or contain only `http/1.1`. The same public
 * wrapper must not be initialized/destroyed concurrently with submit.
 *
 * @param profile Zero-initialized reusable output profile.
 * @param config Explicit CNet TLS client policy.
 * @return CNet TLS setup errors, or `TURBO_ENOTSUP` for non-HTTP/1.1 ALPN.
 */
int chttp_tls_profile_init(chttp_tls_profile *profile, const cnet_tls_client_config *config);

/**
 * Releases the public reference; admitted requests and idle slots remain
 * valid. Repeated destroy succeeds; a NULL wrapper returns `TURBO_EINVAL`.
 */
int chttp_tls_profile_destroy(chttp_tls_profile *profile);

/**
 * Initializes one advanced caller-driven CHTTP/CNet owner. No partial client
 * is published.
 * @param client Zero-initialized output owner.
 * @param config Borrowed configuration copied during initialization.
 * @return `TURBO_OK`, `TURBO_EINVAL`, `TURBO_ENOMEM`, or a CNet/backend init error.
 */
int chttp_async_client_init(chttp_async_client *client, const chttp_client_config *config);

/**
 * Serializes and copies a request, then reuses an idle exact
 * `connection_uri + authority` connection or asynchronously connects through CNet.
 * Success guarantees exactly one later completion callback. No callback is
 * delivered for immediate admission failure. Submission from a completion
 * callback returns `TURBO_EBUSY`; defer it until the callback unwinds.
 * A full pool may begin closing one non-matching idle connection and returns
 * `TURBO_ENOBUFS`; poll before retrying admission.
 * @return `TURBO_OK`, an input/size/transport error, `TURBO_ENOBUFS`,
 * `TURBO_EBUSY`, `TURBO_ESHUTDOWN`, or a CNet admission error.
 */
int chttp_async_client_submit(chttp_async_client *client, const chttp_request_options *options,
                              chttp_request *out_request);

/**
 * Requests cancellation; completion is reported later with `TURBO_ECANCELED`.
 * A completed request whose connection is idle in the pool is already stale
 * and returns `TURBO_ENOENT`.
 * @return `TURBO_OK`, `TURBO_ENOENT`, `TURBO_EALREADY`, or a CNet close error.
 */
int chttp_async_request_cancel(chttp_async_client *client, chttp_request request);

/**
 * Advanced integration API. Advances CNet and HTTP parsing on the calling
 * thread. Ordinary callers should use chttp_get/post/put and never call this
 * function. `out_completions` counts user completion callbacks, not transport
 * callbacks.
 * @return `TURBO_OK`, `TURBO_EINVAL`, `TURBO_EBUSY`, `TURBO_ESHUTDOWN`,
 * or the first CNet/progress error.
 */
int chttp_async_client_poll(chttp_async_client *client, uint32_t timeout_ms,
                            size_t *out_completions);

/**
 * Stops admission and drains all accepted requests plus busy and idle CNet connections.
 * Retry after `TURBO_ETIMEDOUT`; calling from poll/callback returns `TURBO_EBUSY`.
 */
int chttp_async_client_stop(chttp_async_client *client, uint32_t timeout_ms);

/**
 * Requires a completed stop; a null implementation is already destroyed.
 * @return `TURBO_OK`, `TURBO_EINVAL`, `TURBO_EBUSY`, or a CNet destroy error.
 */
int chttp_async_client_destroy(chttp_async_client *client);

/** Returns the first case-insensitive matching response header, or NULL. */
const char *chttp_response_view_header(const chttp_response_view *response, const char *name);

/**
 * Initializes an ordinary sequential requests-style client. Calls drive CNet
 * internally, including bounded idle-origin eviction; callers do not provide
 * a poller, executor, or worker thread.
 */
int chttp_client_init(chttp_client *client, const chttp_client_config *config);

/** Blocking requests-style methods returning an owning response. */
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

/** Returns the first case-insensitive matching owning response header. */
const char *chttp_response_header(const chttp_response *response, const char *name);

/** Releases every allocation owned by a blocking response; zero is accepted. */
void chttp_response_destroy(chttp_response *response);

/**
 * Stops and drains the internal client. `TURBO_ETIMEDOUT` is retryable and
 * preserves the client; successful destroy clears `client->impl`.
 */
int chttp_client_destroy(chttp_client *client, uint32_t timeout_ms);

/**
 * Initializes a stopped server and copies configuration and bounded storage.
 * @return `TURBO_OK`, an invalid/range/aggregate-size error, or `TURBO_ENOMEM`.
 */
int chttp_server_init(chttp_server *server, const chttp_server_config *config);

/**
 * Adds one method/path-pattern route before start. Complete `:name` segments
 * bind raw, non-percent-decoded params. The user pointer is borrowed through
 * stop. Returns `TURBO_ENOBUFS` at route/param capacity, `TURBO_EALREADY` for a
 * duplicate method/pattern, or `TURBO_EBUSY` after start.
 */
int chttp_server_route(chttp_server *server, chttp_method method, const char *path,
                       chttp_server_handler_fn handler, void *user);
int chttp_server_route_with(chttp_server *server, const chttp_server_route_options *options);
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

/**
 * Appends one global middleware before start. Bindings are copied in
 * registration order. Returns `TURBO_ENOBUFS` at middleware capacity.
 */
int chttp_server_use(chttp_server *server, chttp_server_middleware_fn middleware, void *user);

/** Runs the next middleware or terminal dispatch; a second call returns `TURBO_EALREADY`. */
int chttp_server_next_call(chttp_server_next *next);

/**
 * Starts the listener and background CNet owner thread. Port zero selects an
 * ephemeral port. Bind/backend/thread failures are returned before success.
 */
int chttp_server_start(chttp_server *server);

/** Returns the bound port after a successful start. */
int chttp_server_port(const chttp_server *server, uint16_t *out_port);

/** Stops admission and joins the owner thread. Timeout zero waits without a deadline. */
int chttp_server_stop(chttp_server *server, uint32_t timeout_ms);

/** Releases a stopped server; a zero server is already destroyed. */
int chttp_server_destroy(chttp_server *server);

/** Returns the first case-insensitive request header, or NULL. */
const char *chttp_server_request_header(const chttp_server_request_view *request, const char *name);

/** Returns one `:name` route parameter, or NULL. */
const char *chttp_server_request_param(const chttp_server_request_view *request, const char *name);

/**
 * Adds or replaces one copied response header within configured count/byte
 * bounds. Framing headers are framework-owned and return `TURBO_EPERM`.
 */
int chttp_server_response_set_header(chttp_server_response *response, const char *name,
                                     const char *value);

/**
 * Completes the response with a copied content type and body. A second reply
 * returns `TURBO_EALREADY`; an oversized body returns `TURBO_EMSGSIZE`.
 */
int chttp_server_reply(chttp_server_response *response, unsigned int status_code,
                       const char *content_type, const void *body, size_t body_size);

/**
 * Session values are NUL-terminated strings borrowed through the handler and
 * copied into the bounded store by set. A first set lazily allocates a Session;
 * a full live-session or entry capacity returns `TURBO_ENOBUFS`.
 */
const char *chttp_session_get(const chttp_session *session, const char *key);
int chttp_session_set(chttp_session *session, const char *key, const char *value);
int chttp_session_remove(chttp_session *session, const char *key);
int chttp_session_clear(chttp_session *session);
int chttp_session_invalidate(chttp_session *session);

/** Obtains a thread-safe stats snapshot without advancing server state. */
int chttp_server_get_stats(const chttp_server *server, chttp_server_stats *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* CHTTP_CHTTP_H */
