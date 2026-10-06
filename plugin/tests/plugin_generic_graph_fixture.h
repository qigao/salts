#ifndef CMETA_PLUGIN_GENERIC_GRAPH_FIXTURE_H
#define CMETA_PLUGIN_GENERIC_GRAPH_FIXTURE_H

#include <cmeta/invoke_decl.h>

typedef struct plugin_generic_graph_value {
    int value;
} plugin_generic_graph_value;

/*
 * These descriptors are intentionally TU-local. The host test and provider DSO
 * therefore own distinct descriptor addresses while publishing the same
 * semantic identities.
 */
static const cmeta_type_identity plugin_generic_graph_arg_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.plugin.GenericArg");

static const cmeta_generic_desc plugin_generic_graph_constructor =
    CMETA_GENERIC_DESC_INIT(
        "test.plugin.GenericProbe", "GenericProbe",
        1u, 1u, CMETA_GENERIC_HANDLE);

static const cmeta_type_identity *const plugin_generic_graph_args[] = {
    &plugin_generic_graph_arg_identity
};

static const cmeta_type_identity plugin_generic_graph_value_identity =
    CMETA_TYPE_ID_APPLY_INIT(
        &plugin_generic_graph_constructor, plugin_generic_graph_args);

static const cmeta_type_desc plugin_generic_graph_value_type = {
    .name = "plugin_generic_graph_value",
    .size = sizeof(plugin_generic_graph_value),
    .align = _Alignof(plugin_generic_graph_value),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = &plugin_generic_graph_value_identity
};

FunctionInvokeDeclAsAbiResult(
    value,
    int,
    &cmeta_type_int,
    CMETA_ABI_SCALAR,
    CMETA_RESULT_VALUE,
    plugin_generic_graph_probe,
    (plugin_generic_graph_value, value, CMETA_PARAM_IN,
     &plugin_generic_graph_value_type, CMETA_ABI_AGGREGATE));

#endif /* CMETA_PLUGIN_GENERIC_GRAPH_FIXTURE_H */
