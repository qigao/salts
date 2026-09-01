#include "chttp_server_runtime.h"

#include <turbo/clock.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  CHTTP_SERVER_ERROR_RESPONSE_BYTES = 256,
  CHTTP_SERVER_COOKIE_NAME_BYTES = 64,
  CHTTP_SERVER_GENERATED_RESPONSE_BYTES = 256
};

static const char CHTTP_SERVER_CONTINUE_RESPONSE[] = "HTTP/1.1 100 Continue\r\n\r\n";
static TURBO_THREAD_LOCAL chttp_server_impl *chttp_active_callback_server;

static int chttp_server_on_request(void *user, const chttp_server_request_view *request);
static int chttp_server_on_continue(void *user);

static char *chttp_server_string_copy(const char *value) {
  size_t size;
  char *copy;
  if (value == NULL) return NULL;
  size = strlen(value) + 1u;
  if (size == 0u) return NULL;
  copy = (char *)malloc(size);
  if (copy != NULL) memcpy(copy, value, size);
  return copy;
}

static bool chttp_server_multiply(size_t left, size_t right, size_t *out) {
  if (out == NULL || (right != 0u && left > SIZE_MAX / right)) return false;
  *out = left * right;
  return true;
}

static bool chttp_server_cookie_name_valid(const char *name) {
  const unsigned char *cursor = (const unsigned char *)name;
  size_t size = 0u;
  if (cursor == NULL || *cursor == 0u) return false;
  for (; *cursor != 0u; ++cursor, ++size) {
    const unsigned char ch = *cursor;
    if (size >= CHTTP_SERVER_COOKIE_NAME_BYTES) return false;
    if ((ch >= (unsigned char)'0' && ch <= (unsigned char)'9') ||
        (ch >= (unsigned char)'A' && ch <= (unsigned char)'Z') ||
        (ch >= (unsigned char)'a' && ch <= (unsigned char)'z'))
      continue;
    if (strchr("!#$%&'*+-.^_`|~", (int)ch) == NULL) return false;
  }
  return true;
}

static bool chttp_server_power_of_two(size_t value) {
  return value != 0u && (value & (value - 1u)) == 0u;
}

static int chttp_server_config_validate(const chttp_server_config *config) {
  size_t response_bound;
  const char *cookie_name;
  if (config == NULL || config->host == NULL || config->host[0] == '\0' || config->backlog == 0u ||
      config->backlog > INT_MAX || !native_io_backend_kind_supported(config->network.backend) ||
      config->network.connection_capacity == 0u ||
      !chttp_server_power_of_two(config->network.command_capacity) ||
      config->network.request_capacity == 0u || config->network.completion_batch_capacity == 0u ||
      config->network.completion_batch_capacity > config->network.request_capacity ||
      !chttp_server_power_of_two(config->network.event_capacity) ||
      config->network.event_capacity < 2u ||
      config->network.max_send_bytes < CHTTP_SERVER_ERROR_RESPONSE_BYTES ||
      config->network.receive_buffer_bytes == 0u || config->route_capacity == 0u ||
      config->max_target_bytes == 0u || config->max_header_count == 0u ||
      config->max_header_bytes == 0u || config->max_request_body_bytes == 0u ||
      config->max_response_header_count == 0u || config->max_response_header_bytes == 0u ||
      config->max_response_body_bytes == 0u || config->poll_slice_ms == 0u)
    return TURBO_EINVAL;
  if ((config->max_route_param_count != 0u && config->max_route_param_bytes == 0u) ||
      config->max_route_middleware_count > SIZE_MAX / sizeof(chttp_server_middleware) ||
      config->middleware_capacity > SIZE_MAX / sizeof(chttp_server_middleware) ||
      config->max_route_param_count > SIZE_MAX / sizeof(chttp_server_param) ||
      config->route_capacity > SIZE_MAX / sizeof(chttp_server_route_record) ||
      config->network.connection_capacity > SIZE_MAX / sizeof(chttp_server_connection) ||
      config->network.connection_capacity > UINT32_MAX ||
      config->max_response_body_bytes > SIZE_MAX - config->max_response_header_bytes ||
      config->max_response_body_bytes + config->max_response_header_bytes >
          SIZE_MAX - CHTTP_SERVER_GENERATED_RESPONSE_BYTES)
    return TURBO_ERANGE;
  response_bound = config->max_response_body_bytes + config->max_response_header_bytes +
                   CHTTP_SERVER_GENERATED_RESPONSE_BYTES;
  if (response_bound >
      config->network.max_send_bytes - (sizeof(CHTTP_SERVER_CONTINUE_RESPONSE) - 1u))
    return TURBO_EMSGSIZE;
  if (config->session_capacity == 0u) return TURBO_OK;
  cookie_name = config->session_cookie_name == NULL ? "chttp_sid" : config->session_cookie_name;
  if (config->session_entry_capacity == 0u || config->max_session_key_bytes == 0u ||
      config->max_session_value_bytes == 0u || config->session_idle_timeout_ms == 0u ||
      !chttp_server_cookie_name_valid(cookie_name))
    return TURBO_EINVAL;
  return TURBO_OK;
}

static void chttp_server_connection_destroy(chttp_server_connection *connection) {
  if (connection == NULL) return;
  chttp_server_parser_destroy(&connection->parser);
  chttp_server_response_builder_destroy(&connection->response_builder);
  free(connection->outbound);
  free(connection->param_storage);
  free(connection->params);
  *connection = (chttp_server_connection){0};
}

static void chttp_server_impl_free(chttp_server_impl *impl) {
  size_t index;
  if (impl == NULL) return;
  if (impl->connections != NULL)
    for (index = 0u; index < impl->config.network.connection_capacity; ++index)
      chttp_server_connection_destroy(&impl->connections[index]);
  chttp_session_store_destroy(impl);
  free(impl->connections);
  free(impl->middleware);
  free(impl->route_middleware);
  free(impl->route_paths);
  free(impl->routes);
  free(impl->session_cookie_name);
  free(impl->host);
  if (impl->sync_initialized) {
    turbo_cond_destroy(&impl->changed);
    turbo_mutex_destroy(&impl->mutex);
  }
  free(impl);
}

static int chttp_server_connection_init(chttp_server_impl *server,
                                        chttp_server_connection *connection) {
  const chttp_server_parser_config parser_config = {
      .max_target_bytes = server->config.max_target_bytes,
      .max_header_count = server->config.max_header_count,
      .max_header_bytes = server->config.max_header_bytes,
      .max_body_bytes = server->config.max_request_body_bytes,
      .on_request = chttp_server_on_request,
      .on_continue = chttp_server_on_continue,
      .user = connection};
  int status;
  connection->server = server;
  connection->param_storage_capacity = server->config.max_route_param_bytes;
  connection->outbound_capacity = server->config.network.max_send_bytes;
  if (server->config.max_route_param_count != 0u)
    connection->params = (chttp_server_param *)calloc(server->config.max_route_param_count,
                                                      sizeof(*connection->params));
  if (server->config.max_route_param_bytes != 0u)
    connection->param_storage = (char *)malloc(server->config.max_route_param_bytes);
  connection->outbound = (unsigned char *)malloc(connection->outbound_capacity);
  if ((server->config.max_route_param_count != 0u && connection->params == NULL) ||
      (server->config.max_route_param_bytes != 0u && connection->param_storage == NULL) ||
      connection->outbound == NULL)
    return TURBO_ENOMEM;
  status = chttp_server_response_builder_init(&connection->response_builder, &server->config);
  if (status != TURBO_OK) return status;
  connection->response.impl = &connection->response_builder;
  status = chttp_server_parser_init(&connection->parser, &parser_config);
  return status;
}

int chttp_server_init(chttp_server *server, const chttp_server_config *config) {
  chttp_server_impl *impl;
  const char *cookie_name;
  size_t route_path_stride;
  size_t route_path_bytes;
  size_t route_middleware_count;
  size_t index;
  int status;
  if (server == NULL) return TURBO_EINVAL;
  if (server->impl != NULL) return TURBO_EALREADY;
  status = chttp_server_config_validate(config);
  if (status != TURBO_OK) return status;
  route_path_stride = config->max_target_bytes + 1u;
  if (route_path_stride == 0u ||
      !chttp_server_multiply(config->route_capacity, route_path_stride, &route_path_bytes) ||
      !chttp_server_multiply(config->route_capacity, config->max_route_middleware_count,
                             &route_middleware_count) ||
      (route_middleware_count != 0u &&
       route_middleware_count > SIZE_MAX / sizeof(chttp_server_middleware)))
    return TURBO_ERANGE;
  impl = (chttp_server_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return TURBO_ENOMEM;
  impl->config = *config;
  impl->max_response_wire_bytes = config->max_response_body_bytes +
                                  config->max_response_header_bytes +
                                  CHTTP_SERVER_GENERATED_RESPONSE_BYTES;
  cookie_name = config->session_cookie_name == NULL ? "chttp_sid" : config->session_cookie_name;
  impl->host = chttp_server_string_copy(config->host);
  impl->session_cookie_name = chttp_server_string_copy(cookie_name);
  impl->routes = (chttp_server_route_record *)calloc(config->route_capacity, sizeof(*impl->routes));
  impl->route_paths = (char *)calloc(route_path_bytes, 1u);
  if (route_middleware_count != 0u)
    impl->route_middleware =
        (chttp_server_middleware *)calloc(route_middleware_count, sizeof(*impl->route_middleware));
  if (config->middleware_capacity != 0u)
    impl->middleware =
        (chttp_server_middleware *)calloc(config->middleware_capacity, sizeof(*impl->middleware));
  impl->connections = (chttp_server_connection *)calloc(config->network.connection_capacity,
                                                        sizeof(*impl->connections));
  if (impl->host == NULL || impl->session_cookie_name == NULL || impl->routes == NULL ||
      impl->route_paths == NULL ||
      (route_middleware_count != 0u && impl->route_middleware == NULL) ||
      (config->middleware_capacity != 0u && impl->middleware == NULL) ||
      impl->connections == NULL) {
    chttp_server_impl_free(impl);
    return TURBO_ENOMEM;
  }
  impl->config.host = impl->host;
  impl->config.session_cookie_name = impl->session_cookie_name;
  for (index = 0u; index < config->route_capacity; ++index) {
    impl->routes[index].path = impl->route_paths + index * route_path_stride;
    if (config->max_route_middleware_count != 0u)
      impl->routes[index].middleware =
          impl->route_middleware + index * config->max_route_middleware_count;
  }
  status = chttp_session_store_init(impl);
  if (status != TURBO_OK) {
    chttp_server_impl_free(impl);
    return status;
  }
  for (index = 0u; index < config->network.connection_capacity; ++index) {
    status = chttp_server_connection_init(impl, &impl->connections[index]);
    if (status != TURBO_OK) {
      chttp_server_impl_free(impl);
      return status;
    }
  }
  turbo_mutex_init(&impl->mutex);
  turbo_cond_init(&impl->changed);
  impl->sync_initialized = true;
  impl->stats.terminal_status = TURBO_OK;
  server->impl = impl;
  return TURBO_OK;
}

static void chttp_server_stats_update(chttp_server_impl *server, int field) {
  turbo_mutex_lock(&server->mutex);
  switch (field) {
  case 1:
    ++server->stats.accepted_connections;
    ++server->stats.active_connections;
    break;
  case 2:
    if (server->stats.active_connections != 0u) --server->stats.active_connections;
    break;
  case 3:
    ++server->stats.requests;
    break;
  case 4:
    ++server->stats.responses;
    break;
  case 5:
    ++server->stats.protocol_errors;
    break;
  case 6:
    ++server->stats.handler_errors;
    break;
  default:
    break;
  }
  turbo_mutex_unlock(&server->mutex);
}

void chttp_server_stats_connection_open(chttp_server_impl *server) {
  chttp_server_stats_update(server, 1);
}
void chttp_server_stats_connection_close(chttp_server_impl *server) {
  chttp_server_stats_update(server, 2);
}
void chttp_server_stats_request(chttp_server_impl *server) { chttp_server_stats_update(server, 3); }
void chttp_server_stats_response(chttp_server_impl *server) {
  chttp_server_stats_update(server, 4);
}
void chttp_server_stats_protocol_error(chttp_server_impl *server) {
  chttp_server_stats_update(server, 5);
}
void chttp_server_stats_handler_error(chttp_server_impl *server) {
  chttp_server_stats_update(server, 6);
}

static bool chttp_server_connection_matches(const chttp_server_connection *connection,
                                            cnet_connection handle) {
  return connection != NULL && connection->active && connection->handle.slot == handle.slot &&
         connection->handle.generation == handle.generation;
}

static bool chttp_server_action_pressure(int status) {
  return status == TURBO_ENOBUFS || status == TURBO_EBUSY;
}

static int chttp_server_connection_retry(chttp_server_connection *connection) {
  chttp_server_pending_action action;
  int status;
  if (connection == NULL || !connection->active || !connection->connected || connection->writing)
    return TURBO_OK;
  action = connection->pending_action;
  if (action == CHTTP_SERVER_PENDING_NONE) return TURBO_OK;
  if (action == CHTTP_SERVER_PENDING_RECEIVE)
    status = cnet_receive(&connection->server->network, connection->handle, 1u);
  else if (action == CHTTP_SERVER_PENDING_SEND)
    status = connection->close_after_write
                 ? cnet_send_and_close(&connection->server->network, connection->handle,
                                       connection->outbound, connection->outbound_size)
                 : cnet_send(&connection->server->network, connection->handle, connection->outbound,
                             connection->outbound_size);
  else status = cnet_close(&connection->server->network, connection->handle);
  if (status == TURBO_OK) {
    connection->pending_action = CHTTP_SERVER_PENDING_NONE;
    if (action == CHTTP_SERVER_PENDING_SEND) connection->writing = true;
    return TURBO_OK;
  }
  if (action == CHTTP_SERVER_PENDING_CLOSE &&
      (status == TURBO_EALREADY || status == TURBO_ESHUTDOWN)) {
    connection->pending_action = CHTTP_SERVER_PENDING_NONE;
    return TURBO_OK;
  }
  return status;
}

static void chttp_server_connection_close(chttp_server_connection *connection) {
  int status;
  if (connection == NULL || !connection->active) return;
  connection->close_after_write = true;
  if (connection->pending_action != CHTTP_SERVER_PENDING_SEND)
    connection->pending_action = CHTTP_SERVER_PENDING_CLOSE;
  status = chttp_server_connection_retry(connection);
  (void)status;
}

static int chttp_server_connection_receive(chttp_server_connection *connection) {
  int status;
  if (connection == NULL || connection->close_after_write) return TURBO_ESHUTDOWN;
  connection->pending_action = CHTTP_SERVER_PENDING_RECEIVE;
  status = chttp_server_connection_retry(connection);
  if (status != TURBO_OK && !chttp_server_action_pressure(status))
    chttp_server_connection_close(connection);
  return status;
}

static void chttp_server_on_state(void *user, cnet_connection handle, cnet_connection_state state,
                                  const cnet_error *error) {
  chttp_server_connection *connection = (chttp_server_connection *)user;
  (void)error;
  if (!chttp_server_connection_matches(connection, handle)) return;
  if (state == CNET_CONNECTION_CONNECTED) {
    connection->connected = true;
    (void)chttp_server_connection_receive(connection);
  } else if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED) {
    connection->active = false;
    connection->connected = false;
    connection->writing = false;
    connection->close_after_write = false;
    connection->pending_action = CHTTP_SERVER_PENDING_NONE;
    connection->outbound_size = 0u;
    connection->handle = (cnet_connection){0};
    chttp_server_response_builder_reset(&connection->response_builder);
    (void)chttp_server_parser_reset(&connection->parser);
    chttp_server_stats_connection_close(connection->server);
  }
}

static int chttp_server_send_pending(chttp_server_connection *connection) {
  int status;
  if (connection->outbound_size == 0u) return chttp_server_connection_receive(connection);
  connection->pending_action = CHTTP_SERVER_PENDING_SEND;
  status = chttp_server_connection_retry(connection);
  if (status != TURBO_OK && !chttp_server_action_pressure(status))
    chttp_server_connection_close(connection);
  return status;
}

static void chttp_server_on_receive(void *user, cnet_connection handle,
                                    const cnet_receive_view *view) {
  chttp_server_connection *connection = (chttp_server_connection *)user;
  unsigned int http_status = 0u;
  int status;
  if (!chttp_server_connection_matches(connection, handle) || view == NULL ||
      view->kind != CNET_MESSAGE_BYTES || connection->writing) {
    chttp_server_connection_close(connection);
    return;
  }
  status = chttp_server_parser_execute(&connection->parser, view->data, view->size, &http_status);
  if (status != TURBO_OK) {
    if (http_status == 0u) http_status = 500u;
    if (status == TURBO_EPROTO) chttp_server_stats_protocol_error(connection->server);
    else chttp_server_stats_handler_error(connection->server);
    if (chttp_server_error_serialize(http_status, connection->outbound,
                                     connection->outbound_capacity,
                                     &connection->outbound_size) == TURBO_OK)
      chttp_server_stats_response(connection->server);
    connection->close_after_write = true;
  }
  (void)chttp_server_send_pending(connection);
}

static void chttp_server_on_send(void *user, cnet_connection handle, size_t size) {
  chttp_server_connection *connection = (chttp_server_connection *)user;
  if (!chttp_server_connection_matches(connection, handle) || !connection->writing ||
      size != connection->outbound_size) {
    chttp_server_connection_close(connection);
    return;
  }
  connection->writing = false;
  connection->outbound_size = 0u;
  if (connection->close_after_write) chttp_server_connection_close(connection);
  else (void)chttp_server_connection_receive(connection);
}

static int chttp_server_on_continue(void *user) {
  chttp_server_connection *connection = (chttp_server_connection *)user;
  if (connection == NULL || connection->outbound_size > connection->outbound_capacity ||
      sizeof(CHTTP_SERVER_CONTINUE_RESPONSE) - 1u >
          connection->outbound_capacity - connection->outbound_size)
    return TURBO_EMSGSIZE;
  memcpy(connection->outbound + connection->outbound_size, CHTTP_SERVER_CONTINUE_RESPONSE,
         sizeof(CHTTP_SERVER_CONTINUE_RESPONSE) - 1u);
  connection->outbound_size += sizeof(CHTTP_SERVER_CONTINUE_RESPONSE) - 1u;
  return TURBO_OK;
}

static int chttp_server_on_request(void *user, const chttp_server_request_view *request) {
  chttp_server_connection *connection = (chttp_server_connection *)user;
  chttp_server_impl *server;
  chttp_server_request_view routed_request;
  chttp_server_route_record *route;
  chttp_server_chain chain;
  unsigned int allowed_methods = 0u;
  unsigned int fallback_status;
  chttp_server_impl *previous_callback_server;
  int route_status = TURBO_OK;
  int status;
  if (connection == NULL || request == NULL || connection->close_after_write)
    return TURBO_ESHUTDOWN;
  server = connection->server;
  chttp_server_stats_request(server);
  if (connection->outbound_size > connection->outbound_capacity ||
      server->max_response_wire_bytes > connection->outbound_capacity - connection->outbound_size)
    return TURBO_ENOBUFS;
  route = chttp_server_route_find(connection, request->method, request->path, &allowed_methods,
                                  &route_status);
  fallback_status = allowed_methods != 0u ? 405u : 404u;
  if (route_status == TURBO_ENOBUFS) fallback_status = 414u;
  else if (route_status != TURBO_OK) return route_status;
  routed_request = *request;
  routed_request.params = connection->params;
  routed_request.param_count = connection->param_count;
  routed_request.session = server->config.session_capacity == 0u ? NULL : &connection->session;
  chttp_session_request_begin(connection, &routed_request);
  chttp_server_response_builder_reset(&connection->response_builder);
  chain = (chttp_server_chain){.server = server,
                               .request = &routed_request,
                               .response = &connection->response,
                               .route = route,
                               .fallback_status = fallback_status,
                               .allowed_methods = allowed_methods};
  previous_callback_server = chttp_active_callback_server;
  chttp_active_callback_server = server;
  status = chttp_server_chain_run(&chain);
  chttp_active_callback_server = previous_callback_server;
  if (status == TURBO_OK && !connection->response_builder.replied)
    status = chttp_server_reply(&connection->response, 204u, NULL, NULL, 0u);
  if (status == TURBO_OK) status = chttp_session_request_finish(connection);
  if (status != TURBO_OK) {
    chttp_session_request_abort(connection);
    chttp_server_stats_handler_error(server);
    chttp_server_response_builder_reset(&connection->response_builder);
    status =
        chttp_server_reply(&connection->response, 500u, "text/plain", "Internal Server Error", 21u);
  }
  if (status == TURBO_OK) {
    status = chttp_server_response_serialize(&connection->response_builder, &routed_request,
                                             connection->outbound, connection->outbound_capacity,
                                             &connection->outbound_size);
    if (status != TURBO_OK) chttp_session_request_abort(connection);
  }
  if (status == TURBO_OK) {
    chttp_server_stats_response(server);
    if (!routed_request.protocol_keep_alive) connection->close_after_write = true;
  }
  return status;
}

static chttp_server_connection *chttp_server_free_connection(chttp_server_impl *server) {
  size_t index;
  for (index = 0u; index < server->config.network.connection_capacity; ++index)
    if (!server->connections[index].active) return &server->connections[index];
  return NULL;
}

static int chttp_server_accept_ready(chttp_server_impl *server) {
  for (;;) {
    chttp_server_connection *connection = chttp_server_free_connection(server);
    cnet_observer observer;
    cnet_connection handle = {0};
    int status;
    if (connection == NULL) return TURBO_OK;
    observer = (cnet_observer){.on_state = chttp_server_on_state,
                               .on_receive = chttp_server_on_receive,
                               .on_send = chttp_server_on_send,
                               .user = connection};
    status = cnet_listener_accept(&server->listener, &server->network, &observer, &handle);
    if (status == TURBO_ETIMEDOUT) return TURBO_OK;
    if (status == TURBO_ENOBUFS) {
      turbo_mutex_lock(&server->mutex);
      ++server->stats.rejected_connections;
      turbo_mutex_unlock(&server->mutex);
      return TURBO_OK;
    }
    if (status != TURBO_OK) return status;
    connection->handle = handle;
    connection->active = true;
    connection->connected = false;
    connection->writing = false;
    connection->close_after_write = false;
    connection->pending_action = CHTTP_SERVER_PENDING_NONE;
    connection->outbound_size = 0u;
    chttp_server_response_builder_reset(&connection->response_builder);
    status = chttp_server_parser_reset(&connection->parser);
    if (status != TURBO_OK) return status;
    chttp_server_stats_connection_open(server);
  }
}

static int chttp_server_retry_pending(chttp_server_impl *server) {
  const size_t capacity = server->config.network.connection_capacity;
  size_t offset;
  for (offset = 0u; offset < capacity; ++offset) {
    const size_t index = (server->pending_retry_cursor + offset) % capacity;
    chttp_server_connection *connection = &server->connections[index];
    int status;
    if (!connection->active || connection->pending_action == CHTTP_SERVER_PENDING_NONE) continue;
    status = chttp_server_connection_retry(connection);
    server->pending_retry_cursor = (index + 1u) % capacity;
    if (status == TURBO_ENOBUFS) return TURBO_OK;
    if (status != TURBO_OK && status != TURBO_EBUSY) return status;
  }
  return TURBO_OK;
}

static bool chttp_server_should_stop(chttp_server_impl *server) {
  bool stop;
  turbo_mutex_lock(&server->mutex);
  stop = server->stop_requested;
  turbo_mutex_unlock(&server->mutex);
  return stop;
}

static void chttp_server_worker_finish(chttp_server_impl *server, int terminal_status) {
  turbo_mutex_lock(&server->mutex);
  server->stats.running = 0;
  server->stats.stopping = 0;
  server->stats.terminal_status = terminal_status;
  server->worker_done = true;
  turbo_cond_broadcast(&server->changed);
  turbo_mutex_unlock(&server->mutex);
}

static int chttp_server_cleanup_network(chttp_server_impl *server, bool retry_timeouts) {
  int first_status = TURBO_OK;
  if (server->listener_initialized) {
    const int close_status = cnet_listener_close(&server->listener);
    if (close_status != TURBO_OK && close_status != TURBO_EALREADY) first_status = close_status;
  }
  if (server->network_initialized) {
    int stop_status;
    do {
      stop_status = cnet_client_stop(&server->network, server->config.poll_slice_ms);
    } while (retry_timeouts && stop_status == TURBO_ETIMEDOUT);
    if (stop_status == TURBO_EALREADY) stop_status = TURBO_OK;
    if (first_status == TURBO_OK && stop_status != TURBO_OK) first_status = stop_status;
    if (stop_status != TURBO_ETIMEDOUT && stop_status != TURBO_EBUSY) {
      const int destroy_status = cnet_client_destroy(&server->network);
      if (first_status == TURBO_OK && destroy_status != TURBO_OK) first_status = destroy_status;
      if (destroy_status == TURBO_OK) server->network_initialized = false;
    }
  }
  if (server->listener_initialized) {
    const int destroy_status = cnet_listener_destroy(&server->listener);
    if (first_status == TURBO_OK && destroy_status != TURBO_OK) first_status = destroy_status;
    if (destroy_status == TURBO_OK) server->listener_initialized = false;
  }
  return first_status;
}

static void chttp_server_worker(void *user) {
  chttp_server_impl *server = (chttp_server_impl *)user;
  int status = TURBO_OK;
  while (!chttp_server_should_stop(server)) {
    int ready = 0;
    size_t events = 0u;
    status = chttp_server_retry_pending(server);
    if (status != TURBO_OK) break;
    status = cnet_listener_wait(&server->listener, 0u, &ready);
    if (status != TURBO_OK) break;
    if (ready) {
      status = chttp_server_accept_ready(server);
      if (status != TURBO_OK) break;
    }
    status = cnet_client_poll(&server->network, server->config.poll_slice_ms, &events);
    if (status != TURBO_OK) break;
    status = chttp_server_retry_pending(server);
    if (status != TURBO_OK) break;
  }
  {
    const int cleanup_status = chttp_server_cleanup_network(server, true);
    if (status == TURBO_OK && cleanup_status != TURBO_OK) status = cleanup_status;
  }
  chttp_server_worker_finish(server, status);
}

int chttp_server_start(chttp_server *server) {
  chttp_server_impl *impl;
  cnet_listener_config listener_config;
  uint16_t port = 0u;
  int status;
  if (server == NULL || server->impl == NULL) return TURBO_EINVAL;
  impl = (chttp_server_impl *)server->impl;
  if (impl->start_called || impl->thread_started) return TURBO_EALREADY;
  status = cnet_client_init(&impl->network, &impl->config.network);
  if (status != TURBO_OK) return status;
  impl->network_initialized = true;
  listener_config = (cnet_listener_config){.backend = impl->config.network.backend,
                                           .host = impl->host,
                                           .port = impl->config.port,
                                           .backlog = impl->config.backlog};
  status = cnet_listener_init(&impl->listener, &listener_config);
  if (status == TURBO_OK) {
    impl->listener_initialized = true;
    status = cnet_listener_port(&impl->listener, &port);
  }
  if (status != TURBO_OK) {
    (void)chttp_server_cleanup_network(impl, false);
    return status;
  }
  turbo_mutex_lock(&impl->mutex);
  impl->stats.port = port;
  impl->stats.running = 1;
  impl->stats.stopping = 0;
  impl->stats.terminal_status = TURBO_OK;
  impl->stop_requested = false;
  impl->worker_done = false;
  turbo_mutex_unlock(&impl->mutex);
  status = turbo_thread_create(&impl->thread, chttp_server_worker, impl);
  if (status != TURBO_OK) {
    (void)chttp_server_cleanup_network(impl, false);
    turbo_mutex_lock(&impl->mutex);
    impl->stats.running = 0;
    impl->stats.terminal_status = TURBO_EIO;
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_EIO;
  }
  impl->thread_started = true;
  impl->start_called = true;
  return TURBO_OK;
}

int chttp_server_port(const chttp_server *server, uint16_t *out_port) {
  const chttp_server_impl *impl;
  if (out_port == NULL) return TURBO_EINVAL;
  *out_port = 0u;
  if (server == NULL || server->impl == NULL) return TURBO_EINVAL;
  impl = (const chttp_server_impl *)server->impl;
  turbo_mutex_lock((turbo_mutex_t *)&impl->mutex);
  if (!impl->start_called) {
    turbo_mutex_unlock((turbo_mutex_t *)&impl->mutex);
    return TURBO_EINVAL;
  }
  *out_port = impl->stats.port;
  turbo_mutex_unlock((turbo_mutex_t *)&impl->mutex);
  return TURBO_OK;
}

int chttp_server_stop(chttp_server *server, uint32_t timeout_ms) {
  chttp_server_impl *impl;
  const uint64_t started_ms = turbo_monotonic_ms();
  int terminal_status;
  if (server == NULL || server->impl == NULL) return TURBO_EINVAL;
  impl = (chttp_server_impl *)server->impl;
  if (chttp_active_callback_server == impl) return TURBO_EBUSY;
  turbo_mutex_lock(&impl->mutex);
  if (!impl->start_called) {
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
  }
  impl->stop_requested = true;
  if (!impl->worker_done) impl->stats.stopping = 1;
  while (!impl->worker_done) {
    if (timeout_ms == 0u) turbo_cond_wait(&impl->changed, &impl->mutex);
    else {
      const uint64_t elapsed_ms = turbo_monotonic_ms() - started_ms;
      uint64_t remaining_ns;
      if (elapsed_ms >= timeout_ms) {
        turbo_mutex_unlock(&impl->mutex);
        return TURBO_ETIMEDOUT;
      }
      remaining_ns = ((uint64_t)timeout_ms - elapsed_ms) * 1000000u;
      if (turbo_cond_timedwait(&impl->changed, &impl->mutex, remaining_ns) != TURBO_OK &&
          !impl->worker_done) {
        turbo_mutex_unlock(&impl->mutex);
        return TURBO_ETIMEDOUT;
      }
    }
  }
  terminal_status = impl->stats.terminal_status;
  turbo_mutex_unlock(&impl->mutex);
  if (impl->thread_started) {
    if (turbo_thread_join(&impl->thread) != TURBO_OK) return TURBO_EIO;
    turbo_thread_destroy(&impl->thread);
    impl->thread_started = false;
  }
  {
    const int cleanup_status = chttp_server_cleanup_network(impl, false);
    if (terminal_status == TURBO_OK && cleanup_status != TURBO_OK) terminal_status = cleanup_status;
  }
  return terminal_status;
}

int chttp_server_destroy(chttp_server *server) {
  chttp_server_impl *impl;
  if (server == NULL) return TURBO_EINVAL;
  if (server->impl == NULL) return TURBO_OK;
  impl = (chttp_server_impl *)server->impl;
  turbo_mutex_lock(&impl->mutex);
  if (impl->stats.running || impl->stats.stopping || (impl->start_called && !impl->worker_done)) {
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_EBUSY;
  }
  turbo_mutex_unlock(&impl->mutex);
  if (impl->thread_started) {
    if (turbo_thread_join(&impl->thread) != TURBO_OK) return TURBO_EIO;
    turbo_thread_destroy(&impl->thread);
    impl->thread_started = false;
  }
  {
    const int cleanup_status = chttp_server_cleanup_network(impl, false);
    if (cleanup_status != TURBO_OK) return cleanup_status;
  }
  if (impl->network_initialized || impl->listener_initialized) return TURBO_EBUSY;
  chttp_server_impl_free(impl);
  server->impl = NULL;
  return TURBO_OK;
}

int chttp_server_get_stats(const chttp_server *server, chttp_server_stats *out_stats) {
  const chttp_server_impl *impl;
  if (out_stats == NULL) return TURBO_EINVAL;
  *out_stats = (chttp_server_stats){0};
  if (server == NULL || server->impl == NULL) return TURBO_EINVAL;
  impl = (const chttp_server_impl *)server->impl;
  turbo_mutex_lock((turbo_mutex_t *)&impl->mutex);
  *out_stats = impl->stats;
  turbo_mutex_unlock((turbo_mutex_t *)&impl->mutex);
  return TURBO_OK;
}
