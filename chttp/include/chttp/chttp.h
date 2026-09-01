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
 * `connection_uri` currently accepts only `tcp://` and `pipe://`. `authority`
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

#ifdef __cplusplus
}
#endif

#endif /* CHTTP_CHTTP_H */
