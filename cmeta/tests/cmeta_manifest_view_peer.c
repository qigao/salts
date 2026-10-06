#include "cmeta_manifest_view_fixture.h"
cmeta_struct(ViewPayload, cmeta_field(int, value));
FunctionDecl(value, int, view_transform, (int, value, CMETA_PARAM_IN));
#define VIEW_METHODS(X,I) X(I,FV0,void,reset,stateful,&cmeta_type_void,CMETA_ABI_VOID)
CMETA_INTERFACE(ViewService, VIEW_METHODS);

cmeta_registry(view_manifest,
    cmeta_manifest_type_entry("int", &cmeta_type_int)
    cmeta_manifest_struct_entry("payload", StructMeta(ViewPayload))
    cmeta_manifest_function_entry("transform", FunctionAbi(view_transform))
    cmeta_manifest_interface_entry("service", &ViewService_interface_meta)
    cmeta_manifest_trace_entry("event", StructMeta(ViewPayload))
    cmeta_manifest_capability_entry("capability", &ViewService_interface_meta)
);
const cmeta_manifest *cmeta_view_fixture_manifest(void) { return &view_manifest; }
