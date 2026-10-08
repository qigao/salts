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
