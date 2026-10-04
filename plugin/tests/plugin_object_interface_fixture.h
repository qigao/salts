#ifndef SALTS_PLUGIN_OBJECT_INTERFACE_FIXTURE_H
#define SALTS_PLUGIN_OBJECT_INTERFACE_FIXTURE_H

#include <cmeta/data.h>
#include <cmeta/function.h>
#include <cmeta/interface.h>

#include <stddef.h>

typedef struct plugin_object_fixture_state {
    int value;
} plugin_object_fixture_state;

static const cmeta_type_desc plugin_object_fixture_state_type = {
    .name = "plugin_object_fixture_state",
    .size = sizeof(plugin_object_fixture_state),
    .align = _Alignof(plugin_object_fixture_state),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_type_desc plugin_object_fixture_state_ptr_type = {
    .name = "plugin_object_fixture_state *",
    .size = sizeof(plugin_object_fixture_state *),
    .align = _Alignof(plugin_object_fixture_state *),
    .kind = CMETA_T_POINTER,
    .pointee = &plugin_object_fixture_state_type,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_field_desc plugin_object_fixture_layout_fields[] = {
    {
        .name = "value",
        .type_name = "int",
        .offset = offsetof(plugin_object_fixture_state, value),
        .size = sizeof(int),
        .align = _Alignof(int),
        .type = &cmeta_type_int,
        .declared_type = NULL
    }
};

static const cmeta_struct_desc plugin_object_fixture_layout = {
    .name = "plugin_object_fixture_state",
    .size = sizeof(plugin_object_fixture_state),
    .align = _Alignof(plugin_object_fixture_state),
    .fields = plugin_object_fixture_layout_fields,
    .field_count = 1u
};

static const cmeta_data_field_desc plugin_object_fixture_data_fields[] = {
    {
        .stable_id = "test.plugin.object.value",
        .name = "value",
        .offset = offsetof(plugin_object_fixture_state, value),
        .value = &cmeta_data_int
    }
};

static const cmeta_data_struct_shape plugin_object_fixture_shape = {
    .layout = &plugin_object_fixture_layout,
    .fields = plugin_object_fixture_data_fields,
    .field_count = 1u
};

static const cmeta_data_desc plugin_object_fixture_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.plugin.object.data",
    .display_name = "plugin_object_fixture_state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &plugin_object_fixture_state_type,
    .shape = &plugin_object_fixture_shape,
    .buffer_ops = NULL,
    .enum_ops = NULL,
    .variant_ops = NULL,
    .fixed_ops = NULL,
    .enum_bits_ops = NULL,
    .collection_ops = NULL,
    .map_ops = NULL,
    .construct_ops = NULL
};

#define PLUGIN_OBJECT_FIXTURE_METHODS(X, I) \
    X(I, R1, int, add, int, delta) \
    X(I, R0, int, value, _)

CMETA_INTERFACE(plugin_object_fixture_api, PLUGIN_OBJECT_FIXTURE_METHODS);

Function0DeclAsAbiResult(
    value,
    plugin_object_fixture_state *,
    &plugin_object_fixture_state_ptr_type,
    CMETA_ABI_OBJECT_POINTER,
    CMETA_RESULT_BORROWED,
    plugin_object_fixture_identity);

#endif /* SALTS_PLUGIN_OBJECT_INTERFACE_FIXTURE_H */
