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

static const cmeta_data_desc cmeta_data_test_variant_desc = {
    .struct_size = offsetof(cmeta_data_desc, shape) +
                   sizeof(((cmeta_data_desc *)0)->shape),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.Variant.data",
    .display_name = "Variant",
    .kind = CMETA_DATA_VARIANT,
    .storage_type = &cmeta_data_test_variant_type,
    .shape = &cmeta_data_test_variant_shape
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

  it("rejects a variant whose tag is not integer or enum semantic data") {
    cmeta_data_variant_shape shape = cmeta_data_test_variant_shape;
    cmeta_data_desc desc = cmeta_data_test_variant_desc;

    shape.tag = &cmeta_data_float;
    desc.shape = &shape;
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
