/* Executed as C11 and C++17. Crosses a second DSO but keeps the same Owner. */
#include "probe.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while(0)

static native_io_backend_kind backend(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__)
  return NATIVE_IO_BACKEND_KQUEUE;
#else
  return NATIVE_IO_BACKEND_EPOLL;
#endif
}
static void on_state(void *u, cnet_connection c, cnet_connection_state state,
                     const cnet_error *e) {
  (void)u; (void)c; (void)state; (void)e;
}
static void on_recycle(void *u) {
  unsigned *counter = (unsigned *)u;
  ++*counter;
}
int main(void) {
  cnet_client client;
  cnet_client_config cfg;
  cnet_manager mgr;
  cnet_manager_config mc;
  cnet_manager_attachment attachment;
  cnet_managed_connection old_id, new_id;
  cnet_manager_entry entry;
  cnet_manager_snapshot snap;
  cnet_handoff handoff;
  cnet_handoff_config hc;
  cnet_handoff_ticket old_ticket, new_ticket;
  cnet_handoff_snapshot h_snap;
  cnet_client_pool pool;
  cnet_pool_config pool_cfg;
  cnet_pool_key pool_key;
  cnet_pool_connection pool_connection;
  cnet_reconnect_config recovery_config;
  cnet_reconnect_state recovery;
  cnet_reconnect_ticket attempt;
  cnet_reconnect_snapshot recovery_snapshot;
  cnet_managed_dial managed_dial;
  cnet_managed_dial_config dial_config;
  cnet_sg_host_routes sg_routes;
  size_t accepts = 99u, sg_own = 99u;
  uint64_t wait_ms = 0u;
  size_t work = 0;
  unsigned recycled = 0;
  memset(&client,0,sizeof(client)); memset(&cfg,0,sizeof(cfg));
  memset(&mgr,0,sizeof(mgr)); memset(&mc,0,sizeof(mc));
  memset(&attachment,0,sizeof(attachment));
  memset(&handoff,0,sizeof(handoff)); memset(&hc,0,sizeof(hc));
  memset(&pool,0,sizeof(pool)); memset(&pool_cfg,0,sizeof(pool_cfg));
  memset(&pool_key,0,sizeof(pool_key)); memset(&pool_connection,0,sizeof(pool_connection));
  memset(&recovery_config,0,sizeof(recovery_config));
  memset(&recovery,0,sizeof(recovery)); memset(&attempt,0,sizeof(attempt));
  memset(&recovery_snapshot,0,sizeof(recovery_snapshot));
  memset(&managed_dial,0,sizeof(managed_dial)); memset(&dial_config,0,sizeof(dial_config));
  memset(&sg_routes,0,sizeof(sg_routes));
  cfg.backend = backend();
  cfg.connection_capacity = 1; cfg.command_capacity = 8;
  cfg.request_capacity = 8; cfg.completion_batch_capacity = 4;
  cfg.event_capacity = 8; cfg.max_send_bytes = 1024;
  cfg.receive_buffer_bytes = 1024;
  CHECK(cnet_client_init(&client, &cfg) == SALTS_OK);
  mc.size = sizeof(mc); mc.version = CNET_MANAGER_VERSION;
  mc.client = &client; mc.record_capacity = 1; mc.connection_capacity = 1;
  attachment.observer.on_state = on_state;
  attachment.observer.user = &recycled;
  attachment.on_recycle = on_recycle;
  attachment.hold_context = true;
  CHECK(cnet_manager_init(&mgr,&mc) == SALTS_OK);
  CHECK(cnet_manager_reserve(&mgr,&attachment,&old_id) == SALTS_OK);
  CHECK(cnet_manager_cancel(&mgr,old_id) == SALTS_OK);
  CHECK(cnet_manager_advance(&mgr,1,&work) == SALTS_OK && work == 0);
  CHECK(cnet_dso_manager_release(&mgr,old_id) == SALTS_OK);
  CHECK(cnet_dso_manager_release(&mgr,old_id) == SALTS_EALREADY);
  CHECK(cnet_manager_advance(&mgr,1,&work) == SALTS_OK && work == 1);
  CHECK(recycled == 1);
  CHECK(cnet_manager_destroy(&mgr) == SALTS_OK);
  CHECK(cnet_dso_manager_init(&mgr,&mc) == SALTS_OK);

  /* Connection/lease identity survives a DSO boundary, never substitutes the
   * physical Manager's terminal callback for protocol readiness. */
  pool_cfg.size = sizeof(pool_cfg);
  pool_cfg.version = CNET_CLIENT_POOL_VERSION;
  pool_cfg.manager = &mgr;
  pool_cfg.owner_id = 11u;
  pool_cfg.max_connections = 1u;
  pool_cfg.max_connecting = 1u;
  pool_cfg.max_leases = 1u;
  CHECK(cnet_dso_pool_init(&pool,&pool_cfg) == SALTS_OK);
  pool_key.size = sizeof(pool_key);
  pool_key.version = CNET_CLIENT_POOL_VERSION;
  pool_key.runtime_id = 7u;
  pool_key.owner_id = pool_cfg.owner_id;
  pool_key.endpoint_id = 13u;
  pool_key.authority_id = 17u;
  pool_key.transport_id = 1u;
  pool_key.protocol_id = 3u;
  CHECK(cnet_pool_reserve_connecting(&pool,&pool_key,&pool_connection) == SALTS_OK);
  CHECK(cnet_dso_pool_terminal(&pool,pool_connection) == SALTS_OK);
  CHECK(cnet_pool_terminal(&pool,pool_connection) == SALTS_ENOENT);
  CHECK(cnet_pool_destroy(&pool) == SALTS_OK);

  /* Attempt tickets created in the CNet DSO must validate identically when
   * passed through another DSO. Transport CONNECTED is not protocol READY. */
  recovery_config.size = sizeof(recovery_config);
  recovery_config.version = CNET_RECOVERY_POLICY_VERSION;
  recovery_config.max_attempts = 2u;
  recovery_config.deadline_ms = 1000u;
  recovery_config.initial_backoff_ms = 5u;
  recovery_config.maximum_backoff_ms = 20u;
  recovery_config.jitter_seed = 7u;
  CHECK(cnet_dso_reconnect_init(&recovery,&recovery_config) == SALTS_OK);
  CHECK(cnet_reconnect_begin(&recovery,100u,&attempt,&wait_ms) == SALTS_OK);
  CHECK(cnet_dso_reconnect_connected(&recovery,attempt,101u) == SALTS_OK);
  CHECK(cnet_reconnect_get_snapshot(&recovery,&recovery_snapshot) == SALTS_OK);
  CHECK(recovery_snapshot.awaiting_protocol && !recovery_snapshot.protocol_ready);
  CHECK(cnet_reconnect_protocol_ready(&recovery,attempt,102u) == SALTS_OK);
  CHECK(cnet_reconnect_get_snapshot(&recovery,&recovery_snapshot) == SALTS_OK);
  CHECK(recovery_snapshot.protocol_ready);
  CHECK(cnet_reconnect_seal(&recovery) == SALTS_OK);

  /* Owner-driven dial can be created through another DSO without admitting
   * a network connection or creating a hidden timer/worker. */
  dial_config.size = sizeof(dial_config);
  dial_config.version = CNET_MANAGED_DIAL_VERSION;
  dial_config.manager = &mgr;
  dial_config.client = &client;
  dial_config.connection.uri = "tcp://127.0.0.1:1";
  dial_config.connection.observer.on_state = on_state;
  dial_config.recovery = recovery_config;
  dial_config.recovery_episode_ms = 500u;
  CHECK(cnet_dso_managed_dial_init(&managed_dial,&dial_config) == SALTS_OK);
  CHECK(cnet_managed_dial_seal(&managed_dial) == SALTS_OK);
  CHECK(cnet_dso_managed_dial_destroy(&managed_dial) == SALTS_OK);

  /* A zero-completion mixed-SG route is also a typed cross-DSO symbol. */
  sg_routes.size = sizeof(sg_routes);
  sg_routes.version = CNET_SG_HOST_ROUTING_VERSION;
  CHECK(cnet_dso_sg_route_empty(&sg_routes,&accepts,&sg_own) == SALTS_OK);
  CHECK(accepts == 0u && sg_own == 0u);

  CHECK(cnet_manager_reserve(&mgr,&attachment,&new_id) == SALTS_OK);
  CHECK(old_id.manager == new_id.manager && old_id.slot == new_id.slot);
  CHECK(old_id.generation == new_id.generation);
  CHECK(old_id.incarnation != new_id.incarnation);
  CHECK(cnet_manager_lookup(&mgr,old_id,&entry) == SALTS_ENOENT);
  CHECK(cnet_manager_cancel(&mgr,new_id) == SALTS_OK);
  CHECK(cnet_manager_release_context(&mgr,new_id) == SALTS_OK);
  CHECK(cnet_manager_advance(&mgr,1,&work) == SALTS_OK && work == 1);
  CHECK(cnet_manager_get_snapshot(&mgr,&snap) == SALTS_OK && snap.drained);
  CHECK(cnet_manager_destroy(&mgr) == SALTS_OK);
  hc.size = sizeof(hc); hc.version = CNET_HANDOFF_VERSION;
  hc.connection_capacity = 1; hc.queue_capacity = 1;
  CHECK(cnet_handoff_init(&handoff,&hc) == SALTS_OK);
  CHECK(cnet_handoff_reserve(&handoff,&old_ticket) == SALTS_OK);
  CHECK(cnet_handoff_release(&handoff,old_ticket) == SALTS_OK);
  CHECK(cnet_handoff_seal(&handoff) == SALTS_OK);
  CHECK(cnet_handoff_destroy(&handoff) == SALTS_OK);
  CHECK(cnet_dso_handoff_init(&handoff,&hc) == SALTS_OK);
  CHECK(cnet_handoff_reserve(&handoff,&new_ticket) == SALTS_OK);
  CHECK(old_ticket.incarnation != new_ticket.incarnation);
  CHECK(cnet_handoff_release(&handoff,old_ticket) == SALTS_ENOENT);
  CHECK(cnet_dso_handoff_release(&handoff,new_ticket) == SALTS_OK);
  CHECK(cnet_dso_handoff_release(&handoff,new_ticket) == SALTS_ENOENT);
  CHECK(cnet_handoff_seal(&handoff) == SALTS_OK);
  CHECK(cnet_handoff_get_snapshot(&handoff,&h_snap) == SALTS_OK && h_snap.drained);
  CHECK(cnet_handoff_destroy(&handoff) == SALTS_OK);
  CHECK(cnet_client_stop(&client,1000) == SALTS_OK);
  CHECK(cnet_client_destroy(&client) == SALTS_OK);
  puts("CNet manager/handoff/pool/recovery/dial/SG host single-DSO identity: PASS");
  return 0;
}
