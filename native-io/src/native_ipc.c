#include <salts/native_ipc.h>

#include <limits.h>
#include <string.h>

enum { SALTS_IPC_PIPE_NAME_CAPACITY = 256, SALTS_IPC_PIPE_MAX_REQUEST_CAPACITY = 255 };

bool salts_ipc_platform_capability_supported(salts_ipc_pipe_capability capability);
int salts_ipc_platform_endpoint_close(uintptr_t handle);
int salts_ipc_platform_server_init(salts_ipc_pipe_server *server,
                                   const salts_ipc_pipe_server_config *config);
int salts_ipc_platform_server_try_accept(salts_ipc_pipe_server *server,
                                         salts_ipc_request_id *out_request_id);
int salts_ipc_platform_server_cancel(salts_ipc_pipe_server *server,
                                     salts_ipc_request_id request_id);
int salts_ipc_platform_server_observe(salts_ipc_pipe_server *server, size_t max_events,
                                      size_t *out_count);
int salts_ipc_platform_server_close(salts_ipc_pipe_server *server);
bool salts_ipc_platform_server_is_quiescent(const salts_ipc_pipe_server *server);
bool salts_ipc_platform_server_get_stats(const salts_ipc_pipe_server *server,
                                         salts_ipc_pipe_server_stats *out_stats);
int salts_ipc_platform_server_destroy(salts_ipc_pipe_server *server);
int salts_ipc_platform_named_pipe_connect(const char *name, salts_ipc_pipe_direction direction,
                                          salts_ipc_pipe_endpoint *out_endpoint);
int salts_ipc_platform_fifo_open(const char *path, salts_ipc_pipe_direction direction,
                                 salts_ipc_pipe_endpoint *out_endpoint);

static bool salts_ipc_direction_valid(salts_ipc_pipe_direction direction) {
  return direction == SALTS_IPC_PIPE_READ || direction == SALTS_IPC_PIPE_WRITE ||
         direction == SALTS_IPC_PIPE_DUPLEX;
}

static bool salts_ipc_name_valid(const char *name) {
  return name != NULL && name[0] != '\0' &&
         memchr(name, '\0', SALTS_IPC_PIPE_NAME_CAPACITY) != NULL;
}

bool salts_ipc_pipe_capability_supported(salts_ipc_pipe_capability capability) {
  return salts_ipc_platform_capability_supported(capability);
}

void salts_ipc_pipe_endpoint_init(salts_ipc_pipe_endpoint *endpoint) {
  if (endpoint == NULL) return;
  endpoint->handle = UINTPTR_MAX;
  endpoint->native_io_flags = 0u;
}

bool salts_ipc_pipe_endpoint_valid(const salts_ipc_pipe_endpoint *endpoint) {
  return endpoint != NULL && endpoint->handle != UINTPTR_MAX;
}

int salts_ipc_pipe_endpoint_close(salts_ipc_pipe_endpoint *endpoint) {
  uintptr_t handle;
  if (endpoint == NULL) return SALTS_EINVAL;
  if (!salts_ipc_pipe_endpoint_valid(endpoint)) return SALTS_OK;
  handle = endpoint->handle;
  salts_ipc_pipe_endpoint_init(endpoint);
  return salts_ipc_platform_endpoint_close(handle);
}

int salts_ipc_pipe_server_init(salts_ipc_pipe_server *server,
                               const salts_ipc_pipe_server_config *config) {
  if (server == NULL || server->impl != NULL || config == NULL ||
      !salts_ipc_name_valid(config->name) || !salts_ipc_direction_valid(config->direction) ||
      config->request_capacity == 0u ||
      config->request_capacity > SALTS_IPC_PIPE_MAX_REQUEST_CAPACITY ||
      config->input_buffer_size == 0u || config->input_buffer_size > UINT32_MAX ||
      config->output_buffer_size == 0u || config->output_buffer_size > UINT32_MAX ||
      config->completion == NULL)
    return SALTS_EINVAL;
  return salts_ipc_platform_server_init(server, config);
}

int salts_ipc_pipe_server_try_accept(salts_ipc_pipe_server *server,
                                     salts_ipc_request_id *out_request_id) {
  if (out_request_id != NULL) *out_request_id = 0u;
  if (server == NULL || server->impl == NULL || out_request_id == NULL) return SALTS_EINVAL;
  return salts_ipc_platform_server_try_accept(server, out_request_id);
}

int salts_ipc_pipe_server_cancel(salts_ipc_pipe_server *server, salts_ipc_request_id request_id) {
  if (server == NULL || server->impl == NULL || request_id == 0u) return SALTS_EINVAL;
  return salts_ipc_platform_server_cancel(server, request_id);
}

int salts_ipc_pipe_server_observe(salts_ipc_pipe_server *server, size_t max_events,
                                  size_t *out_count) {
  if (out_count != NULL) *out_count = 0u;
  if (server == NULL || server->impl == NULL || max_events == 0u || out_count == NULL)
    return SALTS_EINVAL;
  return salts_ipc_platform_server_observe(server, max_events, out_count);
}

int salts_ipc_pipe_server_close(salts_ipc_pipe_server *server) {
  if (server == NULL || server->impl == NULL) return SALTS_EINVAL;
  return salts_ipc_platform_server_close(server);
}

bool salts_ipc_pipe_server_is_quiescent(const salts_ipc_pipe_server *server) {
  return server != NULL && server->impl != NULL && salts_ipc_platform_server_is_quiescent(server);
}

bool salts_ipc_pipe_server_get_stats(const salts_ipc_pipe_server *server,
                                     salts_ipc_pipe_server_stats *out_stats) {
  return server != NULL && server->impl != NULL && out_stats != NULL &&
         salts_ipc_platform_server_get_stats(server, out_stats);
}

int salts_ipc_pipe_server_destroy(salts_ipc_pipe_server *server) {
  if (server == NULL || server->impl == NULL) return SALTS_EINVAL;
  return salts_ipc_platform_server_destroy(server);
}

int salts_ipc_named_pipe_connect(const char *name, salts_ipc_pipe_direction direction,
                                 salts_ipc_pipe_endpoint *out_endpoint) {
  if (!salts_ipc_name_valid(name) || !salts_ipc_direction_valid(direction) ||
      out_endpoint == NULL || salts_ipc_pipe_endpoint_valid(out_endpoint))
    return SALTS_EINVAL;
  return salts_ipc_platform_named_pipe_connect(name, direction, out_endpoint);
}

int salts_ipc_fifo_open(const char *path, salts_ipc_pipe_direction direction,
                        salts_ipc_pipe_endpoint *out_endpoint) {
  if (path == NULL || path[0] == '\0' ||
      (direction != SALTS_IPC_PIPE_READ && direction != SALTS_IPC_PIPE_WRITE) ||
      out_endpoint == NULL || salts_ipc_pipe_endpoint_valid(out_endpoint))
    return SALTS_EINVAL;
  return salts_ipc_platform_fifo_open(path, direction, out_endpoint);
}
