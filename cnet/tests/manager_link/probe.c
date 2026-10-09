#include "probe.h"
int cnet_dso_manager_init(cnet_manager *m, const cnet_manager_config *c) {
  return cnet_manager_init(m, c);
}
int cnet_dso_manager_release(cnet_manager *m, cnet_managed_connection h) {
  return cnet_manager_release_context(m, h);
}
int cnet_dso_handoff_init(cnet_handoff *h, const cnet_handoff_config *c) {
  return cnet_handoff_init(h, c);
}
int cnet_dso_handoff_release(cnet_handoff *h, cnet_handoff_ticket t) {
  return cnet_handoff_release(h, t);
}

/* ABI linkage of new optional client/runtime surfaces across companion DSO. */
int cnet_dso_pool_init(cnet_client_pool *p, const cnet_pool_config *c) {
  return cnet_pool_init(p, c);
}
int cnet_dso_pool_terminal(cnet_client_pool *p, cnet_pool_connection h) {
  return cnet_pool_terminal(p, h);
}
int cnet_dso_reconnect_init(cnet_reconnect_state *s, const cnet_reconnect_config *c) {
  return cnet_reconnect_init(s, c);
}
int cnet_dso_reconnect_connected(cnet_reconnect_state *s,
                                 cnet_reconnect_ticket t, uint64_t now_ms) {
  return cnet_reconnect_connected(s, t, now_ms);
}
int cnet_dso_managed_dial_init(cnet_managed_dial *d, const cnet_managed_dial_config *c) {
  return cnet_managed_dial_init(d, c);
}
int cnet_dso_managed_dial_destroy(cnet_managed_dial *d) {
  return cnet_managed_dial_destroy(d);
}
int cnet_dso_sg_route_empty(const cnet_sg_host_routes *r,
                            size_t *out_accepts, size_t *out_sharded) {
  return cnet_sg_host_route_batch(NULL, 0u, r, out_accepts, out_sharded);
}
