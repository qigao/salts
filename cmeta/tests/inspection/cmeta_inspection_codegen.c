#include "cmeta_inspection_metadata.h"

#include <cmeta/struct.h>

#include <stdio.h>
#include <string.h>

static int inspection_contract_valid(
    const cmeta_data_desc *data,
    const cmeta_declared_type *declared,
    const cmeta_function_desc *function,
    const cmeta_function_abi_desc *abi,
    const cmeta_field_desc **out_field) {
    const cmeta_data_struct_shape *shape;
    const cmeta_field_desc *field;
    const cmeta_param_desc *param;
    cmeta_generic_desc constructor_clone;
    cmeta_type_desc argument_clone;
    const cmeta_type_desc *peer_arguments[1];
    cmeta_declared_type peer;

    if (out_field == NULL)
        return 0;
    *out_field = NULL;

    if (!cmeta_data_desc_valid(data) ||
        data->kind != CMETA_DATA_STRUCT ||
        data->storage_type == NULL ||
        data->shape == NULL ||
        !cmeta_declared_type_valid(declared) ||
        !cmeta_type_equal(data->storage_type, declared->storage_type) ||
        declared->constructor == NULL ||
        declared->arity != 1u)
        return 0;

    shape = (const cmeta_data_struct_shape *)data->shape;
    if (shape->layout == NULL)
        return 0;
    field = cmeta_struct_find_field(shape->layout, "number");
    if (field == NULL ||
        !cmeta_type_equal(field->type, &cmeta_type_int))
        return 0;

    if (!cmeta_type_equal(
            cmeta_declared_type_argument(declared, 0u),
            &cmeta_type_int))
        return 0;

    /*
     * Cross-address semantic witness: storage representation and optional
     * construction capability are separate from the generic application.
     */
    constructor_clone = *declared->constructor;
    argument_clone = *cmeta_declared_type_argument(declared, 0u);
    peer_arguments[0] = &argument_clone;
    peer = *declared;
    peer.storage_type = &cmeta_type_long;
    peer.constructor = &constructor_clone;
    peer.arguments = peer_arguments;
    peer.construction = NULL;
    if (!cmeta_declared_type_valid(&peer) ||
        !cmeta_declared_type_application_equal(declared, &peer) ||
        cmeta_type_equal(declared->storage_type, peer.storage_type))
        return 0;

    if (!cmeta_function_desc_valid(function) ||
        !cmeta_function_abi_desc_valid(abi) ||
        !cmeta_function_desc_equal(function, abi->function) ||
        function->name == NULL ||
        strcmp(function->name, "cmeta_inspection_increment") != 0 ||
        function->param_count != 1u ||
        !cmeta_type_equal(function->return_type, &cmeta_type_int) ||
        (function->result_flags & CMETA_RESULT_CLASS_MASK) !=
            CMETA_RESULT_VALUE ||
        function->effects != CMETA_EFFECT_PURE ||
        !cmeta_properties_include(
            function->properties,
            CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL |
                CMETA_PROP_NO_ALIAS) ||
        abi->return_carrier != CMETA_ABI_SCALAR ||
        cmeta_function_param_abi(abi, 0u) != CMETA_ABI_SCALAR)
        return 0;

    param = cmeta_function_param(function, 0u);
    if (param == NULL ||
        param->name == NULL ||
        strcmp(param->name, "value") != 0 ||
        param->flags != CMETA_PARAM_IN ||
        !cmeta_type_equal(param->type, &cmeta_type_int))
        return 0;

    *out_field = field;
    return 1;
}

static int emit_runtime_source(
    const char *path,
    const cmeta_function_desc *function,
    const cmeta_field_desc *field) {
    FILE *output;

    if (path == NULL || function == NULL || function->name == NULL ||
        field == NULL || field->name == NULL)
        return 0;

    output = fopen(path, "wb");
    if (output == NULL)
        return 0;

    if (fprintf(
            output,
            "#include \"cmeta_inspection_fixture.h\"\n\n"
            "int main(void) {\n"
            "    cmeta_inspection_record value = { .%s = 41 };\n"
            "    return %s(value.%s) == 42 ? 0 : 1;\n"
            "}\n",
            field->name, function->name, field->name) < 0) {
        fclose(output);
        return 0;
    }

    return fclose(output) == 0;
}

int main(int argc, char **argv) {
    const cmeta_data_desc *data = cmeta_inspection_record_data();
    const cmeta_declared_type *declared =
        cmeta_inspection_declared_type();
    const cmeta_function_desc *function =
        cmeta_inspection_function();
    const cmeta_function_abi_desc *abi =
        cmeta_inspection_function_abi();
    const cmeta_field_desc *field = NULL;
    int mismatch = 0;
    const char *output_path;

    if (argc == 3 && strcmp(argv[1], "--mismatch") == 0) {
        mismatch = 1;
        output_path = argv[2];
    } else if (argc == 2) {
        output_path = argv[1];
    } else {
        fprintf(stderr, "usage: %s [--mismatch] OUTPUT\n", argv[0]);
        return 2;
    }

    if (mismatch) {
        cmeta_function_desc bad;

        if (function == NULL) {
            fprintf(stderr, "inspection metadata missing\n");
            return 3;
        }
        bad = *function;
        bad.return_type = &cmeta_type_long;
        if (inspection_contract_valid(
                data, declared, &bad, abi, &field)) {
            fprintf(stderr, "mismatched inspection metadata was admitted\n");
            return 4;
        }
        (void)remove(output_path);
        fprintf(stderr, "inspection metadata rejected as expected\n");
        return 1;
    }

    if (!inspection_contract_valid(
            data, declared, function, abi, &field)) {
        fprintf(stderr, "inspection metadata rejected\n");
        return 5;
    }
    if (!emit_runtime_source(output_path, function, field)) {
        fprintf(stderr, "failed to emit inspection runtime source\n");
        return 6;
    }
    return 0;
}
