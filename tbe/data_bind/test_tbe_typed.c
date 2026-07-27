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

TURBO_VEC_DEFINE(macro_u32_vec_t, uint32_t)

typedef struct MacroOrder {
  uint32_t order_id;
  tstr_t note;
  macro_u32_vec_t values;
  uint8_t presence[1];
} MacroOrder;

TBE_TYPED_DEFINE_STRUCT_WITH_PRESENCE(
    MACRO_ORDER_BINDING, MacroOrder, "MacroOrder", presence,
    TBE_TYPED_FIELD(MacroOrder, order_id, "id", TBE_TYPED_U32, TBE_TYPED_REQUIRED),
    TBE_TYPED_FIELD(MacroOrder, note, "note", TBE_TYPED_STRING, TBE_TYPED_OPTIONAL(0)),
    TBE_TYPED_LIST_FIELD(MacroOrder, values, "values", TBE_TYPED_U32, uint32_t, NULL,
                         TBE_TYPED_REQUIRED));

typedef struct InvalidMacroOrder {
  tstr_t note;
} InvalidMacroOrder;

TBE_TYPED_DEFINE_STRUCT(
    INVALID_MACRO_ORDER_BINDING, InvalidMacroOrder, "MacroOrder",
    TBE_TYPED_FIELD(InvalidMacroOrder, note, "note", TBE_TYPED_STRING,
                    TBE_TYPED_OPTIONAL(0)));

typedef struct MacroChild {
  uint16_t code;
} MacroChild;

TBE_TYPED_DEFINE_STRUCT(
    MACRO_CHILD_BINDING, MacroChild, "MacroChild",
    TBE_TYPED_FIELD(MacroChild, code, "code", TBE_TYPED_U16, TBE_TYPED_REQUIRED));

TURBO_VEC_DEFINE(macro_child_vec_t, MacroChild)

typedef struct MacroChildMapEntry {
  tstr_t key;
  MacroChild value;
} MacroChildMapEntry;

TURBO_VEC_DEFINE(macro_child_map_vec_t, MacroChildMapEntry)

typedef struct MacroCollections {
  MacroChild child;
  uint16_t fixed_values[2];
  uint8_t fixed_bytes[4];
  macro_child_vec_t children;
  macro_child_vec_t unique_children;
  macro_child_map_vec_t children_by_name;
} MacroCollections;

TBE_TYPED_DEFINE_STRUCT(
    MACRO_COLLECTIONS_BINDING, MacroCollections, "MacroCollections",
    TBE_TYPED_OBJECT_FIELD(MacroCollections, child, "child", &MACRO_CHILD_BINDING,
                           TBE_TYPED_REQUIRED),
    TBE_TYPED_FIXED_ARRAY_FIELD(MacroCollections, fixed_values, "fixed_values", TBE_TYPED_U16,
                                uint16_t, NULL, TBE_TYPED_REQUIRED),
    TBE_TYPED_FIXED_BYTES_FIELD(MacroCollections, fixed_bytes, "fixed_bytes",
                                TBE_TYPED_REQUIRED),
    TBE_TYPED_LIST_FIELD(MacroCollections, children, "children", TBE_TYPED_OBJECT, MacroChild,
                         &MACRO_CHILD_BINDING, TBE_TYPED_REQUIRED),
    TBE_TYPED_SET_FIELD(MacroCollections, unique_children, "unique_children", TBE_TYPED_OBJECT,
                        MacroChild, &MACRO_CHILD_BINDING, TBE_TYPED_REQUIRED),
    TBE_TYPED_MAP_FIELD(MacroCollections, children_by_name, "children_by_name",
                        MacroChildMapEntry, key, value, TBE_TYPED_OBJECT, &MACRO_CHILD_BINDING,
                        TBE_TYPED_REQUIRED));

typedef struct MacroWire {
  uint32_t id;
} MacroWire;

TBE_TYPED_DEFINE_STRUCT_EX(
    MACRO_WIRE_BINDING, MacroWire, "MacroWire", 4u, 0u, 0u, 0,
    TBE_TYPED_FIELD_EX(MacroWire, id, "id", TBE_TYPED_U32, TBE_TYPED_U32, TBE_TYPED_BOOL,
                       TBE_TYPED_BOOL, 0u, 0u, NULL, 0u, 0u, 0u, TBE_TYPED_BOOL,
                       TBE_TYPED_BOOL, NULL, 0u, 4u, 0u, TBE_TYPED_FIELD_WIRE_OFFSET));

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

  it("serializes typed records with schema field mappings") {
    static const char schema[] =
        "message Text { [name(displayText), alias(oldText)] string text; }";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    TestText text;
    char *json = NULL;
    size_t json_len = 0;

    check_int_eq(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    check_int_eq(tbe_typed_init(&TEST_TEXT_TYPE, &text, &error), DATA_BIND_OK);
    text.text = tstr_dup("mapped");
    check_not_null(text.text);
    if (codec != NULL && text.text != NULL) {
      check_int_eq(tbe_typed_serialize(codec, "Text", &TEST_TEXT_TYPE, &text, "json", &json,
                                      &json_len, &error),
                   DATA_BIND_OK);
      check_str_eq(json, "{\"displayText\":\"mapped\"}");
      check_size_eq(json_len, strlen(json));
    }
    tbe_typed_serialized_free(json);
    tbe_typed_clear(&TEST_TEXT_TYPE, &text);
    data_bind_free(codec);
  }

  it("binds an existing C struct through header-only macros") {
    static const char schema[] =
        "message MacroOrder { "
        "[name(orderId), alias(legacyId)] uint32 id; "
        "optional string note; "
        "list<uint32> values; "
        "}";
    static const char json[] = "{\"legacyId\":42,\"note\":\"macro\",\"values\":[7,9]}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    MacroOrder order;
    const uint32_t *values;
    char *mapped = NULL;
    size_t mapped_len = 0;

    check_int_eq(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    check_int_eq(TBE_TYPED_BIND_INIT(MACRO_ORDER_BINDING, &order, &error), DATA_BIND_OK);
    if (codec != NULL) {
      check_int_eq(TBE_TYPED_BIND_PARSE(codec, MACRO_ORDER_BINDING, "json", json,
                                        sizeof(json) - 1, 0, &order, &error),
                   DATA_BIND_OK);
      check_uint_eq(order.order_id, 42u);
      check_str_eq(order.note, "macro");
      check_uint_eq(order.presence[0], 1u);
      check_size_eq(macro_u32_vec_t_size(&order.values), 2u);
      values = macro_u32_vec_t_data_const(&order.values);
      if (macro_u32_vec_t_size(&order.values) == 2u) {
        check_uint_eq(values[0], 7u);
        check_uint_eq(values[1], 9u);
      }
      check_int_eq(TBE_TYPED_BIND_SERIALIZE(codec, MACRO_ORDER_BINDING, &order, "json", &mapped,
                                           &mapped_len, &error),
                   DATA_BIND_OK);
      check_str_eq(mapped, "{\"orderId\":42,\"note\":\"macro\",\"values\":[7,9]}");
      check_size_eq(mapped_len, strlen(mapped));
    }
    tbe_typed_serialized_free(mapped);
    TBE_TYPED_BIND_CLEAR(MACRO_ORDER_BINDING, &order);
    data_bind_free(codec);
  }

  it("rejects an optional macro field without a presence bitmap") {
    DataBindError error = DATA_BIND_ERROR_INIT;
    InvalidMacroOrder order;

    check_int_eq(TBE_TYPED_BIND_INIT(INVALID_MACRO_ORDER_BINDING, &order, &error),
                 DATA_BIND_ERR_SCHEMA);
    check_str_contains(error.message, "presence bitmap");
  }

  it("validates all composite macro field families") {
    DataBindError error = DATA_BIND_ERROR_INIT;
    MacroCollections collections;

    check_int_eq(tbe_typed_validate_descriptor(&MACRO_COLLECTIONS_BINDING, &error),
                 DATA_BIND_OK);
    check_int_eq(TBE_TYPED_BIND_INIT(MACRO_COLLECTIONS_BINDING, &collections, &error),
                 DATA_BIND_OK);
    check_size_eq(macro_child_vec_t_size(&collections.children), 0u);
    check_size_eq(macro_child_vec_t_size(&collections.unique_children), 0u);
    check_size_eq(macro_child_map_vec_t_size(&collections.children_by_name), 0u);
    TBE_TYPED_BIND_CLEAR(MACRO_COLLECTIONS_BINDING, &collections);
  }

  it("uses explicit macro metadata for direct binary layout") {
    static const uint8_t expected[] = {0x78, 0x56, 0x34, 0x12};
    DataBindError error = DATA_BIND_ERROR_INIT;
    MacroWire wire = {UINT32_C(0x12345678)};
    uint8_t *encoded = NULL;
    size_t encoded_len = 0;

    check_int_eq(tbe_typed_serialize_binary(&MACRO_WIRE_BINDING, &wire, &encoded, &encoded_len,
                                            &error),
                 DATA_BIND_OK);
    check_size_eq(encoded_len, sizeof(expected));
    check_mem_eq(encoded, expected, sizeof(expected));
    tbe_typed_serialized_free(encoded);
  }
}
