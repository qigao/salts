#include "cmeta_trace_fixture.h"
SALTS_FAST_KEY(cmeta_shared_fault, false);
cmeta_tracepoint(peer_request, cmeta_field(uint64_t, request_id) cmeta_field(int, status));
static uint64_t peer_last_id;
static void peer_backend(const peer_request_payload *event) {
    peer_last_id = event->request_id;
}
void cmeta_trace_fixture_emit(uint64_t request_id, int status) {
    cmeta_trace_emit(peer_request, request_id, status);
}
cmeta_status cmeta_trace_fixture_enable(void) {
    cmeta_status status = cmeta_trace_bind(peer_request, peer_backend);
    return status == CMETA_OK ? cmeta_trace_enable(peer_request) : status;
}
cmeta_status cmeta_trace_fixture_disable(void) { return cmeta_trace_disable(peer_request); }
const cmeta_struct_desc *cmeta_trace_fixture_meta(void) { return StructMeta(peer_request_payload); }
uint64_t cmeta_trace_fixture_last_id(void) { return peer_last_id; }
