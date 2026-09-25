#include <cmeta/data.h>
#include "cmeta_fixed_bytes_fixture.h"
#include "tinytest.h"

#include <stddef.h>

const cmeta_data_desc *cmeta_fixed_bytes_fixture_from_peer(void);
const cmeta_data_fixed_ops *cmeta_fixed_bytes_fixture_ops_from_peer(void);

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

typedef struct cmeta_data_test_fixed_storage {
    unsigned char bytes[4];
} cmeta_data_test_fixed_storage;

static bool cmeta_data_test_fixed_copy_fails;

static bool cmeta_data_test_fixed_is_zero(const void *object) {
    static const cmeta_data_test_fixed_storage zero = {{0}};
    return object != NULL && memcmp(object, &zero, sizeof(zero)) == 0;
}

static cmeta_status cmeta_data_test_fixed_copy(void *destination,
                                               const void *source) {
    if (destination == NULL || source == NULL)
        return CMETA_INVALID_ARGUMENT;
    memcpy(destination, source, sizeof(cmeta_data_test_fixed_storage));
    return cmeta_data_test_fixed_copy_fails ? CMETA_CALLBACK_ERROR : CMETA_OK;
}

static void cmeta_data_test_fixed_restore_zero(void *object) {
    if (object != NULL)
        memset(object, 0, sizeof(cmeta_data_test_fixed_storage));
}

static const cmeta_type_identity cmeta_data_test_fixed_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.Fixed4");
static const cmeta_type_desc cmeta_data_test_fixed_type = {
    .name = "cmeta_data_test_fixed_storage",
    .size = sizeof(cmeta_data_test_fixed_storage),
    .align = _Alignof(cmeta_data_test_fixed_storage),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = &cmeta_data_test_fixed_identity
};
static const cmeta_data_buffer_shape cmeta_data_test_fixed_shape = {
    .ownership = CMETA_DATA_BUFFER_OWNED
};
static const cmeta_data_fixed_ops cmeta_data_test_fixed_ops = {
    .struct_size = sizeof(cmeta_data_fixed_ops),
    .abi_version = CMETA_DATA_FIXED_OPS_ABI_VERSION,
    .storage_type = &cmeta_data_test_fixed_type,
    .extent = sizeof(cmeta_data_test_fixed_storage),
    .is_zero = cmeta_data_test_fixed_is_zero,
    .copy = cmeta_data_test_fixed_copy,
    .restore_zero = cmeta_data_test_fixed_restore_zero
};
static const cmeta_data_desc cmeta_data_test_fixed_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.Fixed4.data",
    .display_name = "Fixed4",
    .kind = CMETA_DATA_BYTES,
    .storage_type = &cmeta_data_test_fixed_type,
    .shape = &cmeta_data_test_fixed_shape,
    .fixed_ops = &cmeta_data_test_fixed_ops
};

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

static cmeta_status cmeta_data_test_buffer_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
    static const unsigned char bytes[] = {'a', 'b', 'c', 'd'};
    const int size = object != NULL ? *(const int *)object : -2;

    if (object == NULL || out_data == NULL || out_size == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (size == 13)
        return CMETA_CALLBACK_ERROR;
    if (size == -1) {
        *out_data = NULL;
        *out_size = 1u;
        return CMETA_OK;
    }
    if (size < 0 || (size_t)size > sizeof(bytes))
        return CMETA_INVALID_ARGUMENT;
    *out_data = size == 0 ? NULL : bytes;
    *out_size = (size_t)size;
    return CMETA_OK;
}

static cmeta_status cmeta_data_test_buffer_init_zero(void *object) {
    if (object == NULL)
        return CMETA_INVALID_ARGUMENT;
    *(int *)object = 0;
    return CMETA_OK;
}

static void cmeta_data_test_buffer_restore_zero(void *object) {
    if (object != NULL)
        *(int *)object = 0;
}

static void cmeta_data_test_buffer_move(void *destination, void *source) {
    if (destination == NULL || source == NULL)
        return;
    *(int *)destination = *(int *)source;
    *(int *)source = 0;
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
    .restore_zero = cmeta_data_test_buffer_restore_zero,
    .read = cmeta_data_test_buffer_read,
    .init_zero = cmeta_data_test_buffer_init_zero,
    .move = cmeta_data_test_buffer_move
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

typedef struct cmeta_data_test_owned_record {
    int payload;
    int count;
} cmeta_data_test_owned_record;

static const cmeta_type_identity cmeta_data_test_owned_record_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.OwnedRecord");
static const cmeta_type_desc cmeta_data_test_owned_record_type = {
    .name = "cmeta_data_test_owned_record",
    .size = sizeof(cmeta_data_test_owned_record),
    .align = _Alignof(cmeta_data_test_owned_record),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = &cmeta_data_test_owned_record_identity
};

static const cmeta_field_desc cmeta_data_test_owned_layout_fields[] = {
    { "payload", "int", offsetof(cmeta_data_test_owned_record, payload),
      sizeof(int), _Alignof(int), &cmeta_type_int, NULL },
    { "count", "int", offsetof(cmeta_data_test_owned_record, count),
      sizeof(int), _Alignof(int), &cmeta_type_int, NULL }
};
static const cmeta_struct_desc cmeta_data_test_owned_layout = {
    "cmeta_data_test_owned_record", sizeof(cmeta_data_test_owned_record),
    _Alignof(cmeta_data_test_owned_record),
    cmeta_data_test_owned_layout_fields, 2u
};
static const cmeta_data_field_desc cmeta_data_test_owned_fields[] = {
    { "test.OwnedRecord.payload", "payload",
      offsetof(cmeta_data_test_owned_record, payload),
      &cmeta_data_test_buffer_desc },
    { "test.OwnedRecord.count", "count",
      offsetof(cmeta_data_test_owned_record, count),
      &cmeta_data_int }
};
static const cmeta_data_struct_shape cmeta_data_test_owned_shape = {
    &cmeta_data_test_owned_layout, cmeta_data_test_owned_fields, 2u
};
static const cmeta_data_desc cmeta_data_test_owned_desc = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.OwnedRecord.data", "OwnedRecord", CMETA_DATA_STRUCT,
    &cmeta_data_test_owned_record_type, &cmeta_data_test_owned_shape
};

typedef struct cmeta_data_test_outer_record {
    cmeta_data_test_record inner;
    int tail;
} cmeta_data_test_outer_record;

static const cmeta_type_identity cmeta_data_test_outer_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.OuterRecord");
static const cmeta_type_desc cmeta_data_test_outer_type = {
    .name = "cmeta_data_test_outer_record",
    .size = sizeof(cmeta_data_test_outer_record),
    .align = _Alignof(cmeta_data_test_outer_record),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = &cmeta_data_test_outer_identity
};
static const cmeta_field_desc cmeta_data_test_outer_layout_fields[] = {
    { "inner", "cmeta_data_test_record",
      offsetof(cmeta_data_test_outer_record, inner),
      sizeof(cmeta_data_test_record), _Alignof(cmeta_data_test_record),
      &cmeta_data_test_record_type, NULL },
    { "tail", "int", offsetof(cmeta_data_test_outer_record, tail),
      sizeof(int), _Alignof(int), &cmeta_type_int, NULL }
};
static const cmeta_struct_desc cmeta_data_test_outer_layout = {
    "cmeta_data_test_outer_record", sizeof(cmeta_data_test_outer_record),
    _Alignof(cmeta_data_test_outer_record),
    cmeta_data_test_outer_layout_fields, 2u
};
static const cmeta_data_field_desc cmeta_data_test_outer_fields[] = {
    { "test.OuterRecord.inner", "inner",
      offsetof(cmeta_data_test_outer_record, inner),
      &cmeta_data_test_record_desc },
    { "test.OuterRecord.tail", "tail",
      offsetof(cmeta_data_test_outer_record, tail),
      &cmeta_data_int }
};
static const cmeta_data_struct_shape cmeta_data_test_outer_shape = {
    &cmeta_data_test_outer_layout, cmeta_data_test_outer_fields, 2u
};
static const cmeta_data_desc cmeta_data_test_outer_desc = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.OuterRecord.data", "OuterRecord", CMETA_DATA_STRUCT,
    &cmeta_data_test_outer_type, &cmeta_data_test_outer_shape
};

static const int cmeta_data_test_failing_shape = 1;

static cmeta_status cmeta_data_test_failing_init(void *object) {
    if (object == NULL) return CMETA_INVALID_ARGUMENT;
    *(int *)object = 99;
    return CMETA_CALLBACK_ERROR;
}
static void cmeta_data_test_failing_restore(void *object) {
    if (object != NULL) *(int *)object = 0;
}
static void cmeta_data_test_failing_move(void *destination, void *source) {
    if (destination == NULL || source == NULL) return;
    *(int *)destination = *(int *)source;
    *(int *)source = 0;
}
static const cmeta_data_construct_ops cmeta_data_test_failing_construct = {
    sizeof(cmeta_data_construct_ops), CMETA_DATA_CONSTRUCT_OPS_ABI_VERSION,
    &cmeta_type_int, cmeta_data_test_failing_init,
    cmeta_data_test_failing_restore, cmeta_data_test_failing_move
};
static const cmeta_data_desc cmeta_data_test_failing_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.Failing.data", "Failing", CMETA_DATA_CUSTOM,
    &cmeta_type_int, &cmeta_data_test_failing_shape,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, &cmeta_data_test_failing_construct
};
static const cmeta_data_field_desc cmeta_data_test_rollback_fields[] = {
    { "test.Rollback.payload", "payload",
      offsetof(cmeta_data_test_owned_record, payload),
      &cmeta_data_test_buffer_desc },
    { "test.Rollback.count", "count",
      offsetof(cmeta_data_test_owned_record, count),
      &cmeta_data_test_failing_data }
};
static const cmeta_data_struct_shape cmeta_data_test_rollback_shape = {
    &cmeta_data_test_owned_layout, cmeta_data_test_rollback_fields, 2u
};
static const cmeta_data_desc cmeta_data_test_rollback_desc = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.Rollback.data", "Rollback", CMETA_DATA_STRUCT,
    &cmeta_data_test_owned_record_type, &cmeta_data_test_rollback_shape
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

typedef struct cmeta_data_test_int_sequence {
  int values[2];
} cmeta_data_test_int_sequence;

static const cmeta_type_identity cmeta_data_test_int_sequence_id =
    CMETA_TYPE_ID_ATOM_INIT("test.IntSequence");
static const cmeta_type_desc cmeta_data_test_int_sequence_type = {
    "IntSequence", sizeof(cmeta_data_test_int_sequence),
    CMETA_ALIGNOF(cmeta_data_test_int_sequence), CMETA_T_OBJECT,
    NULL, NULL, &cmeta_data_test_int_sequence_id};

static const cmeta_data_desc *cmeta_data_test_int_sequence_element(
    const void *object) {
  return object != NULL ? &cmeta_data_int : NULL;
}

static cmeta_status cmeta_data_test_int_sequence_read(
    const void *object, cmeta_data_collection_view *out) {
  const cmeta_data_test_int_sequence *sequence =
      (const cmeta_data_test_int_sequence *)object;
  if (sequence == NULL || out == NULL) return CMETA_INVALID_ARGUMENT;
  *out = (cmeta_data_collection_view){
      sequence->values, 2u, sizeof(sequence->values[0]), &cmeta_data_int};
  return CMETA_OK;
}

static const cmeta_data_collection_ops cmeta_data_test_int_sequence_ops = {
    sizeof(cmeta_data_collection_ops),
    CMETA_DATA_COLLECTION_OPS_ABI_VERSION,
    &cmeta_data_test_int_sequence_type,
    CMETA_DATA_COLLECTION_CONTIGUOUS | CMETA_DATA_COLLECTION_ORDERED |
        CMETA_DATA_COLLECTION_RANDOM_ACCESS,
    cmeta_data_test_int_sequence_element,
    cmeta_data_test_int_sequence_read,
    NULL,
    NULL};

static const cmeta_data_desc cmeta_data_test_int_sequence_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.IntSequence.data", "IntSequence", CMETA_DATA_SEQUENCE,
    &cmeta_data_test_int_sequence_type, NULL, NULL, NULL, NULL, NULL, NULL,
    &cmeta_data_test_int_sequence_ops};

spec("CMeta semantic data descriptors") {
  it("declares bounded fixed bytes as canonical provider metadata") {
    const cmeta_fixed_bytes_fixture source = {1u, 2u, 3u, 4u, 5u, 6u};
    cmeta_fixed_bytes_fixture destination = {0};
    const cmeta_data_desc *peer = cmeta_fixed_bytes_fixture_from_peer();
    size_t extent = 0u;

    check_true(cmeta_data_desc_valid(peer));
    check_true(cmeta_type_equal(peer->storage_type,
                                &cmeta_fixed_bytes_fixture_value_cmeta_type));
    check_true(peer != &cmeta_fixed_bytes_fixture_value_cmeta_data);
    check_true(cmeta_fixed_bytes_fixture_ops_from_peer() !=
               &cmeta_fixed_bytes_fixture_value_cmeta_fixed_ops);
    check_equal(cmeta_data_fixed_extent(peer, &extent), CMETA_OK);
    check_equal(extent, sizeof(cmeta_fixed_bytes_fixture));
    check_equal(cmeta_data_fixed_copy(peer, &destination, &source,
                                      sizeof(source)), CMETA_OK);
    check_equal(destination, source, sizeof(source));
    check_equal(cmeta_data_fixed_restore_zero(peer, &destination), CMETA_OK);
  }

  it("copies exact fixed native values through explicit provider authority") {
    const cmeta_data_test_fixed_storage source = {{1u, 2u, 3u, 4u}};
    cmeta_data_test_fixed_storage destination = {{0}};
    size_t extent = 0u;
    bool is_zero = false;

    cmeta_data_test_fixed_copy_fails = false;
    check_true(cmeta_data_fixed_ops_of(&cmeta_data_test_fixed_desc) ==
               &cmeta_data_test_fixed_ops);
    check_equal(cmeta_data_fixed_extent(&cmeta_data_test_fixed_desc, &extent),
                CMETA_OK);
    check_equal(extent, sizeof(source));
    check_equal(cmeta_data_fixed_is_zero(&cmeta_data_test_fixed_desc,
                                         &destination, &is_zero), CMETA_OK);
    check_true(is_zero);
    check_equal(cmeta_data_fixed_copy(&cmeta_data_test_fixed_desc,
                                      &destination, &source, sizeof(source)),
                CMETA_OK);
    check_equal(destination.bytes, source.bytes, sizeof(source.bytes));
    check_equal(cmeta_data_fixed_restore_zero(&cmeta_data_test_fixed_desc,
                                              &destination), CMETA_OK);
    check_true(cmeta_data_test_fixed_is_zero(&destination));
  }

  it("rejects fixed-value extent and descriptor mismatches without inference") {
    const cmeta_data_test_fixed_storage source = {{1u, 2u, 3u, 4u}};
    cmeta_data_test_fixed_storage destination = {{0}};
    cmeta_data_fixed_ops ops = cmeta_data_test_fixed_ops;
    cmeta_data_desc desc = cmeta_data_test_fixed_desc;
    size_t extent = 99u;

    check_equal(cmeta_data_fixed_copy(&desc, &destination, &source,
                                      sizeof(source) - 1u),
                CMETA_TYPE_MISMATCH);
    check_true(cmeta_data_test_fixed_is_zero(&destination));

    ops.abi_version += 1u;
    desc.fixed_ops = &ops;
    check_null(cmeta_data_fixed_ops_of(&desc));
    check_equal(cmeta_data_fixed_extent(&desc, &extent),
                CMETA_INVALID_ARGUMENT);
    check_equal(extent, (size_t)99u);

    ops = cmeta_data_test_fixed_ops;
    ops.storage_type = &cmeta_type_int;
    desc.fixed_ops = &ops;
    check_null(cmeta_data_fixed_ops_of(&desc));
    check_equal(cmeta_data_fixed_extent(&desc, &extent),
                CMETA_TYPE_MISMATCH);

    ops = cmeta_data_test_fixed_ops;
    ops.extent -= 1u;
    desc.fixed_ops = &ops;
    check_null(cmeta_data_fixed_ops_of(&desc));
    check_equal(cmeta_data_fixed_extent(&desc, &extent),
                CMETA_TYPE_MISMATCH);

    ops = cmeta_data_test_fixed_ops;
    ops.copy = NULL;
    desc.fixed_ops = &ops;
    check_null(cmeta_data_fixed_ops_of(&desc));
    check_equal(cmeta_data_fixed_extent(&desc, &extent),
                CMETA_INVALID_ARGUMENT);

    desc = cmeta_data_test_fixed_desc;
    desc.struct_size = offsetof(cmeta_data_desc, fixed_ops);
    check_null(cmeta_data_fixed_ops_of(&desc));
  }

  it("rolls failed fixed-value copies back to provider semantic zero") {
    const cmeta_data_test_fixed_storage source = {{1u, 2u, 3u, 4u}};
    cmeta_data_test_fixed_storage destination = {{0}};

    cmeta_data_test_fixed_copy_fails = true;
    check_equal(cmeta_data_fixed_copy(&cmeta_data_test_fixed_desc,
                                      &destination, &source, sizeof(source)),
                CMETA_CALLBACK_ERROR);
    check_true(cmeta_data_test_fixed_is_zero(&destination));
    cmeta_data_test_fixed_copy_fails = false;

    destination.bytes[0] = 9u;
    check_equal(cmeta_data_fixed_copy(&cmeta_data_test_fixed_desc,
                                      &destination, &source, sizeof(source)),
                CMETA_INVALID_ARGUMENT);
    check_equal(destination.bytes[0], (unsigned char)9u);
  }

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

  it("reads a bounded borrowed buffer view") {
    static const unsigned char expected[] = {'a', 'b', 'c'};
    const unsigned char *data = NULL;
    size_t size = 0u;
    const int object = 3;

    check_equal(cmeta_data_buffer_read(&cmeta_data_test_buffer_desc, &object,
                                       sizeof(expected), &data, &size),
                CMETA_OK);
    check_equal(size, sizeof(expected));
    check_not_null(data);
    check_equal(data, expected, sizeof(expected));
  }

  it("reads an empty buffer as a valid empty view") {
    static const unsigned char sentinel[] = {'x'};
    const unsigned char *data = sentinel;
    size_t size = 9u;
    const int object = 0;

    check_equal(cmeta_data_buffer_read(&cmeta_data_test_buffer_desc, &object,
                                       0u, &data, &size),
                CMETA_OK);
    check_null(data);
    check_equal(size, (size_t)0u);
  }

  it("preserves read outputs when the byte ceiling is exceeded") {
    static const unsigned char sentinel[] = {'x'};
    const unsigned char *data = sentinel;
    size_t size = 9u;
    const int object = 3;

    check_equal(cmeta_data_buffer_read(&cmeta_data_test_buffer_desc, &object,
                                       2u, &data, &size),
                CMETA_CAPACITY_EXCEEDED);
    check_true(data == sentinel);
    check_equal(size, (size_t)9u);
  }

  it("rejects a malformed provider view without publishing it") {
    static const unsigned char sentinel[] = {'x'};
    const unsigned char *data = sentinel;
    size_t size = 9u;
    const int object = -1;

    check_equal(cmeta_data_buffer_read(&cmeta_data_test_buffer_desc, &object,
                                       1u, &data, &size),
                CMETA_CALLBACK_ERROR);
    check_true(data == sentinel);
    check_equal(size, (size_t)9u);
  }

  it("preserves read outputs when the provider fails") {
    static const unsigned char sentinel[] = {'x'};
    const unsigned char *data = sentinel;
    size_t size = 9u;
    const int object = 13;

    check_equal(cmeta_data_buffer_read(&cmeta_data_test_buffer_desc, &object,
                                       13u, &data, &size),
                CMETA_CALLBACK_ERROR);
    check_true(data == sentinel);
    check_equal(size, (size_t)9u);
  }

  it("reports a missing buffer read trait without weakening v2 lifecycle") {
    static const unsigned char sentinel[] = {'x'};
    cmeta_data_buffer_ops ops = cmeta_data_test_buffer_ops;
    cmeta_data_desc desc = cmeta_data_test_buffer_desc;
    const unsigned char *data = sentinel;
    size_t size = 9u;
    const int object = 3;

    ops.read = NULL;
    desc.buffer_ops = &ops;
    check_true(cmeta_data_buffer_ops_of(&desc) == &ops);
    check_equal(cmeta_data_buffer_read(&desc, &object, 3u, &data, &size),
                CMETA_TRAIT_MISSING);
    check_true(data == sentinel);
    check_equal(size, (size_t)9u);
  }

  it("rejects invalid buffer read arguments without publishing outputs") {
    static const unsigned char sentinel[] = {'x'};
    const unsigned char *data = sentinel;
    size_t size = 9u;
    const int object = 3;

    check_equal(cmeta_data_buffer_read(NULL, &object, 3u, &data, &size),
                CMETA_INVALID_ARGUMENT);
    check_equal(cmeta_data_buffer_read(&cmeta_data_test_buffer_desc, NULL, 3u,
                                       &data, &size),
                CMETA_INVALID_ARGUMENT);
    check_equal(cmeta_data_buffer_read(&cmeta_data_test_buffer_desc, &object,
                                       3u, NULL, &size),
                CMETA_INVALID_ARGUMENT);
    check_equal(cmeta_data_buffer_read(&cmeta_data_test_buffer_desc, &object,
                                       3u, &data, NULL),
                CMETA_INVALID_ARGUMENT);
    check_true(data == sentinel);
    check_equal(size, (size_t)9u);
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

    object = CMETA_ENUM_FROM_INT64(cmeta_data_test_state, 99);
    check_equal(cmeta_data_enum_read(&desc, &object, &value),
                CMETA_CALLBACK_ERROR);
    check_equal(cmeta_data_enum_restore_zero(&desc, &object), CMETA_OK);

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

    object.tag = 99;
    check_equal(cmeta_data_variant_active_tag(&desc, &object, &tag),
                CMETA_CALLBACK_ERROR);
    check_equal(cmeta_data_variant_restore_zero(&desc, &object), CMETA_OK);

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

  it("owns exact-width integer semantic identity in CMeta core") {
    check_true(cmeta_data_integer_width(true, 8u) == &cmeta_data_int8);
    check_true(cmeta_data_integer_width(false, 8u) == &cmeta_data_uint8);
    check_true(cmeta_data_integer_width(true, 16u) == &cmeta_data_int16);
    check_true(cmeta_data_integer_width(false, 16u) == &cmeta_data_uint16);
    check_true(cmeta_data_integer_width(true, 32u) == &cmeta_data_int32);
    check_true(cmeta_data_integer_width(false, 32u) == &cmeta_data_uint32);
    check_true(cmeta_data_integer_width(true, 64u) == &cmeta_data_int64);
    check_true(cmeta_data_integer_width(false, 64u) == &cmeta_data_uint64);
    check_null(cmeta_data_integer_width(true, 7u));
    check_true(cmeta_data_int8.storage_type == &cmeta_type_int8);
    check_true(cmeta_data_uint64.storage_type == &cmeta_type_uint64);
  }

  it("derives transactional lifecycle for trivial reflected structs") {
    cmeta_data_test_record source = {7, 9};
    cmeta_data_test_record destination = {0, 0};

    check_true(cmeta_data_struct_constructible(&cmeta_data_test_record_desc));
    check_true(cmeta_data_value_move_supported(&cmeta_data_test_record_desc));
    check_equal(cmeta_data_value_init_zero(
                    &cmeta_data_test_record_desc, &destination),
                CMETA_OK);
    check_equal(cmeta_data_value_move(
                    &cmeta_data_test_record_desc, &destination, &source),
                CMETA_OK);
    check_equal(destination.id, 7);
    check_equal(destination.score, 9);
    check_equal(source.id, 0);
    check_equal(source.score, 0);
  }

  it("derives transactional lifecycle for structs with owned fields") {
    cmeta_data_test_owned_record source = {0, 0};
    cmeta_data_test_owned_record destination = {0, 0};
    static const unsigned char bytes[] = {'a', 'b', 'c'};

    check_true(cmeta_data_struct_constructible(&cmeta_data_test_owned_desc));
    check_equal(cmeta_data_value_init_zero(
                    &cmeta_data_test_owned_desc, &source), CMETA_OK);
    check_equal(cmeta_data_value_init_zero(
                    &cmeta_data_test_owned_desc, &destination), CMETA_OK);
    check_equal(cmeta_data_buffer_assign(
                    &cmeta_data_test_buffer_desc, &source.payload,
                    bytes, sizeof(bytes), sizeof(bytes)), CMETA_OK);
    source.count = 5;
    check_equal(cmeta_data_value_move(
                    &cmeta_data_test_owned_desc, &destination, &source),
                CMETA_OK);
    check_equal(destination.payload, 3);
    check_equal(destination.count, 5);
    check_equal(source.payload, 0);
    check_equal(source.count, 0);
    check_equal(cmeta_data_value_restore_zero(
                    &cmeta_data_test_owned_desc, &destination), CMETA_OK);
    check_equal(destination.payload, 0);
    check_equal(destination.count, 0);
  }

  it("derives lifecycle recursively through nested structs") {
    cmeta_data_test_outer_record source = {{3, 4}, 5};
    cmeta_data_test_outer_record destination = {{0, 0}, 0};

    check_true(cmeta_data_struct_constructible(&cmeta_data_test_outer_desc));
    check_equal(cmeta_data_value_init_zero(
                    &cmeta_data_test_outer_desc, &destination),
                CMETA_OK);
    check_equal(cmeta_data_value_move(
                    &cmeta_data_test_outer_desc, &destination, &source),
                CMETA_OK);
    check_equal(destination.inner.id, 3);
    check_equal(destination.inner.score, 4);
    check_equal(destination.tail, 5);
    check_equal(source.inner.id, 0);
    check_equal(source.inner.score, 0);
    check_equal(source.tail, 0);
  }

  it("rolls back earlier owned fields when a later field init fails") {
    cmeta_data_test_owned_record value = {42, 77};

    check_true(cmeta_data_struct_constructible(&cmeta_data_test_rollback_desc));
    check_equal(cmeta_data_value_init_zero(
                    &cmeta_data_test_rollback_desc, &value),
                CMETA_CALLBACK_ERROR);
    check_equal(value.payload, 0);
    check_equal(value.count, 0);
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

  it("validates provider-neutral collection read views") {
    cmeta_data_test_int_sequence value = {{3, 5}};
    cmeta_data_collection_view view = {0};

    check_true(cmeta_data_collection_ops_of(
                   &cmeta_data_test_int_sequence_data) ==
               &cmeta_data_test_int_sequence_ops);
    check_equal(cmeta_data_collection_read(
                    &cmeta_data_test_int_sequence_data, &value, &view),
                CMETA_OK);
    check_equal(view.count, 2u);
    check_equal(view.stride, sizeof(int));
    check_true(view.element == &cmeta_data_int);
    check_equal(*(const int *)view.data, 3);
  }
}
