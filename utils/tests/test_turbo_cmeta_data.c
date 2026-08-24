#include "turbo_cmeta_data.h"
#include "turbo_str.h"
#include "turbo_vstr.h"
#include "tinytest.h"

#include <stdint.h>
#include <string.h>

const cmeta_data_desc *turbo_uuid_cmeta_data_from_peer(void);

_Static_assert(
    _Generic(&(turbo_uuid_cmeta_buffer_ops),
             const cmeta_data_buffer_ops *: 1,
             default: 0),
    "UUID adapter facade preserves the public address type");
_Static_assert(sizeof(turbo_uuid_cmeta_buffer_ops) ==
                   sizeof(cmeta_data_buffer_ops),
               "UUID adapter facade preserves base-object sizeof");

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

static const cmeta_data_desc tstr_bytes = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.tstr.bytes",
    .display_name = "tstr bytes",
    .kind = CMETA_DATA_BYTES,
    .storage_type = &turbo_tstr_cmeta_type,
    .shape = &owned_shape,
    .buffer_ops = &turbo_tstr_cmeta_buffer_ops
};

static const cmeta_data_desc vstr_string = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.vstr.string",
    .display_name = "vstr string",
    .kind = CMETA_DATA_STRING,
    .storage_type = &turbo_vstr_cmeta_type,
    .shape = &borrowed_shape,
    .buffer_ops = &turbo_vstr_cmeta_buffer_ops
};

spec("TurboUtils CMeta buffer adapters") {
  it("copies exact owned tstr bytes including embedded NUL") {
    static const unsigned char input[] = {'a', 0, 'b'};
    tstr value = NULL;
    bool is_zero = false;

    check_equal(cmeta_data_buffer_assign(&tstr_bytes, &value, input,
                                         sizeof(input), sizeof(input)),
                CMETA_OK);
    check_not_null(value);
    check_equal(tstr_len(value), sizeof(input));
    check_equal(memcmp(value, input, sizeof(input)), 0);
    check_equal(cmeta_data_buffer_is_zero(&tstr_bytes, &value, &is_zero),
                CMETA_OK);
    check_false(is_zero);

    check_equal(cmeta_data_buffer_restore_zero(&tstr_bytes, &value), CMETA_OK);
    check_null(value);
  }

  it("keeps owned tstr zero for empty input and failed allocation") {
    static const unsigned char byte = 0;
    tstr value = NULL;

    check_equal(cmeta_data_buffer_assign(&tstr_bytes, &value, NULL, 0u, 0u),
                CMETA_OK);
    check_null(value);

    check_equal(cmeta_data_buffer_assign(&tstr_bytes, &value, &byte,
                                         SIZE_MAX, SIZE_MAX),
                CMETA_OUT_OF_MEMORY);
    check_null(value);
  }

  it("borrows vstr input without copying and restores canonical zero") {
    static const unsigned char input[] = {'u', 't', 'f', '8'};
    vstr value = {NULL, 0u};

    check_equal(cmeta_data_buffer_assign(&vstr_string, &value, input,
                                         sizeof(input), sizeof(input)),
                CMETA_OK);
    check_true(value.data == (const char *)input);
    check_equal(value.len, sizeof(input));

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

spec("TurboUtils fixed-width CMeta descriptors") {
  it("describes every signed width with exact storage ABI") {
    const cmeta_data_desc *const values[] = {
        &turbo_int8_cmeta_data, &turbo_int16_cmeta_data,
        &turbo_int32_cmeta_data, &turbo_int64_cmeta_data
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
        &turbo_uint8_cmeta_data, &turbo_uint16_cmeta_data,
        &turbo_uint32_cmeta_data, &turbo_uint64_cmeta_data
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
    cmeta_type_desc equivalent = turbo_int32_cmeta_type;

    check_true(cmeta_type_equal(&turbo_int32_cmeta_type, &equivalent));
    check_false(cmeta_type_equal(&turbo_int32_cmeta_type,
                                 &turbo_uint32_cmeta_type));
    check_false(cmeta_type_equal(&turbo_int32_cmeta_type,
                                 &turbo_int64_cmeta_type));
  }
}

spec("TurboUtils UUID CMeta adapter") {
  it("accepts canonical UUID provenance instantiated in another TU") {
    static const unsigned char input[] =
        "00112233-4455-6677-8899-aabbccddeeff";
    static const uint8_t expected[TURBO_UUID_SIZE] = {
        0x00u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u,
        0x88u, 0x99u, 0xaau, 0xbbu, 0xccu, 0xddu, 0xeeu, 0xffu
    };
    const cmeta_data_desc *peer = turbo_uuid_cmeta_data_from_peer();
    turbo_uuid_t value = {{0}};

    check_not_null(peer);
    check_true(turbo_uuid_cmeta_data_valid(peer));
    check_true(cmeta_type_equal(peer->storage_type,
                                &turbo_uuid_cmeta_type));
    check_equal(cmeta_data_buffer_assign(peer, &value, input,
                                         TURBO_UUID_STRING_LENGTH,
                                         TURBO_UUID_STRING_LENGTH),
                CMETA_OK);
    check_equal(value.bytes, expected, sizeof(expected));
  }

  it("rejects each UUID callback replacement under copied provenance") {
    const cmeta_data_desc *peer = turbo_uuid_cmeta_data_from_peer();
    const turbo_uuid_cmeta_buffer_ops_desc *peer_ops =
        (const turbo_uuid_cmeta_buffer_ops_desc *)peer->buffer_ops;
    turbo_uuid_cmeta_buffer_ops_desc forged_ops = *peer_ops;
    cmeta_data_buffer_shape forged_shape =
        *(const cmeta_data_buffer_shape *)peer->shape;
    cmeta_type_identity forged_identity = *peer->storage_type->identity;
    cmeta_type_desc forged_type = *peer->storage_type;
    cmeta_data_desc forged_data = *peer;
    char forged_atom[] = "turbo.uuid";
    char forged_stable_id[] = "turbo.uuid.data";
    char forged_display_name[] = "turbo_uuid_t";

    forged_identity.stable_atom_id = forged_atom;
    forged_type.identity = &forged_identity;
    forged_data.stable_id = forged_stable_id;
    forged_data.display_name = forged_display_name;
    forged_data.storage_type = &forged_type;
    forged_data.shape = &forged_shape;
    forged_data.buffer_ops = &forged_ops.base;
    forged_ops.base.storage_type = &forged_type;

    check_true(turbo_uuid_cmeta_data_valid(&forged_data));

    forged_ops.base.is_zero = replaced_uuid_is_zero;
    check_false(turbo_uuid_cmeta_data_valid(&forged_data));

    forged_ops = *peer_ops;
    forged_ops.base.storage_type = &forged_type;
    forged_ops.base.assign = replaced_uuid_assign;
    check_false(turbo_uuid_cmeta_data_valid(&forged_data));

    forged_ops = *peer_ops;
    forged_ops.base.storage_type = &forged_type;
    forged_ops.base.restore_zero = replaced_uuid_restore_zero;
    check_false(turbo_uuid_cmeta_data_valid(&forged_data));
  }

  it("rejects a truncated UUID provenance record before its extension") {
    const cmeta_data_desc *peer = turbo_uuid_cmeta_data_from_peer();
    const turbo_uuid_cmeta_buffer_ops_desc *peer_ops =
        (const turbo_uuid_cmeta_buffer_ops_desc *)peer->buffer_ops;
    turbo_uuid_cmeta_buffer_ops_desc truncated_ops = *peer_ops;
    cmeta_data_desc truncated_data = *peer;

    truncated_data.buffer_ops = &truncated_ops.base;
    truncated_ops.base.struct_size =
        TURBO_UUID_CMETA_BUFFER_OPS_PREFIX_SIZE - 1u;
    check_false(turbo_uuid_cmeta_data_valid(&truncated_data));

    truncated_data = *peer;
    truncated_data.struct_size =
        offsetof(cmeta_data_desc, buffer_ops);
    check_false(turbo_uuid_cmeta_data_valid(&truncated_data));
  }

  it("parses lowercase canonical text from a non-NUL-terminated slice") {
    static const char canonical[] =
        "00112233-4455-6677-8899-aabbccddeeff";
    static const uint8_t expected[TURBO_UUID_SIZE] = {
        0x00u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u,
        0x88u, 0x99u, 0xaau, 0xbbu, 0xccu, 0xddu, 0xeeu, 0xffu
    };
    unsigned char input[TURBO_UUID_STRING_LENGTH];
    turbo_uuid_t value = {{0}};

    memcpy(input, canonical, sizeof(input));
    check_equal(cmeta_data_buffer_assign(&turbo_uuid_cmeta_data, &value,
                                         input, sizeof(input), sizeof(input)),
                CMETA_OK);
    check_equal(value.bytes, expected, sizeof(expected));
  }

  it("accepts uppercase canonical hex") {
    static const unsigned char input[] =
        "00112233-4455-6677-8899-AABBCCDDEEFF";
    static const uint8_t expected[TURBO_UUID_SIZE] = {
        0x00u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u,
        0x88u, 0x99u, 0xaau, 0xbbu, 0xccu, 0xddu, 0xeeu, 0xffu
    };
    turbo_uuid_t value = {{0}};

    check_equal(cmeta_data_buffer_assign(&turbo_uuid_cmeta_data, &value,
                                         input, TURBO_UUID_STRING_LENGTH,
                                         TURBO_UUID_STRING_LENGTH), CMETA_OK);
    check_equal(value.bytes, expected, sizeof(expected));
  }

  it("requires exact canonical length hyphens and hex and restores zero") {
    unsigned char invalid[] = "00112233-4455-6677-8899-aabbccddeeff";
    turbo_uuid_t value = {{0}};
    turbo_uuid_t zero = {{0}};

    check_equal(cmeta_data_buffer_assign(&turbo_uuid_cmeta_data, &value,
                                         invalid,
                                         TURBO_UUID_STRING_LENGTH - 1u,
                                         TURBO_UUID_STRING_LENGTH),
                CMETA_INVALID_ARGUMENT);
    check_equal(value.bytes, zero.bytes, TURBO_UUID_SIZE);
    check_equal(cmeta_data_buffer_assign(&turbo_uuid_cmeta_data, &value,
                                         invalid,
                                         TURBO_UUID_STRING_LENGTH + 1u,
                                         TURBO_UUID_STRING_LENGTH + 1u),
                CMETA_INVALID_ARGUMENT);
    check_equal(value.bytes, zero.bytes, TURBO_UUID_SIZE);

    invalid[8] = '_';
    check_equal(cmeta_data_buffer_assign(&turbo_uuid_cmeta_data, &value,
                                         invalid, TURBO_UUID_STRING_LENGTH,
                                         TURBO_UUID_STRING_LENGTH),
                CMETA_INVALID_ARGUMENT);
    check_equal(value.bytes, zero.bytes, TURBO_UUID_SIZE);
    invalid[8] = '-';
    invalid[0] = 'g';
    check_equal(cmeta_data_buffer_assign(&turbo_uuid_cmeta_data, &value,
                                         invalid, TURBO_UUID_STRING_LENGTH,
                                         TURBO_UUID_STRING_LENGTH),
                CMETA_INVALID_ARGUMENT);
    check_equal(value.bytes, zero.bytes, TURBO_UUID_SIZE);
  }

  it("enforces max bytes before assignment") {
    static const unsigned char input[] =
        "00112233-4455-6677-8899-aabbccddeeff";
    turbo_uuid_t value = {{0}};
    turbo_uuid_t zero = {{0}};

    check_equal(cmeta_data_buffer_assign(&turbo_uuid_cmeta_data, &value,
                                         input, TURBO_UUID_STRING_LENGTH,
                                         TURBO_UUID_STRING_LENGTH - 1u),
                CMETA_CAPACITY_EXCEEDED);
    check_equal(value.bytes, zero.bytes, TURBO_UUID_SIZE);
  }

  it("rejects an occupied destination without changing it") {
    static const unsigned char input[] =
        "00112233-4455-6677-8899-aabbccddeeff";
    turbo_uuid_t value = {{1u}};
    turbo_uuid_t original = value;

    check_equal(cmeta_data_buffer_assign(&turbo_uuid_cmeta_data, &value,
                                         input, TURBO_UUID_STRING_LENGTH,
                                         TURBO_UUID_STRING_LENGTH),
                CMETA_INVALID_ARGUMENT);
    check_equal(value.bytes, original.bytes, TURBO_UUID_SIZE);
  }

  it("restores all bytes to zero idempotently") {
    turbo_uuid_t value;
    turbo_uuid_t zero = {{0}};

    memset(value.bytes, 0xff, sizeof(value.bytes));
    check_equal(cmeta_data_buffer_restore_zero(&turbo_uuid_cmeta_data, &value),
                CMETA_OK);
    check_equal(value.bytes, zero.bytes, TURBO_UUID_SIZE);
    check_equal(cmeta_data_buffer_restore_zero(&turbo_uuid_cmeta_data, &value),
                CMETA_OK);
    check_equal(value.bytes, zero.bytes, TURBO_UUID_SIZE);
  }

  it("exposes fixed owned string metadata") {
    const cmeta_data_buffer_shape *shape =
        (const cmeta_data_buffer_shape *)turbo_uuid_cmeta_data.shape;

    check_equal(sizeof(turbo_uuid_t), (size_t)TURBO_UUID_SIZE);
    check_true(cmeta_data_desc_valid(&turbo_uuid_cmeta_data));
    check_equal(turbo_uuid_cmeta_data.kind, CMETA_DATA_STRING);
    check_equal(shape->ownership, CMETA_DATA_BUFFER_OWNED);
    check_true(turbo_uuid_cmeta_data.buffer_ops ==
               &turbo_uuid_cmeta_buffer_ops);
  }
}
