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
  size_t work = 0;
  unsigned recycled = 0;
  memset(&client,0,sizeof(client)); memset(&cfg,0,sizeof(cfg));
  memset(&mgr,0,sizeof(mgr)); memset(&mc,0,sizeof(mc));
  memset(&attachment,0,sizeof(attachment));
  memset(&handoff,0,sizeof(handoff)); memset(&hc,0,sizeof(hc));
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
  puts("CNet manager/handoff single-DSO identity: PASS");
  return 0;
}
