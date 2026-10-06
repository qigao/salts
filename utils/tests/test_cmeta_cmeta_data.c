#include "cmeta_cmeta_data.h"
#include <cmeta/fixed_array.h>
#include "tstr.h"
#include "vstr.h"
#include "tinytest.h"

#include <stdint.h>
#include <string.h>

enum { BOOL8_ARRAY_COUNT = 2u };
typedef uint8_t Bool8Array[BOOL8_ARRAY_COUNT];
CMETA_DEFINE_FIXED_ARRAY(Bool8ArrayProvider, Bool8Array, uint8_t, BOOL8_ARRAY_COUNT,
                         &cmeta_bool8_cmeta_data, "test.bool8.array", "Bool8 array");

const cmeta_data_desc *cmeta_uuid_cmeta_data_from_peer(void);
const cmeta_type_desc *cmeta_uuid_cmeta_type_from_peer(void);
const cmeta_data_buffer_shape *cmeta_uuid_cmeta_shape_from_peer(void);
const cmeta_data_buffer_ops *cmeta_uuid_cmeta_buffer_ops_from_peer(void);
const cmeta_data_fixed_ops *cmeta_uuid_cmeta_fixed_ops_from_peer(void);

_Static_assert(
    _Generic(&(cmeta_uuid_cmeta_buffer_ops),
             const cmeta_data_buffer_ops *: 1,
             default: 0),
    "UUID adapter preserves the public address type");
_Static_assert(sizeof(cmeta_uuid_cmeta_buffer_ops) ==
                   sizeof(cmeta_data_buffer_ops),
               "UUID adapter preserves its public object sizeof");
_Static_assert(
    _Generic(&(cmeta_uuid_cmeta_fixed_ops),
             const cmeta_data_fixed_ops *: 1,
             default: 0),
    "UUID fixed-value provider preserves the public address type");
_Static_assert(
    _Generic(&cmeta_uuid_cmeta_type, const cmeta_type_desc *: 1,
             default: 0) &&
        _Generic(&cmeta_uuid_cmeta_shape,
                 const cmeta_data_buffer_shape *: 1, default: 0) &&
        _Generic(&cmeta_uuid_cmeta_data, const cmeta_data_desc *: 1,
                 default: 0),
    "UUID metadata preserves its public const object types");

static bool replaced_uuid_is_zero(const void *object) {
  (void)object;
  return true;
}

static cmeta_status replaced_uuid_assign(void *object,
                                         const unsigned char *data,
                                         size_t size,
                                         size_t max_bytes) {
  (void)object;
  (void)data;
  (void)size;
  (void)max_bytes;
  return CMETA_OK;
}

static void replaced_uuid_restore_zero(void *object) {
  (void)object;
}

static const cmeta_data_buffer_shape owned_shape = {
    .ownership = CMETA_DATA_BUFFER_OWNED
};
static const cmeta_data_buffer_shape borrowed_shape = {
    .ownership = CMETA_DATA_BUFFER_BORROWED
};

static cmeta_data_desc tstr_bytes;

static void bind_tstr_bytes(void) {
    tstr_bytes = cmeta_tstr_cmeta_data;
    tstr_bytes.stable_id = "test.tstr.bytes";
    tstr_bytes.display_name = "tstr bytes";
    tstr_bytes.kind = CMETA_DATA_BYTES;
    tstr_bytes.shape = &owned_shape;
}

static const cmeta_data_desc vstr_string = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.vstr.string",
    .display_name = "vstr string",
    .kind = CMETA_DATA_STRING,
    .storage_type = &cmeta_vstr_cmeta_type,
    .shape = &borrowed_shape,
    .buffer_ops = &cmeta_vstr_cmeta_buffer_ops
};


typedef struct owned_tstr_record {
  tstr first;
  tstr second;
  int marker;
} owned_tstr_record;

static const cmeta_type_identity owned_tstr_record_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.salts.owned-tstr-record");

static const cmeta_type_desc owned_tstr_record_type = {
    "owned_tstr_record", sizeof(owned_tstr_record),
    CMETA_ALIGNOF(owned_tstr_record), CMETA_T_OBJECT,
    NULL, NULL, &owned_tstr_record_identity
};

static const cmeta_field_desc owned_tstr_record_layout_fields[] = {
    {"first", "tstr", offsetof(owned_tstr_record, first),
     sizeof(tstr), CMETA_ALIGNOF(tstr), SALTS_TSTR_CMETA_TYPE_REF, NULL},
    {"second", "tstr", offsetof(owned_tstr_record, second),
     sizeof(tstr), CMETA_ALIGNOF(tstr), SALTS_TSTR_CMETA_TYPE_REF, NULL},
    {"marker", "int", offsetof(owned_tstr_record, marker),
     sizeof(int), CMETA_ALIGNOF(int), &cmeta_type_int, NULL}
};

static const cmeta_struct_desc owned_tstr_record_layout = {
    "owned_tstr_record", sizeof(owned_tstr_record),
    CMETA_ALIGNOF(owned_tstr_record),
    owned_tstr_record_layout_fields,
    sizeof(owned_tstr_record_layout_fields) /
        sizeof(owned_tstr_record_layout_fields[0])
};

static const cmeta_data_field_desc owned_tstr_record_fields[] = {
    {"test.salts.owned-tstr-record.first", "first",
     offsetof(owned_tstr_record, first), SALTS_TSTR_CMETA_DATA_REF},
    {"test.salts.owned-tstr-record.second", "second",
     offsetof(owned_tstr_record, second), SALTS_TSTR_CMETA_DATA_REF},
    {"test.salts.owned-tstr-record.marker", "marker",
     offsetof(owned_tstr_record, marker), &cmeta_data_int}
};

static const cmeta_data_struct_shape owned_tstr_record_shape = {
    .layout = &owned_tstr_record_layout,
    .fields = owned_tstr_record_fields,
    .field_count = sizeof(owned_tstr_record_fields) /
                   sizeof(owned_tstr_record_fields[0])
};

static const cmeta_data_desc owned_tstr_record_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.salts.owned-tstr-record.data",
    .display_name = "owned tstr record",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &owned_tstr_record_type,
    .shape = &owned_tstr_record_shape
};

spec("Salts CMeta buffer adapters") {
  before_each() {
    bind_tstr_bytes();
  }

  it("composes owned tstr lifetime through struct data traits") {
    static const unsigned char first_text[] = "invoke-250";
    static const unsigned char second_text[] = "send-577";
    owned_tstr_record source = {0};
    owned_tstr_record copied = {0};
    owned_tstr_record moved = {0};

    check_true(cmeta_data_value_traits_supported(&owned_tstr_record_data));
    check_equal(cmeta_data_value_init_zero(&owned_tstr_record_data, &source),
                CMETA_OK);
    check_equal(cmeta_data_buffer_assign(
                    SALTS_TSTR_CMETA_DATA_REF, &source.first,
                    first_text, sizeof(first_text) - 1u,
                    sizeof(first_text) - 1u),
                CMETA_OK);
    check_equal(cmeta_data_buffer_assign(
                    SALTS_TSTR_CMETA_DATA_REF, &source.second,
                    second_text, sizeof(second_text) - 1u,
                    sizeof(second_text) - 1u),
                CMETA_OK);
    source.marker = 577;

    check_true(cmeta_data_trait_copy_construct(
        &owned_tstr_record_data, &copied, &source));
    check_not_null(copied.first);
    check_not_null(copied.second);
    check_true(copied.first != source.first);
    check_true(copied.second != source.second);
    check_equal(copied.marker, 577);

    cmeta_data_trait_move_construct(
        &owned_tstr_record_data, &moved, &copied);
    check_null(copied.first);
    check_null(copied.second);
    check_equal(copied.marker, 0);
    check_not_null(moved.first);
    check_not_null(moved.second);
    check_equal(moved.marker, 577);

    cmeta_data_trait_destroy(&owned_tstr_record_data, &moved);
    check_null(moved.first);
    check_null(moved.second);
    check_equal(moved.marker, 0);

    cmeta_data_trait_destroy(&owned_tstr_record_data, &source);
    check_null(source.first);
    check_null(source.second);
    check_equal(source.marker, 0);
  }

  it("publishes data-backed COPY MOVE DESTROY traits for tstr") {
    tstr source = tstr_dup("managed");
    tstr copied;
    tstr moved;
    const cmeta_type_traits *traits = cmeta_tstr_cmeta_type.traits;

    check_not_null(source);
    check_true(cmeta_data_value_traits_supported(&cmeta_tstr_cmeta_data));
    check_not_null(traits);
    check_true((traits->flags &
                (CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY)) ==
               (CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY));
    check_not_null(traits->copy_construct);
    check_not_null(traits->move_construct);
    check_not_null(traits->destroy);

    check_true(traits->copy_construct(&copied, &source));
    check_not_null(copied);
    check_true(copied != source);
    check_equal(tstr_len(copied), tstr_len(source));
    check_equal(memcmp(copied, source, tstr_len(source)), 0);

    traits->move_construct(&moved, &copied);
    check_null(copied);
    check_not_null(moved);
    check_equal(memcmp(moved, source, tstr_len(source)), 0);

    traits->destroy(&moved);
    check_null(moved);
    traits->destroy(&source);
    check_null(source);
  }

  it("copies exact owned tstr bytes including embedded NUL") {
    static const unsigned char input[] = {'a', 0, 'b'};
    const unsigned char *view = NULL;
    size_t view_size = 0u;
    tstr value = NULL;
    bool is_zero = false;

    check_equal(cmeta_data_buffer_assign(&tstr_bytes, &value, input,
                                         sizeof(input), sizeof(input)),
                CMETA_OK);
    check_not_null(value);
    check_equal(tstr_len(value), sizeof(input));
    check_equal(memcmp(value, input, sizeof(input)), 0);
    check_equal(cmeta_data_buffer_read(&tstr_bytes, &value, sizeof(input),
                                       &view, &view_size),
                CMETA_OK);
    check_true(view == (const unsigned char *)value);
    check_equal(view_size, sizeof(input));
    check_equal(view, input, sizeof(input));
    check_equal(cmeta_data_buffer_is_zero(&tstr_bytes, &value, &is_zero),
                CMETA_OK);
    check_false(is_zero);

    check_equal(cmeta_data_buffer_restore_zero(&tstr_bytes, &value), CMETA_OK);
    check_null(value);
  }

  it("keeps owned tstr zero for empty input and failed allocation") {
    static const unsigned char byte = 0;
    const unsigned char *view = &byte;
    size_t view_size = 1u;
    tstr value = NULL;

    check_equal(cmeta_data_buffer_assign(&tstr_bytes, &value, NULL, 0u, 0u),
                CMETA_OK);
    check_null(value);
    check_equal(cmeta_data_buffer_read(&tstr_bytes, &value, 0u, &view,
                                       &view_size),
                CMETA_OK);
    check_null(view);
    check_equal(view_size, (size_t)0u);

    check_equal(cmeta_data_buffer_assign(&tstr_bytes, &value, &byte,
                                         SIZE_MAX, SIZE_MAX),
                CMETA_OUT_OF_MEMORY);
    check_null(value);
  }

  it("borrows vstr input without copying and restores canonical zero") {
    static const unsigned char input[] = {'u', 't', 'f', '8'};
    const unsigned char *view = NULL;
    size_t view_size = 0u;
    vstr value = {NULL, 0u};

    check_equal(cmeta_data_buffer_assign(&vstr_string, &value, input,
                                         sizeof(input), sizeof(input)),
                CMETA_OK);
    check_true(value.data == (const char *)input);
    check_equal(value.len, sizeof(input));
    check_equal(cmeta_data_buffer_read(&vstr_string, &value, sizeof(input),
                                       &view, &view_size),
                CMETA_OK);
    check_true(view == input);
    check_equal(view_size, sizeof(input));

    check_equal(cmeta_data_buffer_restore_zero(&vstr_string, &value),
                CMETA_OK);
    check_null(value.data);
    check_equal(value.len, (size_t)0u);
  }

  it("canonicalizes an empty borrowed view") {
    static const unsigned char sentinel = 0;
    vstr value = {NULL, 0u};

    check_equal(cmeta_data_buffer_assign(&vstr_string, &value, &sentinel,
                                         0u, 0u), CMETA_OK);
    check_null(value.data);
    check_equal(value.len, (size_t)0u);
  }
}

spec("Salts fixed-width CMeta descriptors") {
  it("copies moves and collects octet Bool arrays through canonical owning traits") {
    Bool8Array source = {1u, 0u};
    Bool8Array copy = {0u};
    Bool8Array moved = {0u};
    cmeta_collector collector = {0};
    const uint8_t one = 1u;
    const uint8_t zero = 0u;
    bool is_zero = false;
    check_equal(cmeta_type_require_traits(&cmeta_bool8_cmeta_type,
                CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY), CMETA_OK);
    check_true(cmeta_data_value_copy_supported(&Bool8ArrayProvider_cmeta_data));
    check_equal(cmeta_data_value_copy(&Bool8ArrayProvider_cmeta_data, copy, source), CMETA_OK);
    check_equal(copy, source, sizeof(source));
    check_equal(cmeta_data_value_move(&Bool8ArrayProvider_cmeta_data, moved, copy), CMETA_OK);
    check_equal(moved, source, sizeof(source));
    check_equal(cmeta_data_value_is_zero(&Bool8ArrayProvider_cmeta_data, copy, &is_zero), CMETA_OK);
    check_true(is_zero);
    check_equal(cmeta_data_collection_collector(&Bool8ArrayProvider_cmeta_data, copy,
                BOOL8_ARRAY_COUNT, &collector), CMETA_OK);
    check_equal(cmeta_collector_begin(&collector), CMETA_OK);
    check_equal(cmeta_data_collection_accept(&Bool8ArrayProvider_cmeta_data, &collector,
                &cmeta_bool8_cmeta_data, &one), CMETA_OK);
    check_equal(cmeta_data_collection_accept(&Bool8ArrayProvider_cmeta_data, &collector,
                &cmeta_bool8_cmeta_data, &zero), CMETA_OK);
    check_equal(cmeta_collector_finish(&collector), CMETA_OK);
    check_equal(copy, source, sizeof(source));
    check_equal(cmeta_data_value_restore_zero(&Bool8ArrayProvider_cmeta_data, moved), CMETA_OK);
    check_equal(cmeta_data_value_restore_zero(&Bool8ArrayProvider_cmeta_data, copy), CMETA_OK);
    source[1] = 2u;
    check_equal(cmeta_data_value_copy(&Bool8ArrayProvider_cmeta_data, copy, source), CMETA_INVALID_ARGUMENT);
    check_equal(cmeta_data_value_is_zero(&Bool8ArrayProvider_cmeta_data, copy, &is_zero), CMETA_OK);
    check_true(is_zero);
  }

  it("provides an octet-backed Bool with native fixed-value semantics") {
    const uint8_t true_octet = 1u;
    const uint8_t invalid_octet = 2u;
    uint8_t value = 0u;
    bool is_zero = false;
    size_t extent = 0u;

    check_equal(cmeta_bool8_cmeta_data.kind, CMETA_DATA_BOOL);
    check_equal(cmeta_bool8_cmeta_data.storage_type->size, sizeof(uint8_t));
    check_true(cmeta_bool8_cmeta_data.fixed_ops ==
               &cmeta_bool8_cmeta_fixed_ops);
    check_equal(cmeta_data_fixed_extent(&cmeta_bool8_cmeta_data, &extent),
                CMETA_OK);
    check_equal(extent, sizeof(uint8_t));
    check_equal(cmeta_data_fixed_copy(&cmeta_bool8_cmeta_data, &value,
                                      &true_octet, sizeof(true_octet)),
                CMETA_OK);
    check_equal(value, (uint8_t)1u);
    check_equal(cmeta_data_fixed_restore_zero(&cmeta_bool8_cmeta_data, &value),
                CMETA_OK);
    check_equal(cmeta_data_fixed_is_zero(&cmeta_bool8_cmeta_data, &value,
                                         &is_zero), CMETA_OK);
    check_true(is_zero);
    check_equal(cmeta_data_fixed_copy(&cmeta_bool8_cmeta_data, &value,
                                      &invalid_octet,
                                      sizeof(invalid_octet)),
                CMETA_INVALID_ARGUMENT);
    check_equal(value, (uint8_t)0u);
  }

  it("describes every signed width with exact storage ABI") {
    const cmeta_data_desc *const values[] = {
        &cmeta_data_int8, &cmeta_data_int16,
        &cmeta_data_int32, &cmeta_data_int64
    };
    const size_t sizes[] = {
        sizeof(int8_t), sizeof(int16_t), sizeof(int32_t), sizeof(int64_t)
    };
    const size_t alignments[] = {
        _Alignof(int8_t), _Alignof(int16_t),
        _Alignof(int32_t), _Alignof(int64_t)
    };
    const uint8_t bits[] = {8u, 16u, 32u, 64u};
    size_t i;

    for (i = 0u; i < sizeof(values) / sizeof(values[0]); ++i) {
      const cmeta_data_integer_shape *shape =
          (const cmeta_data_integer_shape *)values[i]->shape;
      check_true(cmeta_data_desc_valid(values[i]));
      check_equal(values[i]->kind, CMETA_DATA_SINT);
      check_equal(values[i]->storage_type->kind, CMETA_T_INTEGER);
      check_equal(values[i]->storage_type->size, sizes[i]);
      check_equal(values[i]->storage_type->align, alignments[i]);
      check_equal(shape->bits, bits[i]);
    }
  }

  it("describes every unsigned width with exact storage ABI") {
    const cmeta_data_desc *const values[] = {
        &cmeta_data_uint8, &cmeta_data_uint16,
        &cmeta_data_uint32, &cmeta_data_uint64
    };
    const size_t sizes[] = {
        sizeof(uint8_t), sizeof(uint16_t), sizeof(uint32_t), sizeof(uint64_t)
    };
    const size_t alignments[] = {
        _Alignof(uint8_t), _Alignof(uint16_t),
        _Alignof(uint32_t), _Alignof(uint64_t)
    };
    const uint8_t bits[] = {8u, 16u, 32u, 64u};
    size_t i;

    for (i = 0u; i < sizeof(values) / sizeof(values[0]); ++i) {
      const cmeta_data_integer_shape *shape =
          (const cmeta_data_integer_shape *)values[i]->shape;
      check_true(cmeta_data_desc_valid(values[i]));
      check_equal(values[i]->kind, CMETA_DATA_UINT);
      check_equal(values[i]->storage_type->kind, CMETA_T_INTEGER);
      check_equal(values[i]->storage_type->size, sizes[i]);
      check_equal(values[i]->storage_type->align, alignments[i]);
      check_equal(shape->bits, bits[i]);
    }
  }

  it("uses stable semantic identities rather than descriptor addresses") {
    cmeta_type_desc equivalent = cmeta_type_int32;

    check_true(cmeta_type_equal(&cmeta_type_int32, &equivalent));
    check_false(cmeta_type_equal(&cmeta_type_int32,
                                 &cmeta_type_uint32));
    check_false(cmeta_type_equal(&cmeta_type_int32,
                                 &cmeta_type_int64));
  }
}

spec("Salts UUID CMeta adapter") {
  it("uses one canonical UUID metadata authority across translation units") {
    check_true(cmeta_uuid_cmeta_data_from_peer() ==
               &cmeta_uuid_cmeta_data);
    check_true(cmeta_uuid_cmeta_type_from_peer() ==
               &cmeta_uuid_cmeta_type);
    check_true(cmeta_uuid_cmeta_shape_from_peer() ==
               &cmeta_uuid_cmeta_shape);
    check_true(cmeta_uuid_cmeta_buffer_ops_from_peer() ==
               &cmeta_uuid_cmeta_buffer_ops);
    check_true(cmeta_uuid_cmeta_fixed_ops_from_peer() ==
               &cmeta_uuid_cmeta_fixed_ops);
  }

  it("copies UUID native storage through the canonical fixed-value provider") {
    const cmeta_uuid_t source = {{
        0x00u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u,
        0x88u, 0x99u, 0xaau, 0xbbu, 0xccu, 0xddu, 0xeeu, 0xffu}};
    cmeta_uuid_t destination = {{0}};
    size_t extent = 0u;

    check_true(cmeta_uuid_cmeta_data.fixed_ops ==
               &cmeta_uuid_cmeta_fixed_ops);
    check_equal(cmeta_data_fixed_extent(&cmeta_uuid_cmeta_data, &extent),
                CMETA_OK);
    check_equal(extent, (size_t)SALTS_UUID_SIZE);
    check_equal(cmeta_data_fixed_copy(&cmeta_uuid_cmeta_data, &destination,
                                      &source, sizeof(source)), CMETA_OK);
    check_equal(destination.bytes, source.bytes, sizeof(source.bytes));
    check_equal(cmeta_data_fixed_restore_zero(&cmeta_uuid_cmeta_data,
                                              &destination), CMETA_OK);
  }

  it("keeps UUID as a valid write-only string adapter") {
    static const unsigned char sentinel[] = {'x'};
    const unsigned char *view = sentinel;
    size_t view_size = 9u;
    const cmeta_uuid_t value = {{0}};

    check_true(cmeta_uuid_cmeta_data_valid(&cmeta_uuid_cmeta_data));
    check_equal(cmeta_data_buffer_read(&cmeta_uuid_cmeta_data, &value,
                                       SALTS_UUID_STRING_LENGTH, &view,
                                       &view_size),
                CMETA_TRAIT_MISSING);
    check_true(view == sentinel);
    check_equal(view_size, (size_t)9u);
  }

  it("rejects a replaced callback without candidate-owned authority") {
    const cmeta_data_desc *peer = cmeta_uuid_cmeta_data_from_peer();
    cmeta_data_buffer_ops forged_ops = *peer->buffer_ops;
    cmeta_data_desc forged_data = *peer;

    forged_data.buffer_ops = &forged_ops;
    forged_ops.assign = replaced_uuid_assign;

    check_false(cmeta_uuid_cmeta_data_valid(&forged_data));
  }

  it("accepts canonical UUID provenance instantiated in another TU") {
    static const unsigned char input[] =
        "00112233-4455-6677-8899-aabbccddeeff";
    static const uint8_t expected[SALTS_UUID_SIZE] = {
        0x00u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u,
        0x88u, 0x99u, 0xaau, 0xbbu, 0xccu, 0xddu, 0xeeu, 0xffu
    };
    const cmeta_data_desc *peer = cmeta_uuid_cmeta_data_from_peer();
    cmeta_uuid_t value = {{0}};

    check_not_null(peer);
    check_true(cmeta_uuid_cmeta_data_valid(peer));
    check_true(cmeta_type_equal(peer->storage_type,
                                &cmeta_uuid_cmeta_type));
    check_equal(cmeta_data_buffer_assign(peer, &value, input,
                                         SALTS_UUID_STRING_LENGTH,
                                         SALTS_UUID_STRING_LENGTH),
                CMETA_OK);
    check_equal(value.bytes, expected, sizeof(expected));
  }

  it("rejects each UUID callback replacement under copied provenance") {
    const cmeta_data_desc *peer = cmeta_uuid_cmeta_data_from_peer();
    cmeta_data_buffer_ops forged_ops = *peer->buffer_ops;
    cmeta_data_buffer_shape forged_shape =
        *(const cmeta_data_buffer_shape *)peer->shape;
    cmeta_type_identity forged_identity = *peer->storage_type->identity;
    cmeta_type_desc forged_type = *peer->storage_type;
    cmeta_data_desc forged_data = *peer;
    char forged_atom[] = "salts.uuid";
    char forged_stable_id[] = "salts.uuid.data";
    char forged_display_name[] = "cmeta_uuid_t";

    forged_identity.stable_atom_id = forged_atom;
    forged_type.identity = &forged_identity;
    forged_data.stable_id = forged_stable_id;
    forged_data.display_name = forged_display_name;
    forged_data.storage_type = &forged_type;
    forged_data.shape = &forged_shape;
    forged_data.buffer_ops = &forged_ops;
    forged_ops.storage_type = &forged_type;

    check_true(cmeta_uuid_cmeta_data_valid(&forged_data));

    forged_ops.is_zero = replaced_uuid_is_zero;
    check_false(cmeta_uuid_cmeta_data_valid(&forged_data));

    forged_ops = *peer->buffer_ops;
    forged_ops.storage_type = &forged_type;
    forged_ops.assign = replaced_uuid_assign;
    check_false(cmeta_uuid_cmeta_data_valid(&forged_data));

    forged_ops = *peer->buffer_ops;
    forged_ops.storage_type = &forged_type;
    forged_ops.restore_zero = replaced_uuid_restore_zero;
    check_false(cmeta_uuid_cmeta_data_valid(&forged_data));
  }

  it("rejects truncated UUID generic metadata") {
    const cmeta_data_desc *peer = cmeta_uuid_cmeta_data_from_peer();
    cmeta_data_buffer_ops truncated_ops = *peer->buffer_ops;
    cmeta_data_desc truncated_data = *peer;

    truncated_data.buffer_ops = &truncated_ops;
    truncated_ops.struct_size =
        offsetof(cmeta_data_buffer_ops, restore_zero);
    check_false(cmeta_uuid_cmeta_data_valid(&truncated_data));

    truncated_data = *peer;
    truncated_data.struct_size =
        offsetof(cmeta_data_desc, buffer_ops);
    check_false(cmeta_uuid_cmeta_data_valid(&truncated_data));
  }

  it("parses lowercase canonical text from a non-NUL-terminated slice") {
    static const char canonical[] =
        "00112233-4455-6677-8899-aabbccddeeff";
    static const uint8_t expected[SALTS_UUID_SIZE] = {
        0x00u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u,
        0x88u, 0x99u, 0xaau, 0xbbu, 0xccu, 0xddu, 0xeeu, 0xffu
    };
    unsigned char input[SALTS_UUID_STRING_LENGTH];
    cmeta_uuid_t value = {{0}};

    memcpy(input, canonical, sizeof(input));
    check_equal(cmeta_data_buffer_assign(&cmeta_uuid_cmeta_data, &value,
                                         input, sizeof(input), sizeof(input)),
                CMETA_OK);
    check_equal(value.bytes, expected, sizeof(expected));
  }

  it("accepts uppercase canonical hex") {
    static const unsigned char input[] =
        "00112233-4455-6677-8899-AABBCCDDEEFF";
    static const uint8_t expected[SALTS_UUID_SIZE] = {
        0x00u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u,
        0x88u, 0x99u, 0xaau, 0xbbu, 0xccu, 0xddu, 0xeeu, 0xffu
    };
    cmeta_uuid_t value = {{0}};

    check_equal(cmeta_data_buffer_assign(&cmeta_uuid_cmeta_data, &value,
                                         input, SALTS_UUID_STRING_LENGTH,
                                         SALTS_UUID_STRING_LENGTH), CMETA_OK);
    check_equal(value.bytes, expected, sizeof(expected));
  }

  it("requires exact canonical length hyphens and hex and restores zero") {
    unsigned char invalid[] = "00112233-4455-6677-8899-aabbccddeeff";
    cmeta_uuid_t value = {{0}};
    cmeta_uuid_t zero = {{0}};

    check_equal(cmeta_data_buffer_assign(&cmeta_uuid_cmeta_data, &value,
                                         invalid,
                                         SALTS_UUID_STRING_LENGTH - 1u,
                                         SALTS_UUID_STRING_LENGTH),
                CMETA_INVALID_ARGUMENT);
    check_equal(value.bytes, zero.bytes, SALTS_UUID_SIZE);
    check_equal(cmeta_data_buffer_assign(&cmeta_uuid_cmeta_data, &value,
                                         invalid,
                                         SALTS_UUID_STRING_LENGTH + 1u,
                                         SALTS_UUID_STRING_LENGTH + 1u),
                CMETA_INVALID_ARGUMENT);
    check_equal(value.bytes, zero.bytes, SALTS_UUID_SIZE);

    invalid[8] = '_';
    check_equal(cmeta_data_buffer_assign(&cmeta_uuid_cmeta_data, &value,
                                         invalid, SALTS_UUID_STRING_LENGTH,
                                         SALTS_UUID_STRING_LENGTH),
                CMETA_INVALID_ARGUMENT);
    check_equal(value.bytes, zero.bytes, SALTS_UUID_SIZE);
    invalid[8] = '-';
    invalid[0] = 'g';
    check_equal(cmeta_data_buffer_assign(&cmeta_uuid_cmeta_data, &value,
                                         invalid, SALTS_UUID_STRING_LENGTH,
                                         SALTS_UUID_STRING_LENGTH),
                CMETA_INVALID_ARGUMENT);
    check_equal(value.bytes, zero.bytes, SALTS_UUID_SIZE);
  }

  it("enforces max bytes before assignment") {
    static const unsigned char input[] =
        "00112233-4455-6677-8899-aabbccddeeff";
    cmeta_uuid_t value = {{0}};
    cmeta_uuid_t zero = {{0}};

    check_equal(cmeta_data_buffer_assign(&cmeta_uuid_cmeta_data, &value,
                                         input, SALTS_UUID_STRING_LENGTH,
                                         SALTS_UUID_STRING_LENGTH - 1u),
                CMETA_CAPACITY_EXCEEDED);
    check_equal(value.bytes, zero.bytes, SALTS_UUID_SIZE);
  }

  it("rejects an occupied destination without changing it") {
    static const unsigned char input[] =
        "00112233-4455-6677-8899-aabbccddeeff";
    cmeta_uuid_t value = {{1u}};
    cmeta_uuid_t original = value;

    check_equal(cmeta_data_buffer_assign(&cmeta_uuid_cmeta_data, &value,
                                         input, SALTS_UUID_STRING_LENGTH,
                                         SALTS_UUID_STRING_LENGTH),
                CMETA_INVALID_ARGUMENT);
    check_equal(value.bytes, original.bytes, SALTS_UUID_SIZE);
  }

  it("restores all bytes to zero idempotently") {
    cmeta_uuid_t value;
    cmeta_uuid_t zero = {{0}};

    memset(value.bytes, 0xff, sizeof(value.bytes));
    check_equal(cmeta_data_buffer_restore_zero(&cmeta_uuid_cmeta_data, &value),
                CMETA_OK);
    check_equal(value.bytes, zero.bytes, SALTS_UUID_SIZE);
    check_equal(cmeta_data_buffer_restore_zero(&cmeta_uuid_cmeta_data, &value),
                CMETA_OK);
    check_equal(value.bytes, zero.bytes, SALTS_UUID_SIZE);
  }

  it("exposes fixed owned string metadata") {
    const cmeta_data_buffer_shape *shape =
        (const cmeta_data_buffer_shape *)cmeta_uuid_cmeta_data.shape;

    check_equal(sizeof(cmeta_uuid_t), (size_t)SALTS_UUID_SIZE);
    check_true(cmeta_data_desc_valid(&cmeta_uuid_cmeta_data));
    check_equal(cmeta_uuid_cmeta_data.kind, CMETA_DATA_STRING);
    check_equal(shape->ownership, CMETA_DATA_BUFFER_OWNED);
    check_true(cmeta_uuid_cmeta_data.buffer_ops ==
               &cmeta_uuid_cmeta_buffer_ops);
  }
}
