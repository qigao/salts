#include "tbe_typed.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct TestPacket {
  uint16_t code;
  uint32_t count;
  tstr_t name;
  uint8_t presence[1];
} TestPacket;

static const TbeTypedField TEST_PACKET_FIELDS[] = {
    {.name = "code",
     .kind = TBE_TYPED_U16,
     .wire_kind = TBE_TYPED_U16,
     .offset = offsetof(TestPacket, code),
     .wire_offset = 1,
     .wire_size = 2,
     .optional_bit = 0,
     .flags = TBE_TYPED_FIELD_OPTIONAL | TBE_TYPED_FIELD_WIRE_OFFSET},
    {.name = "count",
     .kind = TBE_TYPED_U32,
     .wire_kind = TBE_TYPED_U32,
     .offset = offsetof(TestPacket, count),
     .wire_offset = 3,
     .wire_size = 4,
     .flags = TBE_TYPED_FIELD_WIRE_OFFSET},
    {.name = "name",
     .kind = TBE_TYPED_STRING,
     .wire_kind = TBE_TYPED_STRING,
     .offset = offsetof(TestPacket, name),
     .flags = TBE_TYPED_FIELD_VAR_DATA}};

static const TbeTypedType TEST_PACKET_TYPE = {
    .name = "Packet",
    .size = sizeof(TestPacket),
    .fields = TEST_PACKET_FIELDS,
    .field_count = sizeof(TEST_PACKET_FIELDS) / sizeof(TEST_PACKET_FIELDS[0]),
    .fixed_block_size = 7,
    .presence_offset = offsetof(TestPacket, presence),
    .presence_size = 1,
    .wire_big_endian = 1};

TURBO_VEC_DEFINE(test_u32_vec_t, uint32_t)

typedef struct TestValues {
  test_u32_vec_t values;
} TestValues;

static const TbeTypedField TEST_VALUES_FIELDS[] = {{
    .name = "values",
    .kind = TBE_TYPED_LIST,
    .wire_kind = TBE_TYPED_LIST,
    .offset = offsetof(TestValues, values),
    .element_kind = TBE_TYPED_U32,
    .element_wire_kind = TBE_TYPED_U32,
    .element_size = sizeof(uint32_t),
}};

static const TbeTypedType TEST_VALUES_TYPE = {
    .name = "Values",
    .size = sizeof(TestValues),
    .fields = TEST_VALUES_FIELDS,
    .field_count = sizeof(TEST_VALUES_FIELDS) / sizeof(TEST_VALUES_FIELDS[0]),
};

typedef struct TestFixedValues {
  uint16_t values[2];
} TestFixedValues;

static const TbeTypedField TEST_FIXED_VALUES_FIELDS[] = {{
    .name = "values",
    .kind = TBE_TYPED_FIXED_ARRAY,
    .wire_kind = TBE_TYPED_FIXED_ARRAY,
    .offset = offsetof(TestFixedValues, values),
    .element_kind = TBE_TYPED_U16,
    .element_wire_kind = TBE_TYPED_U16,
    .element_size = sizeof(uint16_t),
    .fixed_count = 2,
    .wire_offset = 0,
    .wire_size = 4,
    .flags = TBE_TYPED_FIELD_WIRE_OFFSET,
}};

static const TbeTypedType TEST_FIXED_VALUES_TYPE = {
    .name = "FixedValues",
    .size = sizeof(TestFixedValues),
    .fields = TEST_FIXED_VALUES_FIELDS,
    .field_count = sizeof(TEST_FIXED_VALUES_FIELDS) / sizeof(TEST_FIXED_VALUES_FIELDS[0]),
    .fixed_block_size = 4,
};

typedef struct TestText {
  tstr_t text;
} TestText;

static const TbeTypedField TEST_TEXT_FIELDS[] = {{
    .name = "text",
    .kind = TBE_TYPED_STRING,
    .wire_kind = TBE_TYPED_STRING,
    .offset = offsetof(TestText, text),
    .flags = TBE_TYPED_FIELD_VAR_DATA,
}};

static const TbeTypedType TEST_TEXT_TYPE = {
    .name = "Text",
    .size = sizeof(TestText),
    .fields = TEST_TEXT_FIELDS,
    .field_count = sizeof(TEST_TEXT_FIELDS) / sizeof(TEST_TEXT_FIELDS[0]),
};

spec("typed DataBind binary") {
  it("round-trips an optional big-endian owning struct directly") {
    static const char schema[] =
        "schema Direct [byte_order(big)]; "
        "message Packet { optional uint16 code; uint32 count; string name; }";
    static const uint8_t expected[] = {1,    0x12, 0x34, 0x01, 0x02, 0x03, 0x04,
                                       0x00, 0x00, 0x00, 0x03, 'A',  'B',  'C'};
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    TestPacket source;
    TestPacket decoded;
    uint8_t *wire = NULL;
    size_t wire_len = 0;

    check_int_eq(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    check_int_eq(tbe_typed_init(&TEST_PACKET_TYPE, &source, &error), DATA_BIND_OK);
    check_int_eq(tbe_typed_init(&TEST_PACKET_TYPE, &decoded, &error), DATA_BIND_OK);
    source.presence[0] = 1;
    source.code = UINT16_C(0x1234);
    source.count = UINT32_C(0x01020304);
    source.name = tstr_dup("ABC");
    check_not_null(source.name);

    if (codec != NULL && source.name != NULL) {
      check_int_eq(tbe_typed_validate_schema(codec, "Packet", &TEST_PACKET_TYPE, &error),
                   DATA_BIND_OK);
      check_int_eq(tbe_typed_serialize_binary(&TEST_PACKET_TYPE, &source, &wire, &wire_len, &error),
                   DATA_BIND_OK);
      check_size_eq(wire_len, sizeof(expected));
      check_mem_eq(wire, expected, sizeof(expected));
      check_int_eq(tbe_typed_parse(codec, "Packet", &TEST_PACKET_TYPE, "bin", wire, wire_len, 0,
                                   &decoded, &error),
                   DATA_BIND_OK);
      check_uint_eq(decoded.presence[0], 1u);
      check_uint_eq(decoded.code, UINT16_C(0x1234));
      check_uint_eq(decoded.count, UINT32_C(0x01020304));
      check_str_eq(decoded.name, "ABC");
    }

    tbe_typed_serialized_free(wire);
    tbe_typed_clear(&TEST_PACKET_TYPE, &decoded);
    tbe_typed_clear(&TEST_PACKET_TYPE, &source);
    data_bind_free(codec);
  }

  it("rejects a descriptor whose byte order differs from the schema") {
    static const char schema[] =
        "schema Direct [byte_order(big)]; "
        "message Packet { optional uint16 code; uint32 count; string name; }";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    TbeTypedType wrong_order = TEST_PACKET_TYPE;

    wrong_order.wire_big_endian = 0;
    check_int_eq(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    if (codec != NULL)
      check_int_eq(tbe_typed_validate_schema(codec, "Packet", &wrong_order, &error),
                   DATA_BIND_ERR_SCHEMA);
    data_bind_free(codec);
  }

  it("preserves the owning struct when direct decoding is truncated") {
    static const uint8_t truncated[] = {1, 0x12, 0x34, 0, 0, 0, 7, 0, 0, 0, 4, 'x'};
    DataBindError error = DATA_BIND_ERROR_INIT;
    TestPacket packet;

    check_int_eq(tbe_typed_init(&TEST_PACKET_TYPE, &packet, &error), DATA_BIND_OK);
    packet.presence[0] = 1;
    packet.code = 9;
    packet.count = 11;
    packet.name = tstr_dup("stable");
    check_not_null(packet.name);
    if (packet.name != NULL) {
      check_int_eq(tbe_typed_parse_binary(&TEST_PACKET_TYPE, truncated, sizeof(truncated), &packet,
                                          &error),
                   DATA_BIND_ERR_PARSE);
      check_uint_eq(packet.presence[0], 1u);
      check_uint_eq(packet.code, 9u);
      check_uint_eq(packet.count, 11u);
      check_str_eq(packet.name, "stable");
    }
    tbe_typed_clear(&TEST_PACKET_TYPE, &packet);
  }

  it("keeps variable collections compatible through the typed struct API") {
    static const char schema[] = "message Values { list<uint32> values; }";
    static const uint8_t wire[] = {2, 0, 0, 0, 7, 0, 0, 0, 9, 0, 0, 0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    TestValues values;

    check_int_eq(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    check_int_eq(tbe_typed_init(&TEST_VALUES_TYPE, &values, &error), DATA_BIND_OK);
    if (codec != NULL) {
      const uint32_t *items;
      check_int_eq(tbe_typed_validate_schema(codec, "Values", &TEST_VALUES_TYPE, &error),
                   DATA_BIND_OK);
      check_int_eq(tbe_typed_parse(codec, "Values", &TEST_VALUES_TYPE, "bin", wire, sizeof(wire),
                                   0, &values, &error),
                   DATA_BIND_OK);
      check_size_eq(test_u32_vec_t_size(&values.values), 2u);
      items = test_u32_vec_t_data_const(&values.values);
      if (test_u32_vec_t_size(&values.values) == 2u) {
        check_uint_eq(items[0], 7u);
        check_uint_eq(items[1], 9u);
      }
    }
    tbe_typed_clear(&TEST_VALUES_TYPE, &values);
    data_bind_free(codec);
  }

  it("matches and decodes fixed array schema fields") {
    static const char schema[] = "message FixedValues { uint16[2] values; }";
    static const uint8_t wire[] = {0x34, 0x12, 0xcd, 0xab};
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    TestFixedValues values;

    check_int_eq(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    check_int_eq(tbe_typed_init(&TEST_FIXED_VALUES_TYPE, &values, &error), DATA_BIND_OK);
    if (codec != NULL) {
      check_int_eq(tbe_typed_validate_schema(codec, "FixedValues", &TEST_FIXED_VALUES_TYPE,
                                             &error),
                   DATA_BIND_OK);
      check_int_eq(tbe_typed_parse(codec, "FixedValues", &TEST_FIXED_VALUES_TYPE, "bin", wire,
                                   sizeof(wire), 0, &values, &error),
                   DATA_BIND_OK);
      check_uint_eq(values.values[0], UINT16_C(0x1234));
      check_uint_eq(values.values[1], UINT16_C(0xabcd));
    }
    tbe_typed_clear(&TEST_FIXED_VALUES_TYPE, &values);
    data_bind_free(codec);
  }

  it("directly decodes a variable-data-only record with a zero fixed block") {
    static const uint8_t wire[] = {3, 0, 0, 0, 'a', 'b', 'c'};
    DataBindError error = DATA_BIND_ERROR_INIT;
    TestText text;

    check_int_eq(tbe_typed_init(&TEST_TEXT_TYPE, &text, &error), DATA_BIND_OK);
    check_int_eq(tbe_typed_parse_binary(&TEST_TEXT_TYPE, wire, sizeof(wire), &text, &error),
                 DATA_BIND_OK);
    check_str_eq(text.text, "abc");
    tbe_typed_clear(&TEST_TEXT_TYPE, &text);
  }
}
