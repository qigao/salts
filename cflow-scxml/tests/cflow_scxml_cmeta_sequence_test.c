#include "cmeta_sequence.h"

#include <turbostl/typed.h>
#include "tinytest.h"

#include <stddef.h>
#include <string.h>

Struct(scxml_sequence_root,
    (TYPE(Vec, int), values),
    (TYPE(Set, int), members),
    (int, item)
);

static const cmeta_type_desc sequence_root_type = {
    .name = "scxml_sequence_root",
    .size = sizeof(scxml_sequence_root),
    .align = _Alignof(scxml_sequence_root),
    .kind = CMETA_T_OBJECT
};

static const cmeta_data_field_desc sequence_root_fields[] = {
    {"test.scxml.sequence.values", "values",
     offsetof(scxml_sequence_root, values), &cmeta_data_sequence},
    {"test.scxml.sequence.members", "members",
     offsetof(scxml_sequence_root, members), &cmeta_data_set},
    {"test.scxml.sequence.item", "item",
     offsetof(scxml_sequence_root, item), &cmeta_data_int}
};

static const cmeta_data_struct_shape sequence_root_shape = {
    .layout = StructMeta(scxml_sequence_root),
    .fields = sequence_root_fields,
    .field_count = sizeof(sequence_root_fields) /
                   sizeof(sequence_root_fields[0])
};

static const cmeta_data_desc sequence_root_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.sequence.root",
    .display_name = "SCXML sequence root",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &sequence_root_type,
    .shape = &sequence_root_shape
};

static const cmeta_generic_desc incomplete_sequence_generic =
    CMETA_GENERIC_DESC_INIT("test.scxml.IncompleteSequence",
                            "Incomplete sequence", 1u, 1u,
                            CMETA_GENERIC_CONTAINER);

static const cmeta_type_desc *incomplete_sequence_argument(
    const void *object, size_t index) {
    const vec_t *values = (const vec_t *)object;
    return values != NULL && index == 0u ? values->element_type : NULL;
}

static const cmeta_container_type_ops incomplete_sequence_type_ops = {
    .struct_size = sizeof(cmeta_container_type_ops),
    .abi_version = CMETA_CONTAINER_TYPE_OPS_ABI_VERSION,
    .constructor = &incomplete_sequence_generic,
    .arity = 1u,
    .argument = incomplete_sequence_argument
};

static size_t incomplete_sequence_size(const void *object) {
    return vec_size((const vec_t *)object);
}

static cmeta_gen_status incomplete_sequence_next(
    const void *object, cmeta_range_cursor *cursor, void *out_value) {
    const vec_t *values = (const vec_t *)object;
    const int *value;
    if (values == NULL || cursor == NULL || out_value == NULL)
        return CMETA_GEN_ERROR;
    if (cursor->index >= vec_size(values)) return CMETA_GEN_DONE;
    value = (const int *)vec_at_const(values, cursor->index);
    if (value == NULL) return CMETA_GEN_ERROR;
    memcpy(out_value, value, sizeof(*value));
    ++cursor->index;
    return cursor->index == vec_size(values)
               ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE;
}

static cmeta_range_flags incomplete_sequence_flags;

static cmeta_range incomplete_sequence_range(const void *object) {
    const vec_t *values = (const vec_t *)object;
    return (cmeta_range){
        object, values != NULL ? values->element_type : NULL,
        incomplete_sequence_flags, incomplete_sequence_size,
        incomplete_sequence_next, 0u, NULL};
}

static const cmeta_container_ext incomplete_sequence_ext = {
    .struct_size = offsetof(cmeta_container_ext, data) +
                   sizeof(((cmeta_container_ext *)0)->data),
    .abi_version = CMETA_CONTAINER_EXT_ABI_VERSION,
    .type = &incomplete_sequence_type_ops,
    .data = &cmeta_data_sequence
};

static const cmeta_container_desc incomplete_sequence_desc = {
    .name = "incomplete sequence",
    .range = incomplete_sequence_range,
    .ext = &incomplete_sequence_ext
};

static cflow_scxml_cmeta_expr_status compile_sequence(
    cflow_scxml_cmeta_sequence_program *program, const char *location) {
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    return cflow_scxml_cmeta_sequence_compile(
        program, location, strlen(location), &sequence_root_data, 8u,
        &diagnostic);
}

spec("CFlow SCXML CMeta reflected sequence bridge") {
  it("opens one declared Vec as a sized ordered borrowed Range") {
    const int first = 17;
    const int second = 29;
    scxml_sequence_root root = {.values = VecOf(int)};
    cflow_scxml_cmeta_sequence_program program = {0};
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    cmeta_range range = {0};
    cmeta_range_cursor cursor = {0};
    size_t length = 0u;
    int value = 0;

    check_equal(vec_init(&root.values, 2u), STL_OK);
    check_equal(vec_push(&root.values, &first), STL_OK);
    check_equal(vec_push(&root.values, &second), STL_OK);
    check_equal(compile_sequence(&program, "values"),
                CFLOW_SCXML_CMETA_EXPR_OK);
    check_equal(cflow_scxml_cmeta_sequence_open(
                    &program, &root, &range, &length, &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_OK);
    check_equal(length, (size_t)2u);
    check_equal(cmeta_range_next(&range, &cursor, &value), CMETA_GEN_VALUE);
    check_equal(value, first);
    check_equal(cmeta_range_next(&range, &cursor, &value),
                CMETA_GEN_VALUE_AND_DONE);
    check_equal(value, second);
    check_equal(cmeta_range_next(&range, &cursor, &value), CMETA_GEN_DONE);

    vec_destroy(&root.values);
  }

  it("rejects malformed unresolved scalar and non-sequence locations") {
    static const char *const invalid[] = {
        "", ".values", "values.", "missing", "item", "members"};
    static const cflow_scxml_cmeta_expr_status expected[] = {
        CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT,
        CFLOW_SCXML_CMETA_EXPR_SYNTAX_ERROR,
        CFLOW_SCXML_CMETA_EXPR_SYNTAX_ERROR,
        CFLOW_SCXML_CMETA_EXPR_UNKNOWN_LOCATION,
        CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH,
        CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH};
    size_t index;

    for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        cflow_scxml_cmeta_sequence_program program = {0};
        cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
        const cflow_scxml_cmeta_expr_status status =
            cflow_scxml_cmeta_sequence_compile(
                &program, invalid[index], strlen(invalid[index]),
                &sequence_root_data, 8u, &diagnostic);
        info("location=%s", invalid[index]);
        check_equal(status, expected[index]);
        check_null(program.root);
    }
  }

  it("rejects unbound or element-mismatched runtime handles transactionally") {
    scxml_sequence_root root = {0};
    cflow_scxml_cmeta_sequence_program program = {0};
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    cmeta_range range = {.object = &root};
    size_t length = 41u;

    check_equal(compile_sequence(&program, "values"),
                CFLOW_SCXML_CMETA_EXPR_OK);
    check_equal(cflow_scxml_cmeta_sequence_open(
                    &program, &root, &range, &length, &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH);
    check_true(range.object == &root);
    check_equal(length, (size_t)41u);

    root.values = VecOf(long);
    check_equal(vec_init(&root.values, 1u), STL_OK);
    check_equal(cflow_scxml_cmeta_sequence_open(
                    &program, &root, &range, &length, &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH);
    check_true(range.object == &root);
    check_equal(length, (size_t)41u);
    vec_destroy(&root.values);
  }

  it("rejects sequence Ranges without sized and ordered capabilities") {
    scxml_sequence_root root = {.values = VecOf(int)};
    cflow_scxml_cmeta_sequence_program program = {0};
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    cmeta_range range = {.object = &root};
    size_t length = 73u;

    check_equal(vec_init(&root.values, 1u), STL_OK);
    root.values.cmeta.descriptor = &incomplete_sequence_desc;
    check_equal(compile_sequence(&program, "values"),
                CFLOW_SCXML_CMETA_EXPR_OK);

    incomplete_sequence_flags = CMETA_RANGE_SIZED;
    check_equal(cflow_scxml_cmeta_sequence_open(
                    &program, &root, &range, &length, &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH);
    check_true(range.object == &root);
    check_equal(length, (size_t)73u);

    incomplete_sequence_flags = CMETA_RANGE_ORDERED;
    check_equal(cflow_scxml_cmeta_sequence_open(
                    &program, &root, &range, &length, &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH);
    check_true(range.object == &root);
    check_equal(length, (size_t)73u);

    vec_destroy(&root.values);
  }
}
