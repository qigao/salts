#include "cmeta_plugin_fixture.h"
const cmeta_manifest *cmeta_plugin_peer_manifest(void) { return &plugin_discovery; }
cmeta_status cmeta_plugin_peer_fingerprint(uint64_t *out) {
    return cmeta_contract_fingerprint_plugin(cmeta_plugin_meta(FingerprintProvider),
        &fingerprint_limits, out);
}
