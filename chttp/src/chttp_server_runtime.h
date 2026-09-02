#ifndef CHTTP_SERVER_RUNTIME_H
#define CHTTP_SERVER_RUNTIME_H

#include "chttp_server_internal.h"

#include <turbo/thread.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct chttp_server_impl chttp_server_impl;
typedef struct chttp_server_connection chttp_server_connection;

typedef struct chttp_server_route_record {
  chttp_method method;
  char *path;
  chttp_server_middleware *middleware;
  size_t middleware_count;
  size_t param_count;
  chttp_server_handler_fn handler;
  void *user;
  bool dynamic;
} chttp_server_route_record;

typedef struct chttp_server_response_builder {
  chttp_header *headers;
  char *header_storage;
  unsigned char *body;
  size_t header_capacity;
  size_t header_storage_capacity;
  size_t body_capacity;
  size_t header_count;
  size_t header_storage_used;
  size_t header_wire_bytes;
  size_t body_size;
  unsigned int status_code;
  bool replied;
} chttp_server_response_builder;

typedef struct chttp_session_entry {
  char *key;
  char *value;
  bool used;
} chttp_session_entry;

typedef struct chttp_session_record {
  char id[33];
  chttp_session_entry *entries;
  uint64_t expires_at_ms;
  bool used;
} chttp_session_record;

typedef struct chttp_session_context {
  chttp_server_impl *server;
  chttp_session_record *record;
  bool presented;
  bool created;
  bool invalidated;
} chttp_session_context;

typedef struct chttp_server_chain {
  chttp_server_impl *server;
  const chttp_server_request_view *request;
  chttp_server_response *response;
  chttp_server_route_record *route;
  unsigned int fallback_status;
  unsigned int allowed_methods;
} chttp_server_chain;

typedef struct chttp_server_next_impl {
  chttp_server_chain *chain;
  size_t index;
  bool called;
} chttp_server_next_impl;

typedef enum chttp_server_pending_action {
  CHTTP_SERVER_PENDING_NONE = 0,
  CHTTP_SERVER_PENDING_RECEIVE,
  CHTTP_SERVER_PENDING_SEND,
  CHTTP_SERVER_PENDING_CLOSE
} chttp_server_pending_action;

struct chttp_server_connection {
  chttp_server_impl *server;
  cnet_connection handle;
  chttp_server_parser parser;
  chttp_server_response_builder response_builder;
  chttp_server_response response;
  chttp_server_param *params;
  char *param_storage;
  size_t param_storage_capacity;
  size_t param_storage_used;
  size_t param_count;
  unsigned char *outbound;
  size_t outbound_capacity;
  size_t outbound_size;
  chttp_session session;
  chttp_session_context session_context;
  bool active;
  bool connected;
  bool writing;
  bool close_after_write;
  chttp_server_pending_action pending_action;
};

struct chttp_server_impl {
  chttp_server_config config;
  char *host;
  char *session_cookie_name;
  chttp_server_route_record *routes;
  char *route_paths;
  chttp_server_middleware *route_middleware;
  chttp_server_middleware *middleware;
  size_t route_count;
  size_t middleware_count;
  size_t max_response_wire_bytes;
  size_t pending_retry_cursor;
  chttp_server_connection *connections;
  chttp_session_record *sessions;
  chttp_session_entry *session_entries;
  char *session_keys;
  char *session_values;
  cnet_client network;
  cnet_listener listener;
  cnet_tls_server tls_server;
  turbo_mutex_t mutex;
  turbo_cond_t changed;
  turbo_thread_t thread;
  chttp_server_stats stats;
  bool sync_initialized;
  bool network_initialized;
  bool listener_initialized;
  bool tls_initialized;
  bool thread_started;
  bool start_called;
  bool stop_requested;
  bool worker_done;
};

int chttp_server_response_builder_init(chttp_server_response_builder *builder,
                                       const chttp_server_config *config);
void chttp_server_response_builder_reset(chttp_server_response_builder *builder);
void chttp_server_response_builder_destroy(chttp_server_response_builder *builder);
int chttp_server_response_serialize(const chttp_server_response_builder *builder,
                                    const chttp_server_request_view *request, unsigned char *output,
                                    size_t output_capacity, size_t *inout_size);
int chttp_server_error_serialize(unsigned int status_code, unsigned char *output,
                                 size_t output_capacity, size_t *inout_size);

int chttp_server_route_register(chttp_server_impl *server,
                                const chttp_server_route_options *options);
chttp_server_route_record *chttp_server_route_find(chttp_server_connection *connection,
                                                   chttp_method method, const char *path,
                                                   unsigned int *out_allowed_methods,
                                                   int *out_status);
int chttp_server_chain_run(chttp_server_chain *chain);

int chttp_session_store_init(chttp_server_impl *server);
void chttp_session_store_destroy(chttp_server_impl *server);
void chttp_session_request_begin(chttp_server_connection *connection,
                                 const chttp_server_request_view *request);
int chttp_session_request_finish(chttp_server_connection *connection);
void chttp_session_request_abort(chttp_server_connection *connection);

void chttp_server_stats_connection_open(chttp_server_impl *server);
void chttp_server_stats_connection_close(chttp_server_impl *server);
void chttp_server_stats_request(chttp_server_impl *server);
void chttp_server_stats_response(chttp_server_impl *server);
void chttp_server_stats_protocol_error(chttp_server_impl *server);
void chttp_server_stats_handler_error(chttp_server_impl *server);

#endif /* CHTTP_SERVER_RUNTIME_H */
