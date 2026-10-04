#include "cmeta_inspection_metadata.h"

#include <cmeta/struct.h>
#include <cmeta/type_identity.h>

#include <stddef.h>

static const cmeta_type_identity cmeta_inspection_record_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.cmeta.inspection.record");

static const cmeta_type_desc cmeta_inspection_record_type = {
    .name = "cmeta_inspection_record",
    .size = sizeof(cmeta_inspection_record),
    .align = _Alignof(cmeta_inspection_record),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = &cmeta_inspection_record_identity
};

static const cmeta_generic_desc cmeta_inspection_box_generic =
    CMETA_GENERIC_DESC_INIT(
        "test.cmeta.inspection.Box", "InspectionBox", 1u, 1u,
        CMETA_GENERIC_VALUE);

static const cmeta_type_desc *const cmeta_inspection_box_arguments[] = {
    &cmeta_type_int
};

static const cmeta_declared_type cmeta_inspection_box_declared = {
    .storage_type = &cmeta_inspection_record_type,
    .constructor = &cmeta_inspection_box_generic,
    .arguments = cmeta_inspection_box_arguments,
    .arity = 1u,
    .construction = NULL
};

static const cmeta_field_desc cmeta_inspection_record_fields[] = {
    {
        .name = "number",
        .type_name = "int",
        .offset = offsetof(cmeta_inspection_record, number),
        .size = sizeof(int),
        .align = _Alignof(int),
        .type = &cmeta_type_int,
        .declared_type = NULL
    }
};

static const cmeta_struct_desc cmeta_inspection_record_layout = {
    .name = "cmeta_inspection_record",
    .size = sizeof(cmeta_inspection_record),
    .align = _Alignof(cmeta_inspection_record),
    .fields = cmeta_inspection_record_fields,
    .field_count = 1u
};

static const cmeta_data_field_desc cmeta_inspection_record_data_fields[] = {
    {
        .stable_id = "test.cmeta.inspection.record.number",
        .name = "number",
        .offset = offsetof(cmeta_inspection_record, number),
        .value = &cmeta_data_int
    }
};

static const cmeta_data_struct_shape cmeta_inspection_record_shape = {
    .layout = &cmeta_inspection_record_layout,
    .fields = cmeta_inspection_record_data_fields,
    .field_count = 1u
};

static const cmeta_data_desc cmeta_inspection_record_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cmeta.inspection.record.data",
    .display_name = "cmeta_inspection_record",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cmeta_inspection_record_type,
    .shape = &cmeta_inspection_record_shape
};

FunctionDeclAsAbiResult(
    value, int, &cmeta_type_int, CMETA_ABI_SCALAR, CMETA_RESULT_VALUE,
    cmeta_inspection_increment,
    (int, value, CMETA_PARAM_IN, &cmeta_type_int, CMETA_ABI_SCALAR));

const cmeta_data_desc *cmeta_inspection_record_data(void) {
    return &cmeta_inspection_record_desc;
}

const cmeta_declared_type *cmeta_inspection_declared_type(void) {
    return &cmeta_inspection_box_declared;
}

const cmeta_function_desc *cmeta_inspection_function(void) {
    return FunctionMeta(cmeta_inspection_increment);
}

const cmeta_function_abi_desc *cmeta_inspection_function_abi(void) {
    return FunctionAbi(cmeta_inspection_increment);
}
