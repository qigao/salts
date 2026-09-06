#include <salts/native_io.h>
#include <salts/native_ipc.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

int main() {
  salts_ipc_pipe_endpoint endpoint{};
  salts_ipc_pipe_server server{};
  salts_ipc_pipe_server_config config{};
  salts_ipc_pipe_server_stats stats{};
  const auto capability = &salts_ipc_pipe_capability_supported;
  const auto endpoint_close = &salts_ipc_pipe_endpoint_close;
  const auto server_init = &salts_ipc_pipe_server_init;
  const auto server_try_accept = &salts_ipc_pipe_server_try_accept;
  const auto server_cancel = &salts_ipc_pipe_server_cancel;
  const auto server_observe = &salts_ipc_pipe_server_observe;
  const auto server_close = &salts_ipc_pipe_server_close;
  const auto server_is_quiescent = &salts_ipc_pipe_server_is_quiescent;
  const auto server_get_stats = &salts_ipc_pipe_server_get_stats;
  const auto server_destroy = &salts_ipc_pipe_server_destroy;
  const auto named_pipe_connect = &salts_ipc_named_pipe_connect;
  const auto fifo_open = &salts_ipc_fifo_open;
  static_assert(std::is_standard_layout_v<salts_ipc_pipe_endpoint>);
  static_assert(std::is_standard_layout_v<salts_ipc_completion>);
  static_assert(sizeof(salts_ipc_request_id) == sizeof(std::uint64_t));
  static_assert(offsetof(salts_ipc_pipe_endpoint, handle) == 0u);
  static_assert(SALTS_IPC_PIPE_READ == 1u);
  static_assert(SALTS_IPC_PIPE_WRITE == 2u);
  static_assert(SALTS_IPC_PIPE_DUPLEX == 3u);
  (void)server;
  (void)config;
  (void)stats;
  (void)capability;
  (void)endpoint_close;
  (void)server_init;
  (void)server_try_accept;
  (void)server_cancel;
  (void)server_observe;
  (void)server_close;
  (void)server_is_quiescent;
  (void)server_get_stats;
  (void)server_destroy;
  (void)named_pipe_connect;
  (void)fifo_open;
  salts_ipc_pipe_endpoint_init(&endpoint);
  return !salts_ipc_pipe_endpoint_valid(&endpoint) && endpoint.native_io_flags == 0u &&
                 endpoint.handle == UINTPTR_MAX
             ? 0
             : 1;
}
