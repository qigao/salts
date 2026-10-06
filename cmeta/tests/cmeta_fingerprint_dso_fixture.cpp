#include "cmeta_fingerprint_fixture.h"

const cmeta_fingerprint_fixture *cmeta_fingerprint_dso_query(uint32_t reflection_epoch) {
    return reflection_epoch == CMETA_REFLECTION_ABI_VERSION ? &fingerprint_fixture : nullptr;
}
cmeta_status cmeta_fingerprint_dso_values(uint32_t reflection_epoch, uint64_t out[FP_COUNT]) {
    if (reflection_epoch != CMETA_REFLECTION_ABI_VERSION) return CMETA_TYPE_MISMATCH;
    return cmeta_fingerprint_peer_values(out);
}
