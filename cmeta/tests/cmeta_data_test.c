#include <cmeta/data.h>
#include "tinytest.h"

#include <stddef.h>

Struct(cmeta_data_test_record,
    (int, id),
    (long, score)
);

Enum(cmeta_data_test_state,
    (CMETA_DATA_TEST_IDLE, 1, "idle"),
    (CMETA_DATA_TEST_READY, 2, "ready")
);

typedef struct cmeta_data_test_variant_storage {
    int tag;
    union {
        int number;
        long wide;
    } value;
} cmeta_data_test_variant_storage;

typedef enum cmeta_data_test_variant_select_mode {
    CMETA_DATA_TEST_VARIANT_SELECT_OK,
    CMETA_DATA_TEST_VARIANT_SELECT_FAIL,
    CMETA_DATA_TEST_VARIANT_SELECT_WRONG_TAG,
    CMETA_DATA_TEST_VARIANT_SELECT_STAYS_ZERO
} cmeta_data_test_variant_select_mode;

static const cmeta_type_identity cmeta_data_test_record_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.Record");
static const cmeta_type_desc cmeta_data_test_record_type = {
    .name = "cmeta_data_test_record",
    .size = sizeof(cmeta_data_test_record),
    .align = _Alignof(cmeta_data_test_record),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = &cmeta_data_test_record_identity
};

static const cmeta_type_identity cmeta_data_test_variant_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.Variant");
static const cmeta_type_desc cmeta_data_test_variant_type = {
    .name = "cmeta_data_test_variant_storage",
    .size = sizeof(cmeta_data_test_variant_storage),
    .align = _Alignof(cmeta_data_test_variant_storage),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = &cmeta_data_test_variant_identity
};

static cmeta_data_test_variant_select_mode cmeta_data_test_variant_mode;

static bool cmeta_data_test_variant_is_zero(const void *object) {
    const cmeta_data_test_variant_storage *value =
        (const cmeta_data_test_variant_storage *)object;
    return value != NULL && value->tag == 0;
}

static cmeta_status cmeta_data_test_variant_active_tag(const void *object,
                                                       int64_t *out) {
    const cmeta_data_test_variant_storage *value =
        (const cmeta_data_test_variant_storage *)object;
    if (value == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    *out = value->tag;
    return CMETA_OK;
}

static cmeta_status cmeta_data_test_variant_select(void *object, int64_t tag) {
    cmeta_data_test_variant_storage *value =
        (cmeta_data_test_variant_storage *)object;
    if (value == NULL)
        return CMETA_INVALID_ARGUMENT;
    memset(&value->value, 0, sizeof(value->value));
    if (cmeta_data_test_variant_mode ==
        CMETA_DATA_TEST_VARIANT_SELECT_STAYS_ZERO) {
        value->tag = 0;
        return CMETA_OK;
    }
    value->tag = (int)tag;
    if (cmeta_data_test_variant_mode ==
        CMETA_DATA_TEST_VARIANT_SELECT_WRONG_TAG)
        value->tag += 1;
    return cmeta_data_test_variant_mode == CMETA_DATA_TEST_VARIANT_SELECT_FAIL
               ? CMETA_CALLBACK_ERROR
               : CMETA_OK;
}

static void cmeta_data_test_variant_restore_zero(void *object) {
    if (object != NULL)
        memset(object, 0, sizeof(cmeta_data_test_variant_storage));
}

static const cmeta_type_identity cmeta_data_test_enum_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.State");
static const cmeta_type_desc cmeta_data_test_enum_type = {
    .name = "cmeta_data_test_state",
    .size = sizeof(cmeta_data_test_state),
    .align = _Alignof(cmeta_data_test_state),
    .kind = CMETA_T_INTEGER,
    .pointee = NULL,
    .traits = NULL,
    .identity = &cmeta_data_test_enum_identity
};

static bool cmeta_data_test_enum_assign_fails;

static bool cmeta_data_test_enum_is_zero(const void *object) {
    cmeta_data_test_state value;
    if (object == NULL)
        return false;
    memcpy(&value, object, sizeof(value));
    return CMETA_ENUM_TO_INT64(value) == 0;
}

static cmeta_status cmeta_data_test_enum_read(const void *object,
                                              int64_t *out) {
    cmeta_data_test_state value;
    if (object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    memcpy(&value, object, sizeof(value));
    *out = CMETA_ENUM_TO_INT64(value);
    return CMETA_OK;
}

static cmeta_status cmeta_data_test_enum_assign(void *object, int64_t value) {
    cmeta_data_test_state native;
    if (object == NULL)
        return CMETA_INVALID_ARGUMENT;
    native = CMETA_ENUM_FROM_INT64(cmeta_data_test_state, value);
    memcpy(object, &native, sizeof(native));
    return cmeta_data_test_enum_assign_fails ? CMETA_CALLBACK_ERROR : CMETA_OK;
}

static void cmeta_data_test_enum_restore_zero(void *object) {
    cmeta_data_test_state value = CMETA_ENUM_FROM_INT64(cmeta_data_test_state, 0);
    if (object != NULL)
        memcpy(object, &value, sizeof(value));
}

static const cmeta_data_enum_shape cmeta_data_test_enum_shape = {
    .meta = EnumMeta(cmeta_data_test_state)
};

static const cmeta_data_enum_ops cmeta_data_test_enum_ops = {
    .struct_size = sizeof(cmeta_data_enum_ops),
    .abi_version = CMETA_DATA_ENUM_OPS_ABI_VERSION,
    .storage_type = &cmeta_data_test_enum_type,
    .is_zero = cmeta_data_test_enum_is_zero,
    .read = cmeta_data_test_enum_read,
    .assign = cmeta_data_test_enum_assign,
    .restore_zero = cmeta_data_test_enum_restore_zero
};

static const cmeta_data_desc cmeta_data_test_enum_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.State.data",
    .display_name = "State",
    .kind = CMETA_DATA_ENUM,
    .storage_type = &cmeta_data_test_enum_type,
    .shape = &cmeta_data_test_enum_shape,
    .enum_ops = &cmeta_data_test_enum_ops
};

static bool cmeta_data_test_buffer_is_zero(const void *object) {
    return object != NULL && *(const int *)object == 0;
}

static cmeta_status cmeta_data_test_buffer_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
    (void)data;
    if (object == NULL || size > max_bytes)
        return CMETA_INVALID_ARGUMENT;
    *(int *)object = (int)size;
    return size == 13u ? CMETA_CALLBACK_ERROR : CMETA_OK;
}

static void cmeta_data_test_buffer_restore_zero(void *object) {
    if (object != NULL)
        *(int *)object = 0;
}

static const cmeta_data_buffer_shape cmeta_data_test_owned_buffer_shape = {
    .ownership = CMETA_DATA_BUFFER_OWNED
};

static const cmeta_data_buffer_ops cmeta_data_test_buffer_ops = {
    .struct_size = sizeof(cmeta_data_buffer_ops),
    .abi_version = CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    .storage_type = &cmeta_type_int,
    .ownership = CMETA_DATA_BUFFER_OWNED,
    .is_zero = cmeta_data_test_buffer_is_zero,
    .assign = cmeta_data_test_buffer_assign,
    .restore_zero = cmeta_data_test_buffer_restore_zero
};

static const cmeta_data_desc cmeta_data_test_buffer_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.Buffer.data",
    .display_name = "Buffer",
    .kind = CMETA_DATA_BYTES,
    .storage_type = &cmeta_type_int,
    .shape = &cmeta_data_test_owned_buffer_shape,
    .buffer_ops = &cmeta_data_test_buffer_ops
};

static const cmeta_data_field_desc cmeta_data_test_record_fields[] = {
    {
        .stable_id = "test.Record.id",
        .name = "id",
        .offset = offsetof(cmeta_data_test_record, id),
        .value = &cmeta_data_int
    },
    {
        .stable_id = "test.Record.score",
        .name = "score",
        .offset = offsetof(cmeta_data_test_record, score),
        .value = &cmeta_data_long
    }
};

static const cmeta_data_struct_shape cmeta_data_test_record_shape = {
    .layout = StructMeta(cmeta_data_test_record),
    .fields = cmeta_data_test_record_fields,
    .field_count = sizeof(cmeta_data_test_record_fields) /
                   sizeof(cmeta_data_test_record_fields[0])
};

static const cmeta_data_desc cmeta_data_test_record_desc = {
    .struct_size = offsetof(cmeta_data_desc, shape) +
                   sizeof(((cmeta_data_desc *)0)->shape),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.Record.data",
    .display_name = "Record",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cmeta_data_test_record_type,
    .shape = &cmeta_data_test_record_shape
};

static const cmeta_data_variant_case cmeta_data_test_variant_cases[] = {
    {
        .tag = 1,
        .stable_id = "test.Variant.number",
        .name = "number",
        .offset = offsetof(cmeta_data_test_variant_storage, value),
        .value = &cmeta_data_int
    },
    {
        .tag = 2,
        .stable_id = "test.Variant.wide",
        .name = "wide",
        .offset = offsetof(cmeta_data_test_variant_storage, value),
        .value = &cmeta_data_long
    }
};

static const cmeta_data_variant_shape cmeta_data_test_variant_shape = {
    .tag_offset = offsetof(cmeta_data_test_variant_storage, tag),
    .tag = &cmeta_data_int,
    .cases = cmeta_data_test_variant_cases,
    .case_count = sizeof(cmeta_data_test_variant_cases) /
                  sizeof(cmeta_data_test_variant_cases[0])
};

static const cmeta_data_variant_ops cmeta_data_test_variant_ops = {
    .struct_size = sizeof(cmeta_data_variant_ops),
    .abi_version = CMETA_DATA_VARIANT_OPS_ABI_VERSION,
    .storage_type = &cmeta_data_test_variant_type,
    .is_zero = cmeta_data_test_variant_is_zero,
    .active_tag = cmeta_data_test_variant_active_tag,
    .select = cmeta_data_test_variant_select,
    .restore_zero = cmeta_data_test_variant_restore_zero
};

static const cmeta_data_desc cmeta_data_test_variant_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.Variant.data",
    .display_name = "Variant",
    .kind = CMETA_DATA_VARIANT,
    .storage_type = &cmeta_data_test_variant_type,
    .shape = &cmeta_data_test_variant_shape,
    .variant_ops = &cmeta_data_test_variant_ops
};

spec("CMeta semantic data descriptors") {
  it("exposes primitive semantic descriptors") {
    check_equal(cmeta_data_bool.kind, CMETA_DATA_BOOL);
    check_equal(cmeta_data_int.kind, CMETA_DATA_SINT);
    check_equal(cmeta_data_size.kind, CMETA_DATA_UINT);
    check_equal(cmeta_data_float.kind, CMETA_DATA_FLOAT);
    check_true(cmeta_data_desc_valid(&cmeta_data_bool));
    check_true(cmeta_data_desc_valid(&cmeta_data_int));
    check_true(cmeta_data_desc_valid(&cmeta_data_long));
    check_true(cmeta_data_desc_valid(&cmeta_data_size));
    check_true(cmeta_data_desc_valid(&cmeta_data_float));
    check_true(cmeta_data_desc_valid(&cmeta_data_double));
  }

  it("keeps semantic descriptor ABI append safe") {
    cmeta_data_desc prefix = cmeta_data_int;
    prefix.struct_size = offsetof(cmeta_data_desc, shape) + sizeof(prefix.shape);

    check_true(cmeta_data_desc_valid(&prefix));

    prefix.struct_size = offsetof(cmeta_data_desc, shape);
    check_false(cmeta_data_desc_valid(&prefix));
  }

  it("rejects invalid scalar and buffer shapes") {
    const cmeta_data_integer_shape bad_integer = { .bits = 7u };
    const cmeta_data_float_shape bad_float = { .bits = 16u };
    const cmeta_data_buffer_shape owned = { .ownership = CMETA_DATA_BUFFER_OWNED };
    const cmeta_data_buffer_shape invalid_buffer = {
        .ownership = (cmeta_data_buffer_ownership)99
    };
    cmeta_data_desc desc = {
        .struct_size = offsetof(cmeta_data_desc, shape) + sizeof(desc.shape),
        .abi_version = CMETA_DATA_DESC_ABI_VERSION,
        .stable_id = "test.scalar",
        .display_name = "scalar",
        .kind = CMETA_DATA_SINT,
        .storage_type = &cmeta_type_int,
        .shape = &bad_integer
    };

    check_false(cmeta_data_desc_valid(&desc));

    desc.kind = CMETA_DATA_FLOAT;
    desc.shape = &bad_float;
    check_false(cmeta_data_desc_valid(&desc));

    desc.kind = CMETA_DATA_STRING;
    desc.shape = &owned;
    check_true(cmeta_data_desc_valid(&desc));

    desc.kind = CMETA_DATA_BYTES;
    desc.shape = &invalid_buffer;
    check_false(cmeta_data_desc_valid(&desc));
  }

  it("keeps legacy buffer descriptors valid without inventing storage ops") {
    cmeta_data_desc prefix = cmeta_data_test_buffer_desc;
    prefix.struct_size = offsetof(cmeta_data_desc, shape) + sizeof(prefix.shape);

    check_true(cmeta_data_desc_valid(&prefix));
    check_null(cmeta_data_buffer_ops_of(&prefix));
  }

  it("assigns and restores buffers through a checked adapter") {
    static const unsigned char input[] = {'a', 'b', 'c'};
    int object = 0;
    bool is_zero = false;

    check_true(cmeta_data_buffer_ops_of(&cmeta_data_test_buffer_desc) ==
               &cmeta_data_test_buffer_ops);
    check_equal(cmeta_data_buffer_is_zero(&cmeta_data_test_buffer_desc,
                                          &object, &is_zero), CMETA_OK);
    check_true(is_zero);
    check_equal(cmeta_data_buffer_assign(&cmeta_data_test_buffer_desc, &object,
                                         input, sizeof(input), sizeof(input)),
                CMETA_OK);
    check_equal(object, 3);
    check_equal(cmeta_data_buffer_restore_zero(&cmeta_data_test_buffer_desc,
                                               &object), CMETA_OK);
    check_equal(object, 0);
  }

  it("rejects invalid buffer assignment without mutating the destination") {
    static const unsigned char input[] = {'a', 'b', 'c'};
    int object = 0;

    check_equal(cmeta_data_buffer_assign(&cmeta_data_test_buffer_desc, &object,
                                         input, sizeof(input), 2u),
                CMETA_CAPACITY_EXCEEDED);
    check_equal(object, 0);

    check_equal(cmeta_data_buffer_assign(&cmeta_data_test_buffer_desc, &object,
                                         NULL, 1u, 1u),
                CMETA_INVALID_ARGUMENT);
    check_equal(object, 0);

    object = 1;
    check_equal(cmeta_data_buffer_assign(&cmeta_data_test_buffer_desc, &object,
                                         input, sizeof(input), sizeof(input)),
                CMETA_INVALID_ARGUMENT);
    check_equal(object, 1);
  }

  it("restores semantic zero when a provider assignment fails") {
    static const unsigned char input[13] = {0};
    int object = 0;

    check_equal(cmeta_data_buffer_assign(&cmeta_data_test_buffer_desc, &object,
                                         input, sizeof(input), sizeof(input)),
                CMETA_CALLBACK_ERROR);
    check_equal(object, 0);
  }

  it("rejects malformed or mismatched buffer adapters") {
    cmeta_data_buffer_ops ops = cmeta_data_test_buffer_ops;
    cmeta_data_desc desc = cmeta_data_test_buffer_desc;
    int object = 0;
    bool is_zero = false;

    ops.abi_version += 1u;
    desc.buffer_ops = &ops;
    check_null(cmeta_data_buffer_ops_of(&desc));
    check_equal(cmeta_data_buffer_is_zero(&desc, &object, &is_zero),
                CMETA_INVALID_ARGUMENT);

    ops = cmeta_data_test_buffer_ops;
    ops.storage_type = &cmeta_type_long;
    desc.buffer_ops = &ops;
    check_null(cmeta_data_buffer_ops_of(&desc));
    check_equal(cmeta_data_buffer_is_zero(&desc, &object, &is_zero),
                CMETA_TYPE_MISMATCH);

    ops = cmeta_data_test_buffer_ops;
    ops.ownership = CMETA_DATA_BUFFER_BORROWED;
    desc.buffer_ops = &ops;
    check_null(cmeta_data_buffer_ops_of(&desc));
    check_equal(cmeta_data_buffer_is_zero(&desc, &object, &is_zero),
                CMETA_TYPE_MISMATCH);

    ops = cmeta_data_test_buffer_ops;
    {
        cmeta_type_desc forged = cmeta_type_int;
        forged.size += 1u;
        ops.storage_type = &forged;
        desc.buffer_ops = &ops;
        check_true(cmeta_type_equal(desc.storage_type, ops.storage_type));
        check_null(cmeta_data_buffer_ops_of(&desc));
        check_equal(cmeta_data_buffer_is_zero(&desc, &object, &is_zero),
                    CMETA_TYPE_MISMATCH);
    }
  }

  it("validates enum semantic metadata") {
    const cmeta_data_enum_shape enum_shape = {
        .meta = EnumMeta(cmeta_data_test_state)
    };
    cmeta_data_desc desc = {
        .struct_size = offsetof(cmeta_data_desc, shape) + sizeof(desc.shape),
        .abi_version = CMETA_DATA_DESC_ABI_VERSION,
        .stable_id = "test.State.data",
        .display_name = "State",
        .kind = CMETA_DATA_ENUM,
        .storage_type = &cmeta_data_test_enum_type,
        .shape = &enum_shape
    };

    check_true(cmeta_data_desc_valid(&desc));
    check_equal(enum_shape.meta->count, (size_t)2u);
  }

  it("checks enum storage adapters and restores zero after failure") {
    cmeta_data_test_state object =
        CMETA_ENUM_FROM_INT64(cmeta_data_test_state, 0);
    cmeta_data_enum_ops ops = cmeta_data_test_enum_ops;
    cmeta_data_desc desc = cmeta_data_test_enum_desc;
    int64_t value = -1;
    bool is_zero = false;

    cmeta_data_test_enum_assign_fails = false;
    check_true(cmeta_data_enum_ops_of(&desc) == &cmeta_data_test_enum_ops);
    check_equal(cmeta_data_enum_is_zero(&desc, &object, &is_zero), CMETA_OK);
    check_true(is_zero);
    check_equal(cmeta_data_enum_assign(&desc, &object,
                                       CMETA_DATA_TEST_READY), CMETA_OK);
    check_equal(cmeta_data_enum_read(&desc, &object, &value), CMETA_OK);
    check_equal(value, (int64_t)CMETA_DATA_TEST_READY);
    check_equal(cmeta_data_enum_restore_zero(&desc, &object), CMETA_OK);
    check_equal(CMETA_ENUM_TO_INT64(object), INT64_C(0));

    check_equal(cmeta_data_enum_assign(&desc, &object, INT64_C(99)),
                CMETA_INVALID_ARGUMENT);
    check_equal(CMETA_ENUM_TO_INT64(object), INT64_C(0));

    cmeta_data_test_enum_assign_fails = true;
    check_equal(cmeta_data_enum_assign(&desc, &object,
                                       CMETA_DATA_TEST_IDLE),
                CMETA_CALLBACK_ERROR);
    check_equal(CMETA_ENUM_TO_INT64(object), INT64_C(0));
    cmeta_data_test_enum_assign_fails = false;

    ops.storage_type = &cmeta_type_int;
    desc.enum_ops = &ops;
    check_null(cmeta_data_enum_ops_of(&desc));
    check_equal(cmeta_data_enum_read(&desc, &object, &value),
                CMETA_TYPE_MISMATCH);
  }

  it("looks up struct semantic fields and checks reflected offsets") {
    cmeta_data_field_desc bad_field = cmeta_data_test_record_fields[0];
    cmeta_data_struct_shape bad_shape = cmeta_data_test_record_shape;
    cmeta_data_desc bad_desc = cmeta_data_test_record_desc;

    check_true(cmeta_data_desc_valid(&cmeta_data_test_record_desc));
    check_true(cmeta_data_struct_field(&cmeta_data_test_record_shape, 0u) ==
               &cmeta_data_test_record_fields[0]);
    check_true(cmeta_data_struct_find_field(&cmeta_data_test_record_shape,
                                            "score") ==
               &cmeta_data_test_record_fields[1]);
    check_null(cmeta_data_struct_find_field(&cmeta_data_test_record_shape,
                                            "missing"));

    bad_field.offset += 1u;
    bad_shape.fields = &bad_field;
    bad_shape.field_count = 1u;
    bad_desc.shape = &bad_shape;
    check_false(cmeta_data_desc_valid(&bad_desc));
  }

  it("keeps struct validation shallow for recursive semantic graphs") {
    const cmeta_data_desc unresolved_child = {0};
    cmeta_data_field_desc field = cmeta_data_test_record_fields[0];
    cmeta_data_struct_shape shape = cmeta_data_test_record_shape;
    cmeta_data_desc desc = cmeta_data_test_record_desc;

    field.value = &unresolved_child;
    shape.fields = &field;
    shape.field_count = 1u;
    desc.shape = &shape;

    check_true(cmeta_data_desc_valid(&desc));
  }

  it("looks up variant cases and rejects duplicate tags") {
    cmeta_data_variant_case duplicate_cases[2] = {
        cmeta_data_test_variant_cases[0],
        cmeta_data_test_variant_cases[1]
    };
    cmeta_data_variant_shape duplicate_shape = cmeta_data_test_variant_shape;
    cmeta_data_desc duplicate_desc = cmeta_data_test_variant_desc;

    check_true(cmeta_data_desc_valid(&cmeta_data_test_variant_desc));
    check_true(cmeta_data_variant_case_by_tag(&cmeta_data_test_variant_shape,
                                              1) ==
               &cmeta_data_test_variant_cases[0]);
    check_true(cmeta_data_variant_case_by_tag(&cmeta_data_test_variant_shape,
                                              2) ==
               &cmeta_data_test_variant_cases[1]);
    check_null(cmeta_data_variant_case_by_tag(&cmeta_data_test_variant_shape,
                                              3));

    duplicate_cases[1].tag = 1;
    duplicate_shape.cases = duplicate_cases;
    duplicate_desc.shape = &duplicate_shape;
    check_false(cmeta_data_desc_valid(&duplicate_desc));
  }

  it("checks variant lifecycle adapters and enforces select postconditions") {
    cmeta_data_test_variant_storage object = {0};
    cmeta_data_variant_ops ops = cmeta_data_test_variant_ops;
    cmeta_data_desc desc = cmeta_data_test_variant_desc;
    int64_t tag = 0;
    bool is_zero = false;

    cmeta_data_test_variant_mode = CMETA_DATA_TEST_VARIANT_SELECT_OK;
    check_true(cmeta_data_variant_ops_of(&desc) ==
               &cmeta_data_test_variant_ops);
    check_equal(cmeta_data_variant_is_zero(&desc, &object, &is_zero),
                CMETA_OK);
    check_true(is_zero);
    check_equal(cmeta_data_variant_select(&desc, &object, INT64_C(1)),
                CMETA_OK);
    check_equal(cmeta_data_variant_active_tag(&desc, &object, &tag),
                CMETA_OK);
    check_equal(tag, INT64_C(1));
    check_equal(cmeta_data_variant_restore_zero(&desc, &object), CMETA_OK);
    check_equal(object.tag, 0);

    check_equal(cmeta_data_variant_select(&desc, &object, INT64_C(99)),
                CMETA_INVALID_ARGUMENT);
    check_equal(object.tag, 0);

    cmeta_data_test_variant_mode = CMETA_DATA_TEST_VARIANT_SELECT_FAIL;
    check_equal(cmeta_data_variant_select(&desc, &object, INT64_C(1)),
                CMETA_CALLBACK_ERROR);
    check_equal(object.tag, 0);

    cmeta_data_test_variant_mode = CMETA_DATA_TEST_VARIANT_SELECT_WRONG_TAG;
    check_equal(cmeta_data_variant_select(&desc, &object, INT64_C(1)),
                CMETA_CALLBACK_ERROR);
    check_equal(object.tag, 0);

    cmeta_data_test_variant_mode = CMETA_DATA_TEST_VARIANT_SELECT_STAYS_ZERO;
    check_equal(cmeta_data_variant_select(&desc, &object, INT64_C(1)),
                CMETA_CALLBACK_ERROR);
    check_equal(object.tag, 0);
    cmeta_data_test_variant_mode = CMETA_DATA_TEST_VARIANT_SELECT_OK;

    ops.storage_type = &cmeta_type_int;
    desc.variant_ops = &ops;
    check_null(cmeta_data_variant_ops_of(&desc));
    check_equal(cmeta_data_variant_active_tag(&desc, &object, &tag),
                CMETA_TYPE_MISMATCH);
  }

  it("rejects an invalid or non-integral variant tag descriptor") {
    const cmeta_data_integer_shape bad_integer = { .bits = 7u };
    const cmeta_data_desc invalid_tag = {
        .struct_size = offsetof(cmeta_data_desc, shape) +
                       sizeof(((cmeta_data_desc *)0)->shape),
        .abi_version = CMETA_DATA_DESC_ABI_VERSION,
        .stable_id = "test.BadTag.data",
        .display_name = "BadTag",
        .kind = CMETA_DATA_SINT,
        .storage_type = &cmeta_type_int,
        .shape = &bad_integer
    };
    cmeta_data_variant_shape shape = cmeta_data_test_variant_shape;
    cmeta_data_desc desc = cmeta_data_test_variant_desc;

    shape.tag = &invalid_tag;
    desc.shape = &shape;
    check_false(cmeta_data_desc_valid(&desc));

    shape.tag = &cmeta_data_float;
    check_false(cmeta_data_desc_valid(&desc));
  }

  it("keeps container categories free of T K V") {
    check_equal(cmeta_data_sequence.kind, CMETA_DATA_SEQUENCE);
    check_equal(cmeta_data_set.kind, CMETA_DATA_SET);
    check_equal(cmeta_data_map.kind, CMETA_DATA_MAP);
    check_true(cmeta_data_kind_is_container(CMETA_DATA_SEQUENCE));
    check_true(cmeta_data_kind_is_container(CMETA_DATA_SET));
    check_true(cmeta_data_kind_is_container(CMETA_DATA_MAP));
    check_false(cmeta_data_kind_is_container(CMETA_DATA_STRUCT));
    check_null(cmeta_data_sequence.storage_type);
    check_null(cmeta_data_sequence.shape);
    check_true(cmeta_data_desc_valid(&cmeta_data_sequence));
    check_true(cmeta_data_desc_valid(&cmeta_data_set));
    check_true(cmeta_data_desc_valid(&cmeta_data_map));
  }

  it("validates kind boundaries and custom semantic data") {
    static const int custom_shape = 1;
    cmeta_data_desc custom = {
        .struct_size = offsetof(cmeta_data_desc, shape) + sizeof(custom.shape),
        .abi_version = CMETA_DATA_DESC_ABI_VERSION,
        .stable_id = "test.Custom.data",
        .display_name = "Custom",
        .kind = CMETA_DATA_CUSTOM,
        .storage_type = &cmeta_type_int,
        .shape = &custom_shape
    };

    check_true(cmeta_data_kind_valid(CMETA_DATA_BOOL));
    check_true(cmeta_data_kind_valid(CMETA_DATA_CUSTOM));
    check_false(cmeta_data_kind_valid((cmeta_data_kind)-1));
    check_false(cmeta_data_kind_valid((cmeta_data_kind)99));
    check_true(cmeta_data_desc_valid(&custom));

    custom.shape = NULL;
    check_false(cmeta_data_desc_valid(&custom));
  }
}
