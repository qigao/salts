#ifndef CMETA_PLUGIN_FIXTURE_H
#define CMETA_PLUGIN_FIXTURE_H
#include "cmeta_fingerprint_fixture.h"

cmeta_plugin(FingerprintProvider,
    cmeta_provides(FingerprintService)
    cmeta_requires(FingerprintService));
cmeta_registry(plugin_discovery,
    cmeta_manifest_plugin_entry("provider", cmeta_plugin_meta(FingerprintProvider)));
static const cmeta_manifest_limits plugin_view_limits = {
    CMETA_MANIFEST_DEFAULT_ITEMS, CMETA_MANIFEST_DEFAULT_DEPTH, CMETA_MANIFEST_DEFAULT_NODES
};
#define CMETA_PLUGIN_FIXTURE_GOLDEN UINT64_C(0x5aa1c2c29cbe9f20)
#ifdef __cplusplus
extern "C" {
#endif
const cmeta_manifest *cmeta_plugin_peer_manifest(void);
cmeta_status cmeta_plugin_peer_fingerprint(uint64_t *out);
#ifdef __cplusplus
}
#endif
#endif
