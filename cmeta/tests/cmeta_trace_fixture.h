#ifndef CMETA_TRACE_FIXTURE_H
#define CMETA_TRACE_FIXTURE_H
#include <cmeta/trace.h>
#ifdef __cplusplus
extern "C" {
#endif
extern salts_static_key_state cmeta_shared_fault;
void cmeta_trace_fixture_emit(uint64_t request_id, int status);
cmeta_status cmeta_trace_fixture_enable(void);
cmeta_status cmeta_trace_fixture_disable(void);
const cmeta_struct_desc *cmeta_trace_fixture_meta(void);
uint64_t cmeta_trace_fixture_last_id(void);
#ifdef __cplusplus
}
#endif
#endif
