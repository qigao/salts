#ifndef PLUGIN_MANIFEST_FIXTURE_H
#define PLUGIN_MANIFEST_FIXTURE_H

/* One canonical test schema, shared with Core fingerprint qualification. */
#include "../../cmeta/tests/cmeta_plugin_fixture.h"
#include <salts/plugin.h>

#define PLUGIN_MANIFEST_EXPORT_ID "declaration"
#define PLUGIN_MANIFEST_CONTRACT_ID "test.plugin.manifest.v1"
#define PLUGIN_SERVICE_EXPORT_ID "service"
#define PLUGIN_SERVICE_CONTRACT_ID "test.plugin.service.v1"
enum { PLUGIN_MANIFEST_CONTRACT_VERSION = 1u, PLUGIN_SERVICE_CONTRACT_VERSION = 1u };

static const cmeta_type_identity plugin_manifest_atom = CMETA_TYPE_ID_ATOM_INIT("cmeta.manifest.v1");
static const cmeta_type_identity plugin_manifest_const = CMETA_TYPE_ID_CONST_INIT(&plugin_manifest_atom);
static const cmeta_type_identity plugin_manifest_pointer = CMETA_TYPE_ID_POINTER_INIT(&plugin_manifest_const);
static const cmeta_type_desc plugin_manifest_type = {
    "const cmeta_manifest", sizeof(cmeta_manifest), CMETA_ALIGNOF(cmeta_manifest),
    CMETA_T_OBJECT, NULL, NULL, &plugin_manifest_const
};
static const cmeta_type_desc plugin_manifest_pointer_type = {
    "const cmeta_manifest *", sizeof(const cmeta_manifest *), CMETA_ALIGNOF(const cmeta_manifest *),
    CMETA_T_POINTER, &plugin_manifest_type, NULL, &plugin_manifest_pointer
};
CMETA_FUNCTION0_METADATA_AS_ABI_RESULT(plugin_manifest_query, "declaration", pure,
    &plugin_manifest_pointer_type, CMETA_ABI_OBJECT_POINTER, CMETA_RESULT_BORROWED);

#ifdef __cplusplus
extern "C" {
#endif
const cmeta_manifest *plugin_fixture_discovery(void);
#ifdef __cplusplus
}
#endif
#endif
