#include "typed_order.h"

#include "tinytest.h"

#include <stdio.h>
#include <string.h>

#define TEST_ORDER_REQUEST_ID "01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001"

enum {
  TEST_GUEST_WIRE_CAPACITY = 512,
  TEST_GUEST_TEXT_CAPACITY = 64,
  TEST_BRIDGE_CONTRACT_ERROR = 91,
  TEST_BRIDGE_CAPACITY_ERROR = 92
};

typedef struct TestGuestBridgeContext {
  const uint8_t *wire;
  uint32_t wire_size;
  uint32_t observed_format;
  uint32_t observed_csv_row;
} TestGuestBridgeContext;

static int32_t test_guest_text_to_binary(void *context, const char *schema_id,
                                         uint32_t schema_id_len, const char *type_name,
                                         uint32_t type_name_len, uint32_t format, const void *input,
                                         uint32_t input_len, uint32_t csv_row, void *output,
                                         uint32_t output_capacity, uint32_t *output_size) {
  TestGuestBridgeContext *bridge = (TestGuestBridgeContext *)context;
  if (bridge == NULL || schema_id == NULL || type_name == NULL || input == NULL || input_len == 0 ||
      output_size == NULL || schema_id_len != 6 || memcmp(schema_id, "Orders", 6) != 0 ||
      type_name_len != 5 || memcmp(type_name, "Order", 5) != 0)
    return TEST_BRIDGE_CONTRACT_ERROR;
  bridge->observed_format = format;
  bridge->observed_csv_row = csv_row;
  *output_size = bridge->wire_size;
  if (output == NULL || output_capacity < bridge->wire_size) return TEST_BRIDGE_CAPACITY_ERROR;
  memcpy(output, bridge->wire, bridge->wire_size);
  return TBE_GUEST_OK;
}

static int32_t test_guest_binary_to_text(void *context, const char *schema_id,
                                         uint32_t schema_id_len, const char *type_name,
                                         uint32_t type_name_len, uint32_t format, const void *input,
                                         uint32_t input_len, void *output, uint32_t output_capacity,
                                         uint32_t *output_size) {
  static const char *const texts[] = {NULL, "json", "yaml", "csv", "xml"};
  TestGuestBridgeContext *bridge = (TestGuestBridgeContext *)context;
  size_t text_len;
  if (bridge == NULL || schema_id == NULL || type_name == NULL || input == NULL ||
      output_size == NULL || schema_id_len != 6 || memcmp(schema_id, "Orders", 6) != 0 ||
      type_name_len != 5 || memcmp(type_name, "Order", 5) != 0 || input_len != bridge->wire_size ||
      memcmp(input, bridge->wire, input_len) != 0 || format < TBE_GUEST_FORMAT_JSON ||
      format > TBE_GUEST_FORMAT_XML)
    return TEST_BRIDGE_CONTRACT_ERROR;
  bridge->observed_format = format;
  text_len = strlen(texts[format]);
  *output_size = (uint32_t)text_len;
  if (output == NULL || output_capacity < text_len) return TEST_BRIDGE_CAPACITY_ERROR;
  memcpy(output, texts[format], text_len);
  return TBE_GUEST_OK;
}

static void check_status_ok(DataBindStatus status, const DataBindError *error) {
  if (status != DATA_BIND_OK && error != NULL)
    fprintf(stderr, "DataBind failure: status=%d path=%s message=%s\n", (int)status, error->path,
            error->message);
  check_int_eq(status, DATA_BIND_OK);
}

static void check_order(const Order_t *order) {
  const Fill_t *first;
  const Fill_t *second;
  turbo_uuid_t expected_request_id;

  check_not_null(order);
  if (order == NULL) return;
  check_int_eq(order->header.seq, 7);
  check_int_eq(order->order_id, 42);
  check(order->min_value == INT64_MIN);
  check(order->max_value == UINT64_MAX);
  check_int_eq(turbo_uuid_parse(TEST_ORDER_REQUEST_ID, &expected_request_id), TURBO_OK);
  check_true(turbo_uuid_equal(&order->request_id, &expected_request_id));
  check_int_eq(order->side, Side_Buy);
  check_str_eq(order->symbol, "ABC");
  check_size_eq(tbe_bytes_t_size(&order->payload), 3);
  check_mem_eq(tbe_bytes_t_data_const(&order->payload), "raw", 3);
  check_size_eq(Order_fills_vec_t_size(&order->fills), 2);
  first = Order_fills_vec_t_at_const(&order->fills, 0);
  second = Order_fills_vec_t_at_const(&order->fills, 1);
  check_not_null(first);
  check_not_null(second);
  if (first != NULL) {
    check_int_eq(first->price, 100);
    check_int_eq(first->qty, 3);
  }
  if (second != NULL) {
    check_int_eq(second->price, 101);
    check_int_eq(second->qty, 4);
  }
}

spec("generated typed Order") {
  static DataBind *codec = NULL;
  static DataBindError error = DATA_BIND_ERROR_INIT;
  static Order_t order;
  const char *json =
      "{\"header\":{\"seq\":7},\"legacyId\":42,\"request_id\":\"" TEST_ORDER_REQUEST_ID
      "\",\"min_value\":-9223372036854775808,"
      "\"max_value\":18446744073709551615,\"side\":\"Buy\","
      "\"fills\":[{\"price\":100,\"qty\":3},{\"price\":101,\"qty\":4}],"
      "\"symbol\":\"ABC\",\"payload\":\"raw\"}";
  const char *optional_json =
      "{\"header\":{\"seq\":7},\"legacyId\":42,\"request_id\":\"" TEST_ORDER_REQUEST_ID
      "\",\"min_value\":-9223372036854775808,"
      "\"max_value\":18446744073709551615,\"side\":\"Buy\",\"routing_hint\":9,"
      "\"fills\":[{\"price\":100,\"qty\":3},{\"price\":101,\"qty\":4}],"
      "\"symbol\":\"ABC\",\"client_tag\":\"edge-a\",\"payload\":\"raw\"}";

  before_each() {
    Order_init(&order);
    check_int_eq(Orders_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec != NULL)
      check_int_eq(Order_from_json(codec, &order, json, strlen(json), &error), DATA_BIND_OK);
  }

  after_each() {
    Order_clear(&order);
    data_bind_free(codec);
  }

  it("should deserialize JSON into strong fields") { check_order(&order); }

  it("should leave omitted optional fields absent") {
    char *encoded = NULL;
    size_t encoded_len = 0;

    check_uint_eq(order._presence[0], 0u);
    check_status_ok(Order_to_json(codec, &order, &encoded, &encoded_len, &error), &error);
    check_not_null(encoded);
    if (encoded != NULL) {
      check_null(strstr(encoded, "\"routing_hint\""));
      check_null(strstr(encoded, "\"client_tag\""));
    }
    tbe_typed_serialized_free(encoded);
  }

  it("should bound optional presence helpers by field and bitmap size") {
    uint8_t presence[Order_PRESENCE_BITMAP_SIZE] = {0};
    Order_view_t view = {presence, sizeof(presence)};
    Order_builder_t builder = {presence, sizeof(presence)};

    check_false(Order_has_optional_field(&view, Order_OPTIONAL_client_tag));
    Order_set_optional_field(&builder, Order_OPTIONAL_client_tag);
    check_true(Order_has_optional_field(&view, Order_OPTIONAL_client_tag));
    Order_clear_optional_field(&builder, Order_OPTIONAL_client_tag);
    check_false(Order_has_optional_field(&view, Order_OPTIONAL_client_tag));

    Order_set_optional_field(&builder, (Order_optional_field_t)-1);
    Order_set_optional_field(&builder, (Order_optional_field_t)Order_OPTIONAL_FIELD_COUNT);
    check_uint_eq(presence[0], 0u);

    view.size = 0;
    builder.size = 0;
    Order_set_optional_field(&builder, Order_OPTIONAL_routing_hint);
    check_false(Order_has_optional_field(&view, Order_OPTIONAL_routing_hint));
    check_uint_eq(presence[0], 0u);
  }

  it("should preserve optional presence through text and binary bindings") {
    Order_t present;
    Order_t decoded;
    uint8_t *wire = NULL;
    char *encoded = NULL;
    size_t wire_len = 0;
    size_t encoded_len = 0;

    Order_init(&present);
    Order_init(&decoded);
    check_status_ok(Order_from_json(codec, &present, optional_json, strlen(optional_json), &error),
                    &error);
    check_uint_eq(present._presence[0],
                  (1u << Order_OPTIONAL_routing_hint) | (1u << Order_OPTIONAL_client_tag));
    check_uint_eq(present.routing_hint, 9u);
    check_str_eq(present.client_tag, "edge-a");
    check_status_ok(Order_to_json(codec, &present, &encoded, &encoded_len, &error), &error);
    check_str_contains(encoded, "\"routing_hint\":9");
    check_str_contains(encoded, "\"client_tag\":\"edge-a\"");
    check_status_ok(Order_to_bin(&present, &wire, &wire_len, &error), &error);
    check_status_ok(Order_from_bin(codec, &decoded, wire, wire_len, &error), &error);
    check_uint_eq(decoded._presence[0], present._presence[0]);
    check_uint_eq(decoded.routing_hint, 9u);
    check_str_eq(decoded.client_tag, "edge-a");

    tbe_typed_serialized_free(wire);
    tbe_typed_serialized_free(encoded);
    Order_clear(&decoded);
    Order_clear(&present);
  }

  it("should round-trip the binary wire format") {
    uint8_t *encoded = NULL;
    size_t encoded_len = 0;
    Order_t decoded;
    Order_init(&decoded);
    check_int_eq(Order_to_bin(&order, &encoded, &encoded_len, &error), DATA_BIND_OK);
    check_not_null(encoded);
    check_size_gt(encoded_len, 0);
    if (encoded != NULL) {
      check_status_ok(Order_from_bin(codec, &decoded, encoded, encoded_len, &error), &error);
      check_order(&decoded);
    }
    tbe_typed_serialized_free(encoded);
    Order_clear(&decoded);
  }

  it("should preserve the owning struct when direct binary decoding fails") {
    uint8_t *encoded = NULL;
    size_t encoded_len = 0;
    check_status_ok(Order_to_bin(&order, &encoded, &encoded_len, &error), &error);
    check_not_null(encoded);
    check_size_gt(encoded_len, 1);
    if (encoded != NULL && encoded_len > 1) {
      check_int_eq(Order_from_bin(codec, &order, encoded, encoded_len - 1, &error),
                   DATA_BIND_ERR_PARSE);
      check_order(&order);
    }
    tbe_typed_serialized_free(encoded);
  }

  it("should reject a generated descriptor used with a different schema") {
    static const char other_schema[] =
        "schema Other [byte_order(little)]; message Order { uint32 id; }";
    DataBind *other = NULL;
    uint8_t wire[4] = {0};
    check_int_eq(data_bind_create_from_text(other_schema, sizeof(other_schema) - 1, &other, &error),
                 DATA_BIND_OK);
    check_not_null(other);
    if (other != NULL) {
      check_int_eq(Order_from_bin(other, &order, wire, sizeof(wire), &error), DATA_BIND_ERR_SCHEMA);
      check_order(&order);
    }
    data_bind_free(other);
  }

  it("should round-trip JSON YAML CSV and XML") {
    static const char *formats[] = {"json", "yaml", "csv", "xml"};
    size_t i;
    for (i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
      char *encoded = NULL;
      size_t encoded_len = 0;
      Order_t decoded;
      DataBindStatus status;
      Order_init(&decoded);
      if (strcmp(formats[i], "json") == 0) {
        status = Order_to_json(codec, &order, &encoded, &encoded_len, &error);
        if (status == DATA_BIND_OK)
          status = Order_from_json(codec, &decoded, encoded, encoded_len, &error);
      } else if (strcmp(formats[i], "yaml") == 0) {
        status = Order_to_yaml(codec, &order, &encoded, &encoded_len, &error);
        if (status == DATA_BIND_OK)
          status = Order_from_yaml(codec, &decoded, encoded, encoded_len, &error);
      } else if (strcmp(formats[i], "csv") == 0) {
        status = Order_to_csv(codec, &order, &encoded, &encoded_len, &error);
        if (status == DATA_BIND_OK)
          status = Order_from_csv(codec, &decoded, encoded, encoded_len, 0, &error);
      } else {
        status = Order_to_xml(codec, &order, &encoded, &encoded_len, &error);
        if (status == DATA_BIND_OK)
          status = Order_from_xml(codec, &decoded, encoded, encoded_len, &error);
      }
      check_status_ok(status, &error);
      check_not_null(encoded);
      if (status == DATA_BIND_OK) check_order(&decoded);
      tbe_typed_serialized_free(encoded);
      Order_clear(&decoded);
    }
  }

  it("should apply schema names in generated serializers") {
    char *json_output = NULL;
    char *yaml_output = NULL;
    char *csv_output = NULL;
    char *xml_output = NULL;
    size_t output_len = 0;

    check_status_ok(Order_to_json(codec, &order, &json_output, &output_len, &error), &error);
    check_str_contains(json_output, "\"orderId\":42");
    check_status_ok(Order_to_yaml(codec, &order, &yaml_output, &output_len, &error), &error);
    check_str_contains(yaml_output, "orderId");
    check_status_ok(Order_to_csv(codec, &order, &csv_output, &output_len, &error), &error);
    check_str_contains(csv_output, "orderId");
    check_status_ok(Order_to_xml(codec, &order, &xml_output, &output_len, &error), &error);
    check_str_contains(xml_output, "<orderId>42</orderId>");

    tbe_typed_serialized_free(json_output);
    tbe_typed_serialized_free(yaml_output);
    tbe_typed_serialized_free(csv_output);
    tbe_typed_serialized_free(xml_output);
  }

  it("should expose a schema-specific host codec for runtime providers") {
    const tbe_schema_codec_v1_t *schema_codec = Orders_schema_codec();
    uint8_t wire[TEST_GUEST_WIRE_CAPACITY];
    size_t wire_len = 0;
    static const uint32_t formats[] = {
        TBE_SCHEMA_FORMAT_JSON,
        TBE_SCHEMA_FORMAT_YAML,
        TBE_SCHEMA_FORMAT_CSV,
        TBE_SCHEMA_FORMAT_XML,
    };
    size_t i;

    check_not_null(schema_codec);
    if (schema_codec == NULL) return;
    check_size_eq(schema_codec->struct_size, sizeof(*schema_codec));
    check_int_eq(schema_codec->abi_version, TBE_SCHEMA_CODEC_ABI_VERSION);
    check_str_eq(schema_codec->schema_id, "Orders");
    check_not_null(schema_codec->create);
    check_not_null(schema_codec->text_to_binary_into);
    check_not_null(schema_codec->binary_to_text);
    check_not_null(schema_codec->free_output);

    check_status_ok(schema_codec->text_to_binary_into(codec, "Order", TBE_SCHEMA_FORMAT_JSON, json,
                                                      strlen(json), 0, wire, sizeof(wire),
                                                      &wire_len, &error),
                    &error);
    check_size_gt(wire_len, 0);

    if (wire_len != 0) {
      for (i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        char *text = NULL;
        size_t text_len = 0;
        uint8_t roundtrip_wire[TEST_GUEST_WIRE_CAPACITY];
        size_t roundtrip_wire_len = 0;
        Order_t decoded;

        Order_init(&decoded);
        check_status_ok(schema_codec->binary_to_text(codec, "Order", formats[i], wire, wire_len,
                                                     &text, &text_len, &error),
                        &error);
        check_not_null(text);
        check_size_gt(text_len, 0);
        if (text != NULL) {
          check_status_ok(schema_codec->text_to_binary_into(
                              codec, "Order", formats[i], text, text_len, 0, roundtrip_wire,
                              sizeof(roundtrip_wire), &roundtrip_wire_len, &error),
                          &error);
          if (roundtrip_wire_len != 0) {
            check_status_ok(
                Order_from_bin(codec, &decoded, roundtrip_wire, roundtrip_wire_len, &error),
                &error);
            check_order(&decoded);
          }
        }
        schema_codec->free_output(text);
        Order_clear(&decoded);
      }
    }

    wire_len = 0;

    check_int_eq(schema_codec->text_to_binary_into(codec, "Missing", TBE_SCHEMA_FORMAT_JSON, json,
                                                   strlen(json), 0, wire, sizeof(wire), &wire_len,
                                                   &error),
                 DATA_BIND_ERR_TYPE_NOT_FOUND);
    check_size_eq(wire_len, 0);

    check_int_eq(schema_codec->text_to_binary_into(codec, "Order", TBE_SCHEMA_FORMAT_JSON, json,
                                                   strlen(json), 0, wire, 1, &wire_len, &error),
                 DATA_BIND_ERR_INVALID_ARG);
    check_size_gt(wire_len, 1);
  }

  it("should route guest text formats through the injected bridge") {
    uint8_t *encoded = NULL;
    size_t encoded_len = 0;
    uint8_t guest_wire[TEST_GUEST_WIRE_CAPACITY];
    char guest_text[TEST_GUEST_TEXT_CAPACITY];
    size_t output_len = 0;
    Order_view_t view;
    TestGuestBridgeContext context = {0};
    tbe_guest_bridge_t bridge = {
        .struct_size = sizeof(bridge),
        .abi_version = TBE_GUEST_BRIDGE_ABI_VERSION,
        .context = &context,
        .text_to_binary = test_guest_text_to_binary,
        .binary_to_text = test_guest_binary_to_text,
    };

    check_status_ok(Order_to_bin(&order, &encoded, &encoded_len, &error), &error);
    check_not_null(encoded);
    check(encoded_len <= TEST_GUEST_WIRE_CAPACITY);
    check(encoded_len <= UINT32_MAX);
    if (encoded == NULL || encoded_len > TEST_GUEST_WIRE_CAPACITY || encoded_len > UINT32_MAX) {
      tbe_typed_serialized_free(encoded);
    } else {
      context.wire = encoded;
      context.wire_size = (uint32_t)encoded_len;

      check_int_eq(Order_guest_from_json(&bridge, json, strlen(json), guest_wire,
                                         sizeof(guest_wire), &output_len, &view),
                   TBE_GUEST_OK);
      check_int_eq(context.observed_format, TBE_GUEST_FORMAT_JSON);
      check_int_eq(context.observed_csv_row, 0);
      check_size_eq(output_len, encoded_len);
      check_mem_eq(view.data, encoded, encoded_len);

      check_int_eq(Order_guest_from_yaml(&bridge, json, strlen(json), guest_wire,
                                         sizeof(guest_wire), &output_len, &view),
                   TBE_GUEST_OK);
      check_int_eq(context.observed_format, TBE_GUEST_FORMAT_YAML);

      check_int_eq(Order_guest_from_csv(&bridge, json, strlen(json), 3, guest_wire,
                                        sizeof(guest_wire), &output_len, &view),
                   TBE_GUEST_OK);
      check_int_eq(context.observed_format, TBE_GUEST_FORMAT_CSV);
      check_int_eq(context.observed_csv_row, 3);

      check_int_eq(Order_guest_from_xml(&bridge, json, strlen(json), guest_wire, sizeof(guest_wire),
                                        &output_len, &view),
                   TBE_GUEST_OK);
      check_int_eq(context.observed_format, TBE_GUEST_FORMAT_XML);

      check_int_eq(Order_guest_to_json(&bridge, &view, guest_text, sizeof(guest_text), &output_len),
                   TBE_GUEST_OK);
      check_mem_eq(guest_text, "json", output_len);
      check_int_eq(Order_guest_to_yaml(&bridge, &view, guest_text, sizeof(guest_text), &output_len),
                   TBE_GUEST_OK);
      check_mem_eq(guest_text, "yaml", output_len);
      check_int_eq(Order_guest_to_csv(&bridge, &view, guest_text, sizeof(guest_text), &output_len),
                   TBE_GUEST_OK);
      check_mem_eq(guest_text, "csv", output_len);
      check_int_eq(Order_guest_to_xml(&bridge, &view, guest_text, sizeof(guest_text), &output_len),
                   TBE_GUEST_OK);
      check_mem_eq(guest_text, "xml", output_len);

      bridge.abi_version = 0;
      check_int_eq(Order_guest_from_json(&bridge, json, strlen(json), guest_wire,
                                         sizeof(guest_wire), &output_len, &view),
                   TBE_GUEST_EBRIDGE);
      check_null(view.data);
      check_size_eq(view.size, 0);
      bridge.abi_version = TBE_GUEST_BRIDGE_ABI_VERSION;

      check_int_eq(
          Order_guest_from_json(&bridge, json, strlen(json), guest_wire, 1, &output_len, &view),
          TEST_BRIDGE_CAPACITY_ERROR);
      check_size_eq(output_len, encoded_len);
      tbe_typed_serialized_free(encoded);
    }
  }
}
