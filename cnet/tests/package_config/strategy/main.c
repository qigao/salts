/* Standalone installed SDK consumer, built as both strict C11 and C++17.
 * No source-tree private header, build-tree link or protocol DATA replay. */
#include <cnet/cnet.h>
#include <cnet/manager.h>
#include <cnet/owner_placement.h>
#include <cnet/destination_policy.h>
#include <cnet/client_pool.h>
#include <cnet/recovery_policy.h>
#include <cnet/managed_dial.h>
#include <cnet/sg_host.h>
#include <salts/native_io_sharded.h>
#include <salts/error_codes.h>
#include <stdint.h>
#include <stdio.h>

#define CHECK(x) do { \
  if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
    return 1; \
  } \
} while (0)

static native_io_backend_kind backend_kind(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__)
  return NATIVE_IO_BACKEND_KQUEUE;
#else
  return NATIVE_IO_BACKEND_EPOLL;
#endif
}
static void observed_state(void *user, cnet_connection connection,
                           cnet_connection_state state, const cnet_error *error) {
  (void)user; (void)connection; (void)state; (void)error;
}

int main(void) {
  cnet_client client = {0};
  cnet_client_config client_config = {0};
  cnet_manager manager = {0};
  cnet_manager_config manager_config = {0};
  cnet_client_pool pool = {0};
  cnet_pool_config pool_config = {0};
  cnet_pool_key key = {0};
  cnet_pool_connection physical = {0};
  cnet_pool_snapshot pool_snapshot = {0};
  cnet_reconnect_config reconnect_config = {0};
  cnet_reconnect_state reconnect_state = {0};
  cnet_reconnect_ticket reconnect_ticket = {0};
  cnet_reconnect_snapshot reconnect_snapshot = {0};
  cnet_retry_input retry_input = {0};
  cnet_retry_result retry_result = {0};
  cnet_managed_dial managed_dial = {0};
  cnet_managed_dial_config dial_config = {0};
  cnet_sg_host_routes routes = {0};
  native_io_sharded_host_lease invalid_lease = {0};
  native_io_sharded_completion ignored_completion = {0};
  size_t accepts = 42u, sg_owned = 42u, events = 0u;
  uint64_t wait_ms = 99u;

  client_config.backend = backend_kind();
  client_config.connection_capacity = 2u;
  client_config.command_capacity = 8u;
  client_config.request_capacity = 8u;
  client_config.completion_batch_capacity = 4u;
  client_config.event_capacity = 8u;
  client_config.max_send_bytes = 1024u;
  client_config.receive_buffer_bytes = 1024u;
  CHECK(cnet_client_init(&client, &client_config) == SALTS_OK);

  manager_config.size = sizeof(manager_config);
  manager_config.version = CNET_MANAGER_VERSION;
  manager_config.client = &client;
  manager_config.record_capacity = 2u;
  manager_config.connection_capacity = 2u;
  CHECK(cnet_manager_init(&manager, &manager_config) == SALTS_OK);

  pool_config.size = sizeof(pool_config);
  pool_config.version = CNET_CLIENT_POOL_VERSION;
  pool_config.manager = &manager;
  pool_config.owner_id = 11u;
  pool_config.max_connections = 1u;
  pool_config.max_connecting = 1u;
  pool_config.max_leases = 1u;
  CHECK(cnet_pool_init(&pool, &pool_config) == SALTS_OK);
  key.size = sizeof(key);
  key.version = CNET_CLIENT_POOL_VERSION;
  key.runtime_id = 9u;
  key.owner_id = 11u;
  key.endpoint_id = 13u;
  key.authority_id = 17u;
  key.transport_id = 1u;
  key.protocol_id = 3u;
  CHECK(cnet_pool_reserve_connecting(&pool, &key, &physical) == SALTS_OK);
  CHECK(physical.slot != 0u);
  CHECK(cnet_pool_terminal(&pool, physical) == SALTS_OK);
  CHECK(cnet_pool_get_snapshot(&pool, &pool_snapshot) == SALTS_OK);
  CHECK(pool_snapshot.drained);
  CHECK(cnet_pool_destroy(&pool) == SALTS_OK);

  reconnect_config.size = sizeof(reconnect_config);
  reconnect_config.version = CNET_RECOVERY_POLICY_VERSION;
  reconnect_config.max_attempts = 2u;
  reconnect_config.deadline_ms = 1000u;
  reconnect_config.initial_backoff_ms = 5u;
  reconnect_config.maximum_backoff_ms = 20u;
  reconnect_config.jitter_seed = 17u;
  CHECK(cnet_reconnect_init(&reconnect_state, &reconnect_config) == SALTS_OK);
  CHECK(cnet_reconnect_begin(&reconnect_state, 100u, &reconnect_ticket,
                             &wait_ms) == SALTS_OK);
  CHECK(cnet_reconnect_connected(&reconnect_state, reconnect_ticket, 101u) == SALTS_OK);
  CHECK(cnet_reconnect_get_snapshot(&reconnect_state, &reconnect_snapshot) == SALTS_OK);
  CHECK(reconnect_snapshot.awaiting_protocol && !reconnect_snapshot.protocol_ready);
  CHECK(cnet_reconnect_protocol_ready(&reconnect_state,
                                      reconnect_ticket, 102u) == SALTS_OK);
  CHECK(cnet_reconnect_seal(&reconnect_state) == SALTS_OK);

  retry_input.size = sizeof(retry_input);
  retry_input.version = CNET_RECOVERY_POLICY_VERSION;
  retry_input.attempts_used = 1u;
  retry_input.max_attempts = 2u;
  retry_input.deadline_ms = 1000u;
  retry_input.now_ms = 103u;
  CHECK(cnet_retry_evaluate(&retry_input, &retry_result) == SALTS_OK);
  CHECK(!retry_result.allowed && retry_result.reason == CNET_RETRY_DISABLED);

  dial_config.size = sizeof(dial_config);
  dial_config.version = CNET_MANAGED_DIAL_VERSION;
  dial_config.manager = &manager;
  dial_config.client = &client;
  dial_config.connection.uri = "tcp://127.0.0.1:1";
  dial_config.connection.observer.on_state = observed_state;
  dial_config.recovery = reconnect_config;
  dial_config.recovery_episode_ms = 2000u;
  CHECK(cnet_managed_dial_init(&managed_dial, &dial_config) == SALTS_OK);
  CHECK(cnet_managed_dial_seal(&managed_dial) == SALTS_OK);
  CHECK(cnet_managed_dial_destroy(&managed_dial) == SALTS_OK);

  routes.size = sizeof(routes);
  routes.version = CNET_SG_HOST_ROUTING_VERSION;
  CHECK(cnet_sg_host_route_batch(NULL, 0u, &routes,
                                 &accepts, &sg_owned) == SALTS_OK);
  CHECK(accepts == 0u && sg_owned == 0u);
  CHECK(native_io_sharded_context_observe_host(
      NULL, invalid_lease, &ignored_completion, 1u, 0u, &events) == SALTS_EINVAL);
  CHECK(native_io_sharded_context_release_host(
      NULL, invalid_lease) == SALTS_ENOENT);

  CHECK(cnet_manager_destroy(&manager) == SALTS_OK);
  CHECK(cnet_client_stop(&client, 1000u) == SALTS_OK);
  CHECK(cnet_client_destroy(&client) == SALTS_OK);
  puts("Salts installed CNet strategy consumer: PASS");
  return 0;
}
