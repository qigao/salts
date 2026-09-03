#ifndef CHTTP_SERVER_RUNTIME_H
#define CHTTP_SERVER_RUNTIME_H

#include "chttp_file_transfer.h"
#include "chttp_server_internal.h"

#include <cnet/websocket.h>
#include <turbo/thread.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct chttp_server_impl chttp_server_impl;
typedef struct chttp_server_connection chttp_server_connection;
typedef struct chttp_server_request_state chttp_server_request_state;
typedef struct chttp_h2_server_connection chttp_h2_server_connection;

extern TURBO_THREAD_LOCAL chttp_server_impl *chttp_active_callback_server;

typedef enum chttp_server_wire_protocol {
  CHTTP_SERVER_WIRE_UNKNOWN = 0,
  CHTTP_SERVER_WIRE_HTTP_1_1,
  CHTTP_SERVER_WIRE_HTTP_2
} chttp_server_wire_protocol;

typedef struct chttp_server_route_record {
  chttp_method method;
  char *path;
  chttp_server_middleware *middleware;
  size_t middleware_count;
  size_t param_count;
  chttp_server_handler_fn handler;
  void *user;
  chttp_server_body_open_fn body_open;
  chttp_server_body_close_fn body_close;
  chttp_websocket_open_fn websocket_open;
  chttp_websocket_event_fn websocket_event;
  size_t websocket_max_frame_bytes;
  size_t websocket_max_message_bytes;
  size_t websocket_max_buffered_input_bytes;
  void *websocket_user;
  bool websocket;
  bool dynamic;
} chttp_server_route_record;

#include "chttp_websocket_server_internal.h"

typedef struct chttp_server_response_builder {
  chttp_server_impl *server;
  chttp_header *headers;
  char *header_storage;
  unsigned char *body;
  size_t header_capacity;
  size_t header_storage_capacity;
  size_t body_capacity;
  size_t source_capacity;
  size_t header_count;
  size_t header_storage_used;
  size_t header_wire_bytes;
  size_t body_size;
  size_t source_transferred;
  unsigned int status_code;
  chttp_body_source body_source;
  void (*source_cleanup)(void *user, int status);
  void *source_cleanup_user;
  chttp_file_transfer *file_transfer;
  bool replied;
  bool source_enabled;
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

struct chttp_server_request_state {
  chttp_server_impl *server;
  chttp_server_response_builder response_builder;
  chttp_server_response response;
  chttp_server_param *params;
  char *param_storage;
  size_t param_storage_capacity;
  size_t param_storage_used;
  size_t param_count;
  chttp_session session;
  chttp_session_context session_context;
  chttp_server_route_record *body_route;
  chttp_body_sink body_sink;
  bool body_sink_open;
  bool body_was_streamed;
};

typedef struct chttp_server_chain {
  chttp_server_impl *server;
  const chttp_server_request_view *request;
  chttp_server_response *response;
  chttp_server_route_record *route;
  unsigned int fallback_status;
  unsigned int allowed_methods;
  chttp_server_handler_fn terminal;
  void *terminal_user;
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
  chttp_server_request_state request_state;
  chttp_h2_server_connection *h2;
  chttp_server_websocket_peer websocket_peer;
  unsigned char *websocket_upgrade_input;
  size_t websocket_upgrade_input_capacity;
  size_t websocket_upgrade_input_size;
  unsigned char *outbound;
  size_t outbound_capacity;
  size_t outbound_size;
  uint64_t h2_close_after_ms;
  unsigned char protocol_prefix[24];
  size_t protocol_prefix_size;
  chttp_server_wire_protocol wire_protocol;
  bool active;
  bool connected;
  bool writing;
  bool close_after_write;
  bool response_streaming;
  bool response_source_chunked;
  bool response_close_after_stream;
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
  cflow_io_file_runtime file_runtime;
  chttp_file_transfer **file_transfers;
  size_t file_transfer_capacity;
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
  bool file_runtime_initialized;
};

int chttp_server_response_builder_init(chttp_server_response_builder *builder,
                                       const chttp_server_config *config);
void chttp_server_response_builder_reset(chttp_server_response_builder *builder);
void chttp_server_response_builder_destroy(chttp_server_response_builder *builder);
void chttp_server_response_builder_close_source(chttp_server_response_builder *builder, int status);
int chttp_server_response_source_owned(chttp_server_response *response, unsigned int status_code,
                                       const char *content_type, const chttp_body_source *source,
                                       void (*cleanup)(void *user, int status), void *cleanup_user);
int chttp_server_response_serialize(const chttp_server_response_builder *builder,
                                    const chttp_server_request_view *request, unsigned char *output,
                                    size_t output_capacity, size_t *inout_size);
int chttp_server_error_serialize(unsigned int status_code, unsigned char *output,
                                 size_t output_capacity, size_t *inout_size);

int chttp_server_request_state_init(chttp_server_request_state *state, chttp_server_impl *server);
void chttp_server_request_state_reset(chttp_server_request_state *state);
void chttp_server_request_state_destroy(chttp_server_request_state *state);
int chttp_server_dispatch_request(chttp_server_request_state *state,
                                  const chttp_server_request_view *request);
int chttp_server_request_body_open(chttp_server_request_state *state,
                                   const chttp_server_request_view *request,
                                   chttp_body_sink *out_sink);
int chttp_server_request_body_write(chttp_server_request_state *state, const void *data,
                                    size_t size);
void chttp_server_request_body_close(chttp_server_request_state *state, int status);
bool chttp_server_request_body_streaming(const chttp_server_request_state *state);

int chttp_server_route_register(chttp_server_impl *server,
                                const chttp_server_route_options *options);
int chttp_server_websocket_route_register(chttp_server_impl *server,
                                          const chttp_server_websocket_options *options);
chttp_server_route_record *chttp_server_route_find(chttp_server_request_state *state,
                                                   chttp_method method, const char *path,
                                                   unsigned int *out_allowed_methods,
                                                   int *out_status);
int chttp_server_chain_run(chttp_server_chain *chain);

int chttp_server_websocket_upgrade(void *user, const chttp_server_request_view *request,
                                   chttp_server_parser_upgrade_action *out_action,
                                   unsigned int *out_http_status);
int chttp_server_websocket_input(chttp_server_connection *connection, const void *data,
                                 size_t size);
int chttp_server_websocket_send_complete(chttp_server_connection *connection);
void chttp_server_websocket_transport_closed(chttp_server_connection *connection);
void chttp_server_websocket_reset(chttp_server_connection *connection);

int chttp_session_store_init(chttp_server_impl *server);
void chttp_session_store_destroy(chttp_server_impl *server);
void chttp_session_request_begin(chttp_server_request_state *state,
                                 const chttp_server_request_view *request);
int chttp_session_request_finish(chttp_server_request_state *state);
void chttp_session_request_abort(chttp_server_request_state *state);

void chttp_server_stats_connection_open(chttp_server_impl *server);
void chttp_server_stats_connection_close(chttp_server_impl *server);
void chttp_server_stats_request(chttp_server_impl *server);
void chttp_server_stats_response(chttp_server_impl *server);
void chttp_server_stats_protocol_error(chttp_server_impl *server);
void chttp_server_stats_handler_error(chttp_server_impl *server);

int chttp_server_file_runtime_ensure(chttp_server_impl *server,
                                     cflow_io_file_runtime **out_runtime);
int chttp_server_file_transfer_register(chttp_server_impl *server, chttp_file_transfer *transfer);
int chttp_server_send_pending(chttp_server_connection *connection);
void chttp_server_connection_close(chttp_server_connection *connection);

#endif /* CHTTP_SERVER_RUNTIME_H */
