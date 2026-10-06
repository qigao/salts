#include "cmeta_fingerprint_fixture.h"

const cmeta_fingerprint_fixture *cmeta_fingerprint_peer(void) { return &fingerprint_fixture; }
cmeta_status cmeta_fingerprint_peer_values(uint64_t out[FP_COUNT]) {
    if (out == NULL) return CMETA_INVALID_ARGUMENT;
    cmeta_status status = cmeta_contract_fingerprint_type(fingerprint_fixture.type,
        &fingerprint_limits, &out[FP_TYPE]);
    if (status != CMETA_OK) return status;
    status = cmeta_contract_fingerprint_struct(fingerprint_fixture.structure,
        &fingerprint_limits, &out[FP_STRUCT]);
    if (status != CMETA_OK) return status;
    status = cmeta_contract_fingerprint_enum(fingerprint_fixture.enumeration,
        &fingerprint_limits, &out[FP_ENUM]);
    if (status != CMETA_OK) return status;
    status = cmeta_contract_fingerprint_function(fingerprint_fixture.function,
        &fingerprint_limits, &out[FP_FUNCTION]);
    if (status != CMETA_OK) return status;
    return cmeta_contract_fingerprint_interface(fingerprint_fixture.interface_desc,
        &fingerprint_limits, &out[FP_INTERFACE]);
}
