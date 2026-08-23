#include <cbind/cbind.h>
#include "recording.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>

#define CBIND_DATA_PREFIX_SIZE \
    (offsetof(cmeta_data_desc, shape) + sizeof(((cmeta_data_desc *)0)->shape))

Struct(cbind_test_empty_storage,
    (int, marker)
);

Struct(cbind_test_one,
    (int, a)
);

Struct(cbind_test_eight,
    (int, a0), (int, a1), (int, a2), (int, a3),
    (int, a4), (int, a5), (int, a6), (int, a7)
);

Struct(cbind_test_nine,
    (int, a0), (int, a1), (int, a2), (int, a3), (int, a4),
    (int, a5), (int, a6), (int, a7), (int, a8)
);

Struct(cbind_test_inner,
    (int, count),
    (double, ratio)
);

Struct(cbind_test_record,
    (int, id),
    (long, score),
    (cbind_test_inner, inner),
    (int, untouched)
);

Struct(cbind_test_siblings,
    (cbind_test_inner, left),
    (cbind_test_inner, right)
);

#define DEFINE_OBJECT_TYPE(symbol, ctype, stable_name) \
    static const cmeta_type_identity symbol##_identity = \
        CMETA_TYPE_ID_ATOM_INIT(stable_name); \
    static const cmeta_type_desc symbol = { \
        .name = #ctype, \
        .size = sizeof(ctype), \
        .align = _Alignof(ctype), \
        .kind = CMETA_T_OBJECT, \
        .pointee = NULL, \
        .traits = NULL, \
        .identity = &symbol##_identity \
    }

DEFINE_OBJECT_TYPE(cbind_test_empty_type, cbind_test_empty_storage,
                   "test.cbind.empty");
DEFINE_OBJECT_TYPE(cbind_test_one_type, cbind_test_one,
                   "test.cbind.one");
DEFINE_OBJECT_TYPE(cbind_test_eight_type, cbind_test_eight,
                   "test.cbind.eight");
DEFINE_OBJECT_TYPE(cbind_test_nine_type, cbind_test_nine,
                   "test.cbind.nine");
DEFINE_OBJECT_TYPE(cbind_test_inner_type, cbind_test_inner,
                   "test.cbind.inner");
DEFINE_OBJECT_TYPE(cbind_test_record_type, cbind_test_record,
                   "test.cbind.record");
DEFINE_OBJECT_TYPE(cbind_test_siblings_type, cbind_test_siblings,
                   "test.cbind.siblings");

static const cmeta_data_struct_shape cbind_test_empty_shape = {
    .layout = StructMeta(cbind_test_empty_storage),
    .fields = NULL,
    .field_count = 0u
};
static const cmeta_data_desc cbind_test_empty_data = {
    .struct_size = CBIND_DATA_PREFIX_SIZE,
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.empty.data",
    .display_name = "empty",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cbind_test_empty_type,
    .shape = &cbind_test_empty_shape
};

static const cmeta_data_field_desc cbind_test_one_fields[] = {
    {"test.cbind.one.a", "a", offsetof(cbind_test_one, a), &cmeta_data_int}
};
static const cmeta_data_struct_shape cbind_test_one_shape = {
    .layout = StructMeta(cbind_test_one),
    .fields = cbind_test_one_fields,
    .field_count = 1u
};
static const cmeta_data_desc cbind_test_one_data = {
    .struct_size = CBIND_DATA_PREFIX_SIZE,
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.one.data",
    .display_name = "one",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cbind_test_one_type,
    .shape = &cbind_test_one_shape
};

#define INT_SEM_FIELD(owner, member) \
    {"test.cbind." #owner "." #member, #member, offsetof(owner, member), \
     &cmeta_data_int}

static const cmeta_data_field_desc cbind_test_eight_fields[] = {
    INT_SEM_FIELD(cbind_test_eight, a0), INT_SEM_FIELD(cbind_test_eight, a1),
    INT_SEM_FIELD(cbind_test_eight, a2), INT_SEM_FIELD(cbind_test_eight, a3),
    INT_SEM_FIELD(cbind_test_eight, a4), INT_SEM_FIELD(cbind_test_eight, a5),
    INT_SEM_FIELD(cbind_test_eight, a6), INT_SEM_FIELD(cbind_test_eight, a7)
};
static const cmeta_data_struct_shape cbind_test_eight_shape = {
    .layout = StructMeta(cbind_test_eight),
    .fields = cbind_test_eight_fields,
    .field_count = 8u
};
static const cmeta_data_desc cbind_test_eight_data = {
    .struct_size = CBIND_DATA_PREFIX_SIZE,
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.eight.data",
    .display_name = "eight",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cbind_test_eight_type,
    .shape = &cbind_test_eight_shape
};

static const cmeta_data_field_desc cbind_test_nine_fields[] = {
    INT_SEM_FIELD(cbind_test_nine, a0), INT_SEM_FIELD(cbind_test_nine, a1),
    INT_SEM_FIELD(cbind_test_nine, a2), INT_SEM_FIELD(cbind_test_nine, a3),
    INT_SEM_FIELD(cbind_test_nine, a4), INT_SEM_FIELD(cbind_test_nine, a5),
    INT_SEM_FIELD(cbind_test_nine, a6), INT_SEM_FIELD(cbind_test_nine, a7),
    INT_SEM_FIELD(cbind_test_nine, a8)
};
static const cmeta_data_struct_shape cbind_test_nine_shape = {
    .layout = StructMeta(cbind_test_nine),
    .fields = cbind_test_nine_fields,
    .field_count = 9u
};
static const cmeta_data_desc cbind_test_nine_data = {
    .struct_size = CBIND_DATA_PREFIX_SIZE,
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.nine.data",
    .display_name = "nine",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cbind_test_nine_type,
    .shape = &cbind_test_nine_shape
};

static const cmeta_data_field_desc cbind_test_inner_fields[] = {
    {"test.cbind.inner.count", "count", offsetof(cbind_test_inner, count),
     &cmeta_data_int},
    {"test.cbind.inner.ratio", "ratio", offsetof(cbind_test_inner, ratio),
     &cmeta_data_double}
};
static const cmeta_data_struct_shape cbind_test_inner_shape = {
    .layout = StructMeta(cbind_test_inner),
    .fields = cbind_test_inner_fields,
    .field_count = 2u
};
static const cmeta_data_desc cbind_test_inner_data = {
    .struct_size = CBIND_DATA_PREFIX_SIZE,
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.inner.data",
    .display_name = "inner",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cbind_test_inner_type,
    .shape = &cbind_test_inner_shape
};

static const cmeta_data_field_desc cbind_test_record_fields[] = {
    {"test.cbind.record.id", "id", offsetof(cbind_test_record, id),
     &cmeta_data_int},
    {"test.cbind.record.score", "score", offsetof(cbind_test_record, score),
     &cmeta_data_long},
    {"test.cbind.record.inner", "inner", offsetof(cbind_test_record, inner),
     &cbind_test_inner_data}
};
static const cmeta_data_struct_shape cbind_test_record_shape = {
    .layout = StructMeta(cbind_test_record),
    .fields = cbind_test_record_fields,
    .field_count = 3u
};
static const cmeta_data_desc cbind_test_record_data = {
    .struct_size = CBIND_DATA_PREFIX_SIZE,
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.record.data",
    .display_name = "record",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cbind_test_record_type,
    .shape = &cbind_test_record_shape
};

static const cmeta_data_field_desc cbind_test_siblings_fields[] = {
    {"test.cbind.siblings.left", "left", offsetof(cbind_test_siblings, left),
     &cbind_test_inner_data},
    {"test.cbind.siblings.right", "right", offsetof(cbind_test_siblings, right),
     &cbind_test_inner_data}
};
static const cmeta_data_struct_shape cbind_test_siblings_shape = {
    .layout = StructMeta(cbind_test_siblings),
    .fields = cbind_test_siblings_fields,
    .field_count = 2u
};
static const cmeta_data_desc cbind_test_siblings_data = {
    .struct_size = CBIND_DATA_PREFIX_SIZE,
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.siblings.data",
    .display_name = "siblings",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cbind_test_siblings_type,
    .shape = &cbind_test_siblings_shape
};

#undef INT_SEM_FIELD
#undef DEFINE_OBJECT_TYPE

static cbind_status probe_preflight(const cmeta_data_desc *shape,
                                    void *out,
                                    size_t max_depth,
                                    void *scratch,
                                    size_t scratch_size,
                                    size_t *source_index) {
    const cserde_token token = {.kind = CSERDE_MAP_BEGIN};
    cserde_recording_reader_context source = {&token, 1u, 0u};
    cserde_reader reader = {0};
    cbind_context context =
        CBIND_CONTEXT_INIT(scratch, scratch_size, max_depth);
    cbind_error error = CBIND_ERROR_INIT;
    cbind_status status;

    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops, &source),
                CSERDE_OK);
    status = cbind_decode(&context, shape, &reader, out, &error);
    *source_index = source.index;
    return status;
}

static void check_rejected_before_input(const cmeta_data_desc *shape,
                                        void *out,
                                        size_t max_depth,
                                        void *scratch,
                                        size_t scratch_size,
                                        cbind_status expected) {
    size_t source_index = SIZE_MAX;
    cbind_status status = probe_preflight(shape, out, max_depth, scratch,
                                          scratch_size, &source_index);
    check_equal(status, expected);
    check_equal(source_index, (size_t)0u);
}

static void check_preflight_reaches_reader(const cmeta_data_desc *shape,
                                           void *out,
                                           size_t max_depth,
                                           void *scratch,
                                           size_t scratch_size) {
    size_t source_index = SIZE_MAX;
    (void)probe_preflight(shape, out, max_depth, scratch, scratch_size,
                          &source_index);
    check_equal(source_index, (size_t)1u);
}

spec("CBind struct preflight resource budgets") {
  it("counts struct depth even when the semantic struct has no fields") {
    cbind_test_empty_storage out = {0};

    check_rejected_before_input(&cbind_test_empty_data, &out, 0u,
                                NULL, 0u, CBIND_LIMIT_EXCEEDED);
    check_preflight_reaches_reader(&cbind_test_empty_data, &out, 1u,
                                   NULL, 0u);
  }

  it("requires one bitmap byte for one through eight fields") {
    cbind_test_one one = {0};
    cbind_test_eight eight = {0};
    unsigned char scratch[1] = {0};

    check_rejected_before_input(&cbind_test_one_data, &one, 1u,
                                NULL, 0u, CBIND_LIMIT_EXCEEDED);
    check_preflight_reaches_reader(&cbind_test_one_data, &one, 1u,
                                   scratch, sizeof(scratch));

    check_rejected_before_input(&cbind_test_eight_data, &eight, 1u,
                                NULL, 0u, CBIND_LIMIT_EXCEEDED);
    check_preflight_reaches_reader(&cbind_test_eight_data, &eight, 1u,
                                   scratch, sizeof(scratch));
  }

  it("requires two bitmap bytes for nine fields") {
    cbind_test_nine out = {0};
    unsigned char scratch1[1] = {0};
    unsigned char scratch2[2] = {0};

    check_rejected_before_input(&cbind_test_nine_data, &out, 1u,
                                scratch1, sizeof(scratch1),
                                CBIND_LIMIT_EXCEEDED);
    check_preflight_reaches_reader(&cbind_test_nine_data, &out, 1u,
                                   scratch2, sizeof(scratch2));
  }

  it("uses the active nesting path for scratch and depth") {
    cbind_test_record record = {0};
    cbind_test_siblings siblings = {0};
    unsigned char scratch1[1] = {0};
    unsigned char scratch2[2] = {0};

    check_rejected_before_input(&cbind_test_record_data, &record, 1u,
                                scratch2, sizeof(scratch2),
                                CBIND_LIMIT_EXCEEDED);
    check_rejected_before_input(&cbind_test_record_data, &record, 2u,
                                scratch1, sizeof(scratch1),
                                CBIND_LIMIT_EXCEEDED);
    check_preflight_reaches_reader(&cbind_test_record_data, &record, 2u,
                                   scratch2, sizeof(scratch2));

    check_rejected_before_input(&cbind_test_siblings_data, &siblings, 2u,
                                scratch1, sizeof(scratch1),
                                CBIND_LIMIT_EXCEEDED);
    check_preflight_reaches_reader(&cbind_test_siblings_data, &siblings, 2u,
                                   scratch2, sizeof(scratch2));
  }
}

spec("CBind struct storage proof") {
  it("rejects parent storage size and alignment mismatches") {
    cbind_test_record out = {0};
    cmeta_type_desc bad_type = cbind_test_record_type;
    cmeta_data_desc bad_data = cbind_test_record_data;

    bad_type.size += 1u;
    bad_data.storage_type = &bad_type;
    check_true(cmeta_data_desc_valid(&bad_data));
    check_rejected_before_input(&bad_data, &out, 2u, NULL, 0u,
                                CBIND_INVALID_SHAPE);

    bad_type = cbind_test_record_type;
    bad_type.align += 1u;
    bad_data.storage_type = &bad_type;
    check_true(cmeta_data_desc_valid(&bad_data));
    check_rejected_before_input(&bad_data, &out, 2u, NULL, 0u,
                                CBIND_INVALID_SHAPE);
  }

  it("rejects semantic/reflected offset mismatch") {
    cbind_test_one out = {0};
    cmeta_data_field_desc semantic = cbind_test_one_fields[0];
    cmeta_data_struct_shape shape = cbind_test_one_shape;
    cmeta_data_desc data = cbind_test_one_data;

    semantic.offset += 1u;
    shape.fields = &semantic;
    data.shape = &shape;
    check_false(cmeta_data_desc_valid(&data));
    check_rejected_before_input(&data, &out, 1u, NULL, 0u,
                                CBIND_INVALID_SHAPE);
  }

  it("rejects reflected child size and alignment mismatches") {
    cbind_test_one out = {0};
    cmeta_field_desc reflected = StructMeta(cbind_test_one)->fields[0];
    cmeta_struct_desc layout = *StructMeta(cbind_test_one);
    cmeta_data_struct_shape shape = cbind_test_one_shape;
    cmeta_data_desc data = cbind_test_one_data;
    unsigned char scratch[1] = {0};

    reflected.size += 1u;
    layout.fields = &reflected;
    shape.layout = &layout;
    data.shape = &shape;
    check_true(cmeta_data_desc_valid(&data));
    check_rejected_before_input(&data, &out, 1u, scratch, sizeof(scratch),
                                CBIND_INVALID_SHAPE);

    reflected = StructMeta(cbind_test_one)->fields[0];
    reflected.align += 1u;
    layout.fields = &reflected;
    shape.layout = &layout;
    data.shape = &shape;
    check_true(cmeta_data_desc_valid(&data));
    check_rejected_before_input(&data, &out, 1u, scratch, sizeof(scratch),
                                CBIND_INVALID_SHAPE);
  }

  it("rejects a field range that escapes parent storage") {
    cbind_test_one out = {0};
    cmeta_field_desc reflected = StructMeta(cbind_test_one)->fields[0];
    cmeta_struct_desc layout = *StructMeta(cbind_test_one);
    cmeta_data_field_desc semantic = cbind_test_one_fields[0];
    cmeta_data_struct_shape shape = cbind_test_one_shape;
    cmeta_data_desc data = cbind_test_one_data;
    unsigned char scratch[1] = {0};

    reflected.offset = sizeof(cbind_test_one) - 1u;
    semantic.offset = reflected.offset;
    layout.fields = &reflected;
    shape.layout = &layout;
    shape.fields = &semantic;
    data.shape = &shape;
    check_true(cmeta_data_desc_valid(&data));
    check_rejected_before_input(&data, &out, 1u, scratch, sizeof(scratch),
                                CBIND_INVALID_SHAPE);
  }

  it("rejects duplicate semantic mapping to the same reflected field") {
    cbind_test_inner out = {0};
    cmeta_data_field_desc fields[2] = {
        cbind_test_inner_fields[0], cbind_test_inner_fields[0]
    };
    cmeta_data_struct_shape shape = cbind_test_inner_shape;
    cmeta_data_desc data = cbind_test_inner_data;
    unsigned char scratch[1] = {0};

    fields[1].stable_id = "test.cbind.inner.count.duplicate";
    shape.fields = fields;
    data.shape = &shape;
    check_true(cmeta_data_desc_valid(&data));
    check_rejected_before_input(&data, &out, 1u, scratch, sizeof(scratch),
                                CBIND_INVALID_SHAPE);
  }

  it("rejects a recursive semantic descriptor cycle") {
    static const cmeta_type_identity cycle_identity =
        CMETA_TYPE_ID_ATOM_INIT("test.cbind.cycle");
    static const cmeta_type_desc cycle_type = {
        .name = "cycle_storage",
        .size = sizeof(long),
        .align = _Alignof(long),
        .kind = CMETA_T_OBJECT,
        .pointee = NULL,
        .traits = NULL,
        .identity = &cycle_identity
    };
    const cmeta_field_desc reflected = {
        .name = "self",
        .type_name = "cycle_storage",
        .offset = 0u,
        .size = sizeof(long),
        .align = _Alignof(long),
        .type = NULL,
        .declared_type = NULL
    };
    const cmeta_struct_desc layout = {
        .name = "cycle_layout",
        .size = sizeof(long),
        .align = _Alignof(long),
        .fields = &reflected,
        .field_count = 1u
    };
    cmeta_data_desc data;
    cmeta_data_field_desc field;
    cmeta_data_struct_shape shape;
    long out = 0;
    unsigned char scratch[1] = {0};

    data = (cmeta_data_desc){
        .struct_size = CBIND_DATA_PREFIX_SIZE,
        .abi_version = CMETA_DATA_DESC_ABI_VERSION,
        .stable_id = "test.cbind.cycle.data",
        .display_name = "cycle",
        .kind = CMETA_DATA_STRUCT,
        .storage_type = &cycle_type,
        .shape = NULL
    };
    field = (cmeta_data_field_desc){
        .stable_id = "test.cbind.cycle.self",
        .name = "self",
        .offset = 0u,
        .value = &data
    };
    shape = (cmeta_data_struct_shape){
        .layout = &layout,
        .fields = &field,
        .field_count = 1u
    };
    data.shape = &shape;

    check_true(cmeta_data_desc_valid(&data));
    check_rejected_before_input(&data, &out, 4u, scratch, sizeof(scratch),
                                CBIND_INVALID_SHAPE);
  }

  it("rejects a nested valid but unsupported semantic child") {
    static const int custom_shape = 1;
    const cmeta_data_desc custom = {
        .struct_size = CBIND_DATA_PREFIX_SIZE,
        .abi_version = CMETA_DATA_DESC_ABI_VERSION,
        .stable_id = "test.cbind.custom",
        .display_name = "custom",
        .kind = CMETA_DATA_CUSTOM,
        .storage_type = &cmeta_type_int,
        .shape = &custom_shape
    };
    cbind_test_one out = {0};
    cmeta_data_field_desc field = cbind_test_one_fields[0];
    cmeta_data_struct_shape shape = cbind_test_one_shape;
    cmeta_data_desc data = cbind_test_one_data;
    unsigned char scratch[1] = {0};

    field.value = &custom;
    shape.fields = &field;
    data.shape = &shape;
    check_true(cmeta_data_desc_valid(&data));
    check_rejected_before_input(&data, &out, 1u, scratch, sizeof(scratch),
                                CBIND_UNSUPPORTED);
  }
}

#undef CBIND_DATA_PREFIX_SIZE
