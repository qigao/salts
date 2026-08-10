/**
 * @file test_data_bind_public_api.c
 * @brief Third-party style smoke test for the public data_bind C API.
 */

#include "data_bind.h"
#include "tinytest.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static void write_schema_file(const char *path, const char *content) {
  FILE *f = fopen(path, "wb");
  check_not_null(f);
  if (!f) return;
  fwrite(content, 1, strlen(content), f);
  fclose(f);
}

typedef struct record_callback_state {
  size_t count;
  int ids[8];
  size_t stop_after;
  int fail;
} record_callback_state;

typedef struct serialized_output {
  char data[2048];
  size_t len;
} serialized_output;

typedef struct exact_record_state {
  size_t count;
  int64_t signed_values[2];
  uint64_t unsigned_values[2];
} exact_record_state;

static int collect_serialized(const void *data, size_t len, void *user) {
  serialized_output *output = (serialized_output *)user;
  if (!output || !data || len > sizeof(output->data) - output->len - 1) return -1;
  memcpy(output->data + output->len, data, len);
  output->len += len;
  output->data[output->len] = '\0';
  return 0;
}

static DataBindRecordAction collect_record(void *user_data, const DataBindValue *record,
                                           uint64_t record_index) {
  record_callback_state *state = (record_callback_state *)user_data;
  const DataBindValue *id = data_bind_value_get(record, "id");
  if (state == NULL || id == NULL || record_index != state->count ||
      state->count >= sizeof(state->ids) / sizeof(state->ids[0])) {
    return DATA_BIND_RECORD_ERROR;
  }
  state->ids[state->count++] = data_bind_value_as_int(id);
  if (state->fail) return DATA_BIND_RECORD_ERROR;
  if (state->stop_after != 0 && state->count >= state->stop_after) {
    return DATA_BIND_RECORD_STOP;
  }
  return DATA_BIND_RECORD_CONTINUE;
}

static DataBindRecordAction collect_exact_record(void *user_data, const DataBindValue *record,
                                                 uint64_t record_index) {
  exact_record_state *state = (exact_record_state *)user_data;
  if (state == NULL || record == NULL || record_index != state->count || state->count >= 2 ||
      data_bind_value_get_int64(data_bind_value_get(record, "min_value"),
                                &state->signed_values[state->count]) != DATA_BIND_OK ||
      data_bind_value_get_uint64(data_bind_value_get(record, "max_value"),
                                 &state->unsigned_values[state->count]) != DATA_BIND_OK) {
    return DATA_BIND_RECORD_ERROR;
  }
  state->count++;
  return DATA_BIND_RECORD_CONTINUE;
}

spec("data_bind public API") {
  it("should expose version and ABI metadata") {
    check_int_eq(data_bind_library_version(), DATA_BIND_VERSION);
    check_int_eq(data_bind_abi_version(), DATA_BIND_ABI_VERSION);
    check_str_eq(data_bind_version_string(), "2.1.0");
    check_str_eq(data_bind_status_name(DATA_BIND_ERR_TYPE_MISMATCH), "type_mismatch");
  }

  it("should bind JSON and XML through public opaque handles") {
    write_schema_file("test_public_api.tbe",
                      "enum Side <uint8> { Buy = 1; Sell = 2; }\n"
                      "message Order { uint32 id; Side side; bool active; string symbol; }\n");

    DataBind *codec = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    DataBindStatus status = data_bind_create("test_public_api.tbe", &codec, &err);
    check_int_eq(status, DATA_BIND_OK);
    check_not_null(codec);

    if (codec) {
      const char *json = "{\"id\":7,\"side\":\"Buy\",\"active\":true,\"symbol\":\"ABCD\"}";
      const char *xml =
          "<order id=\"8\" active=\"false\"><side>Sell</side><symbol>WXYZ</symbol></order>";
      DataBindValue *from_json = NULL;
      DataBindValue *from_xml = NULL;
      int32_t id = 0;
      int active = 0;
      const char *symbol = NULL;
      size_t symbol_len = 0;
      status = data_bind_parse_json(codec, "Order", json, strlen(json), &from_json, &err);
      check_int_eq(status, DATA_BIND_OK);
      status = data_bind_parse_xml(codec, "Order", xml, strlen(xml), &from_xml, &err);
      check_int_eq(status, DATA_BIND_OK);

      check_not_null(from_json);
      check_not_null(from_xml);
      check_int_eq(data_bind_value_get_int32(data_bind_value_get(from_json, "id"), &id),
                   DATA_BIND_OK);
      check_int_eq(id, 7);
      check_int_eq(data_bind_value_as_int(data_bind_value_get(from_json, "side")), 1);
      check(data_bind_value_kind(data_bind_value_get(from_json, "active")) == DATA_BIND_VALUE_BOOL);
      check_int_eq(data_bind_value_get_bool(data_bind_value_get(from_json, "active"), &active),
                   DATA_BIND_OK);
      check_int_eq(active, 1);
      check_int_eq(data_bind_value_get_string(data_bind_value_get(from_json, "symbol"), &symbol,
                                              &symbol_len),
                   DATA_BIND_OK);
      check_size_eq(symbol_len, 4);
      check_str_eq(symbol, "ABCD");
      check_int_eq(data_bind_value_as_int(data_bind_value_get(from_xml, "id")), 8);
      check_int_eq(data_bind_value_as_int(data_bind_value_get(from_xml, "side")), 2);
      check(data_bind_value_kind(data_bind_value_get(from_xml, "active")) == DATA_BIND_VALUE_BOOL);
      check_int_eq(data_bind_value_as_bool(data_bind_value_get(from_xml, "active")), 0);
      check_str_eq(data_bind_value_as_string(data_bind_value_get(from_xml, "symbol")), "WXYZ");

      data_bind_value_free(from_json);
      data_bind_value_free(from_xml);
      data_bind_free(codec);
    }

    remove("test_public_api.tbe");
  }

  it("should create codecs from schema text and report structured errors") {
    const char *schema = "message Order { uint32 id; string symbol; }\n";
    const char *json = "{\"id\":9,\"symbol\":\"TEXT\"}";
    DataBind *codec = NULL;
    DataBindValue *order = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    DataBindStatus status = data_bind_create_from_text(schema, strlen(schema), &codec, &err);
    check_int_eq(status, DATA_BIND_OK);
    check_not_null(codec);

    if (codec) {
      status = data_bind_parse_json(codec, "Order", json, strlen(json), &order, &err);
      check_int_eq(status, DATA_BIND_OK);
      check_not_null(order);
      check_int_eq(data_bind_value_as_int(data_bind_value_get(order, "id")), 9);
      data_bind_value_free(order);
      order = NULL;

      status = data_bind_parse_json(codec, "Missing", json, strlen(json), &order, &err);
      check_int_eq(status, DATA_BIND_ERR_TYPE_NOT_FOUND);
      check_null(order);
      check(err.message[0] != '\0');

      data_bind_free(codec);
    }
  }

  it("should strictly validate JSON CSV and XML documents") {
    const char *schema = "enum Side <uint8> { Buy = 1; Sell = 2; }\n"
                         "message Order { uint32 id; Side side; string symbol; }\n"
                         "union Choice { Side side; Order order; }\n";
    DataBind *codec = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    DataBindStatus status = data_bind_create_from_text(schema, strlen(schema), &codec, &err);
    check_int_eq(status, DATA_BIND_OK);
    check_not_null(codec);

    if (codec) {
      const char *json_good = "[{\"id\":1,\"side\":\"Buy\",\"symbol\":\"ABCD\"},"
                              "{\"id\":2,\"side\":\"Sell\",\"symbol\":\"WXYZ\"}]";
      const char *json_bad = "[{\"id\":1,\"side\":\"Buy\",\"symbol\":\"ABCD\"},"
                             "{\"id\":\"bad\",\"side\":\"Sell\",\"symbol\":\"WXYZ\"}]";
      const char *csv_good = "id,side,symbol\n"
                             "1,Buy,ABCD\n"
                             "2,Sell,WXYZ\n";
      const char *csv_bad = "id,side,symbol\n"
                            "1,Buy,ABCD\n"
                            "bad,Sell,WXYZ\n";
      const char *csv_union = "side,order.id,order.side,order.symbol\n"
                              "Buy,,,\n"
                              ",2,Sell,WXYZ\n";
      const char *xml_good =
          "<orders><order><id>1</id><side>Buy</side><symbol>ABCD</symbol></order>"
          "<order><id>2</id><side>Sell</side><symbol>WXYZ</symbol></order></orders>";
      const char *xml_bad =
          "<orders><order><id>1</id><side>Buy</side><symbol>ABCD</symbol></order>"
          "<order><id>bad</id><side>Sell</side><symbol>WXYZ</symbol></order></orders>";

      status = data_bind_validate_json(codec, "Order", json_good, strlen(json_good), &err);
      check_int_eq(status, DATA_BIND_OK);
      status = data_bind_validate_json(codec, "Order", json_bad, strlen(json_bad), &err);
      check_int_eq(status, DATA_BIND_ERR_TYPE_MISMATCH);

      status = data_bind_validate_csv(codec, "Order", csv_good, strlen(csv_good), &err);
      check_int_eq(status, DATA_BIND_OK);
      status = data_bind_validate_csv(codec, "Order", csv_bad, strlen(csv_bad), &err);
      check_int_eq(status, DATA_BIND_ERR_TYPE_MISMATCH);
      status = data_bind_validate_csv(codec, "Choice", csv_union, strlen(csv_union), &err);
      check_int_eq(status, DATA_BIND_OK);

      status =
          data_bind_validate_xml_path(codec, "Order", xml_good, strlen(xml_good), "//order", &err);
      check_int_eq(status, DATA_BIND_OK);
      status =
          data_bind_validate_xml_path(codec, "Order", xml_bad, strlen(xml_bad), "//order", &err);
      check_int_eq(status, DATA_BIND_ERR_TYPE_MISMATCH);

      data_bind_free(codec);
    }
  }

  it("should parse JSON, CSV, and XML documents from stream chunks") {
    const char *schema = "enum Side <uint8> { Buy = 1; Sell = 2; }\n"
                         "message Order { uint32 id; Side side; string symbol; }\n";
    DataBind *codec = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    DataBindStatus status = data_bind_create_from_text(schema, strlen(schema), &codec, &err);
    check_int_eq(status, DATA_BIND_OK);
    check_not_null(codec);

    if (codec) {
      data_bind_stream_t *stream = NULL;
      DataBindValue *value = NULL;
      size_t out_len = 0;
      const DataBindValue *item0 = NULL;
      const DataBindValue *item1 = NULL;
      stream = data_bind_stream_json_create(codec, "Order", &value, &err);
      check_not_null(stream);
      if (stream) {
        const char *json_front = "{\"id\":1,\"side\":\"Buy\"";
        const char *json_back = ",\"symbol\":\"ABCD\"}";
        status = data_bind_stream_feed(stream, json_front, strlen(json_front));
        check_int_eq(status, DATA_BIND_OK);
        status = data_bind_stream_feed(stream, json_back, strlen(json_back));
        check_int_eq(status, DATA_BIND_OK);
        status = data_bind_stream_finish(stream);
        check_int_eq(status, DATA_BIND_OK);
        check_not_null(value);
        check_int_eq(data_bind_value_as_int(data_bind_value_get(value, "id")), 1);
        check_int_eq(data_bind_value_as_int(data_bind_value_get(value, "side")), 1);
        check_str_eq(data_bind_value_as_string(data_bind_value_get(value, "symbol")), "ABCD");
        data_bind_value_free(value);
        value = NULL;
        data_bind_stream_destroy(stream);
      }

      stream = data_bind_stream_json_path_create(codec, "Order", "$", &value, &err);
      check_not_null(stream);
      if (stream) {
        const char *path_obj = "{\"id\":3,\"side\":\"Sell\",\"symbol\":\"XYZ\"}";
        status = data_bind_stream_feed(stream, path_obj, strlen(path_obj));
        check_int_eq(status, DATA_BIND_OK);
        status = data_bind_stream_finish(stream);
        check_int_eq(status, DATA_BIND_OK);
        check_not_null(value);
        check_int_eq(data_bind_value_as_int(data_bind_value_get(value, "id")), 3);
        data_bind_value_free(value);
        value = NULL;
        data_bind_stream_destroy(stream);
      }

      stream = data_bind_stream_json_path_all_create(codec, "Order", "$[*]", &value, &err);
      check_not_null(stream);
      if (stream) {
        const char *json_arr_front = "[{\"id\":3,\"side\":\"Sell\",\"symbol\":\"XYZ\"},";
        const char *json_arr_back = "{\"id\":4,\"side\":\"Buy\",\"symbol\":\"DEF\"}]";
        status = data_bind_stream_feed(stream, json_arr_front, strlen(json_arr_front));
        check_int_eq(status, DATA_BIND_OK);
        status = data_bind_stream_feed(stream, json_arr_back, strlen(json_arr_back));
        check_int_eq(status, DATA_BIND_OK);
        status = data_bind_stream_finish(stream);
        check_int_eq(status, DATA_BIND_OK);
        check_not_null(value);
        out_len = data_bind_value_count(value);
        check_size_eq(out_len, 2);
        item0 = data_bind_value_at(value, 0);
        item1 = data_bind_value_at(value, 1);
        check_int_eq(data_bind_value_as_int(data_bind_value_get(item0, "id")), 3);
        check_int_eq(data_bind_value_as_int(data_bind_value_get(item1, "id")), 4);
        data_bind_value_free(value);
        value = NULL;
        data_bind_stream_destroy(stream);
      }

      stream = data_bind_stream_json_path_all_create(codec, "Order", "$.orders[0,1]", &value,
                                                     &err);
      check_not_null(stream);
      if (stream) {
        const char *nested_front =
            "{\"orders\":[{\"id\":7,\"side\":\"Buy\",\"symbol\":\"N1\"},";
        const char *nested_back =
            "{\"id\":8,\"side\":\"Sell\",\"symbol\":\"N2\"}],\"ignored\":true}";
        check_int_eq(data_bind_stream_feed(stream, nested_front, strlen(nested_front)),
                     DATA_BIND_OK);
        check_int_eq(data_bind_stream_feed(stream, nested_back, strlen(nested_back)),
                     DATA_BIND_OK);
        check_int_eq(data_bind_stream_finish(stream), DATA_BIND_OK);
        check_not_null(value);
        check_size_eq(data_bind_value_count(value), 2);
        check_int_eq(data_bind_value_as_int(
                         data_bind_value_get(data_bind_value_at(value, 0), "id")),
                     7);
        check_int_eq(data_bind_value_as_int(
                         data_bind_value_get(data_bind_value_at(value, 1), "id")),
                     8);
        data_bind_value_free(value);
        value = NULL;
        data_bind_stream_destroy(stream);
      }

      stream = data_bind_stream_json_path_create(codec, "Order", "$.orders[1]", &value, &err);
      check_not_null(stream);
      if (stream) {
        const char *nested =
            "{\"orders\":[{\"id\":9,\"side\":\"Buy\",\"symbol\":\"N3\"},"
            "{\"id\":10,\"side\":\"Sell\",\"symbol\":\"N4\"}]}";
        check_int_eq(data_bind_stream_feed(stream, nested, strlen(nested)), DATA_BIND_OK);
        check_int_eq(data_bind_stream_finish(stream), DATA_BIND_OK);
        check_not_null(value);
        check_int_eq(data_bind_value_as_int(data_bind_value_get(value, "id")), 10);
        data_bind_value_free(value);
        value = NULL;
        data_bind_stream_destroy(stream);
      }

      stream = data_bind_stream_json_path_all_create(codec, "Order",
                                                     "$.orders[@.id >= 12]", &value, &err);
      check_not_null(stream);
      if (stream) {
        const char *filtered =
            "{\"orders\":[{\"id\":11,\"side\":\"Buy\",\"symbol\":\"F1\"},"
            "{\"id\":12,\"side\":\"Sell\",\"symbol\":\"F2\"}]}";
        check_int_eq(data_bind_stream_feed(stream, filtered, strlen(filtered)), DATA_BIND_OK);
        check_int_eq(data_bind_stream_finish(stream), DATA_BIND_OK);
        check_not_null(value);
        check_size_eq(data_bind_value_count(value), 1);
        check_int_eq(data_bind_value_as_int(
                         data_bind_value_get(data_bind_value_at(value, 0), "id")),
                     12);
        data_bind_value_free(value);
        value = NULL;
        data_bind_stream_destroy(stream);
      }

      stream = data_bind_stream_json_all_create(codec, "Order", &value, &err);
      check_not_null(stream);
      if (stream) {
        const char *json_parts[] = {"[",
                                    "{\"id\":5,",
                                    "\"side\":\"Buy\",",
                                    "\"symbol\":\"AAA\"}",
                                    ",",
                                    "{\"id\":6,\"side\":\"Sell\",\"symbol\":\"BBB\"}",
                                    "]"};
        size_t part_count = sizeof(json_parts) / sizeof(json_parts[0]);
        size_t part_index;
        for (part_index = 0; part_index < part_count; ++part_index) {
          status =
              data_bind_stream_feed(stream, json_parts[part_index], strlen(json_parts[part_index]));
          check_int_eq(status, DATA_BIND_OK);
        }
        status = data_bind_stream_finish(stream);
        check_int_eq(status, DATA_BIND_OK);
        check_not_null(value);
        check_size_eq(data_bind_value_count(value), 2);
        item0 = data_bind_value_at(value, 0);
        item1 = data_bind_value_at(value, 1);
        check_int_eq(data_bind_value_as_int(data_bind_value_get(item0, "id")), 5);
        check_str_eq(data_bind_value_as_string(data_bind_value_get(item1, "symbol")), "BBB");
        data_bind_value_free(value);
        value = NULL;
        data_bind_stream_destroy(stream);
      }

      stream = data_bind_stream_csv_all_create(codec, "Order", &value, &err);
      check_not_null(stream);
      if (stream) {
        const char *csv_head = "id,side,symbol\n1,Buy,ABCD\n";
        const char *csv_tail = "2,Sell,WXYZ\n";
        status = data_bind_stream_feed(stream, csv_head, strlen(csv_head));
        check_int_eq(status, DATA_BIND_OK);
        status = data_bind_stream_feed(stream, csv_tail, strlen(csv_tail));
        check_int_eq(status, DATA_BIND_OK);
        status = data_bind_stream_finish(stream);
        check_int_eq(status, DATA_BIND_OK);
        check_not_null(value);
        out_len = data_bind_value_count(value);
        check_size_eq(out_len, 2);
        data_bind_value_free(value);
        value = NULL;
        data_bind_stream_destroy(stream);
      }

      stream =
          data_bind_stream_csv_path_create(codec, "Order", "symbol == \"W,XYZ\"", &value, &err);
      check_not_null(stream);
      if (stream) {
        const char *csv_front = "id,side,symbol\n10,Buy,AB";
        const char *csv_back = "CD\n11,Sell,\"W,XYZ\"\n";
        status = data_bind_stream_feed(stream, csv_front, strlen(csv_front));
        check_int_eq(status, DATA_BIND_OK);
        status = data_bind_stream_feed(stream, csv_back, strlen(csv_back));
        check_int_eq(status, DATA_BIND_OK);
        status = data_bind_stream_finish(stream);
        check_int_eq(status, DATA_BIND_OK);
        check_not_null(value);
        out_len = data_bind_value_count(value);
        check_size_eq(out_len, 1);
        item0 = data_bind_value_at(value, 0);
        check_int_eq(data_bind_value_as_int(data_bind_value_get(item0, "id")), 11);
        check_str_eq(data_bind_value_as_string(data_bind_value_get(item0, "symbol")), "W,XYZ");
        data_bind_value_free(value);
        value = NULL;
        data_bind_stream_destroy(stream);
      }

      stream =
          data_bind_stream_csv_path_create(codec, "Order", "symbol == \"W\\\"XYZ\"", &value, &err);
      check_not_null(stream);
      if (stream) {
        const char *csv_front = "id,side,symbol\n12,Sell,\"W\"";
        const char *csv_back = "\"XYZ\"\n";
        status = data_bind_stream_feed(stream, csv_front, strlen(csv_front));
        check_int_eq(status, DATA_BIND_OK);
        status = data_bind_stream_feed(stream, csv_back, strlen(csv_back));
        check_int_eq(status, DATA_BIND_OK);
        status = data_bind_stream_finish(stream);
        check_int_eq(status, DATA_BIND_OK);
        check_not_null(value);
        check_size_eq(data_bind_value_count(value), 1);
        item0 = data_bind_value_at(value, 0);
        check_int_eq(data_bind_value_as_int(data_bind_value_get(item0, "id")), 12);
        check_str_eq(data_bind_value_as_string(data_bind_value_get(item0, "symbol")), "W\"XYZ");
        data_bind_value_free(value);
        value = NULL;
        data_bind_stream_destroy(stream);
      }

      stream = data_bind_stream_xml_path_all_create(codec, "Order", "//order", &value, &err);
      check_not_null(stream);
      if (stream) {
        const char *xml_front =
            "<orders><order><id>7</id><side>Buy</side><symbol>ABCD</symbol></order>";
        const char *xml_back =
            "<order><id>8</id><side>Sell</side><symbol>WXYZ</symbol></order></orders>";
        status = data_bind_stream_feed(stream, xml_front, strlen(xml_front));
        check_int_eq(status, DATA_BIND_OK);
        status = data_bind_stream_feed(stream, xml_back, strlen(xml_back));
        check_int_eq(status, DATA_BIND_OK);
        status = data_bind_stream_finish(stream);
        check_int_eq(status, DATA_BIND_OK);
        check_not_null(value);
        out_len = data_bind_value_count(value);
        check_size_eq(out_len, 2);
        data_bind_value_free(value);
        value = NULL;
        data_bind_stream_destroy(stream);
      }
      data_bind_free(codec);
    }
  }

  it("should reject invalid JSON YAML and XML stream chunks before binding") {
    const char *schema = "enum Side <uint8> { Buy = 1; Sell = 2; }\n"
                         "message Order { uint32 id; Side side; string symbol; }\n";
    DataBind *codec = NULL;
    data_bind_stream_t *stream = NULL;
    DataBindValue *value = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    DataBindStatus status = data_bind_create_from_text(schema, strlen(schema), &codec, &err);
    check_int_eq(status, DATA_BIND_OK);
    check_not_null(codec);

    if (codec) {
      stream = data_bind_stream_json_create(codec, "Order", &value, &err);
      check_not_null(stream);
      if (stream) {
        const char *json_front = "{\"id\":1,";
        const char *json_bad = "]";
        status = data_bind_stream_feed(stream, json_front, strlen(json_front));
        check_int_eq(status, DATA_BIND_OK);
        status = data_bind_stream_feed(stream, json_bad, strlen(json_bad));
        if (status == DATA_BIND_OK) {
          status = data_bind_stream_finish(stream);
        }
        check_int_eq(status, DATA_BIND_ERR_PARSE);
        check_int_eq(err.code, DATA_BIND_ERR_PARSE);
        data_bind_value_free(value);
        value = NULL;
        data_bind_stream_destroy(stream);
        stream = NULL;
      }

      stream = data_bind_stream_json_path_create(codec, "Order", "$.orders[", &value, &err);
      check_not_null(stream);
      if (stream) {
        const char *valid_json =
            "{\"orders\":[{\"id\":1,\"side\":\"Buy\",\"symbol\":\"A\"}]}";
        check_int_eq(data_bind_stream_feed(stream, valid_json, strlen(valid_json)), DATA_BIND_OK);
        check_int_eq(data_bind_stream_finish(stream), DATA_BIND_ERR_PARSE);
        check_int_eq(err.code, DATA_BIND_ERR_PARSE);
        check_null(value);
        data_bind_stream_destroy(stream);
        stream = NULL;
      }

      stream = data_bind_stream_yaml_create(codec, "Order", &value, &err);
      check_not_null(stream);
      if (stream) {
        const char yaml_bad[] = {'i', 'd', ':', ' ', -128, '\0'};
        status = data_bind_stream_feed(stream, yaml_bad, sizeof(yaml_bad));
        check_int_eq(status, DATA_BIND_ERR_PARSE);
        check_int_eq(err.code, DATA_BIND_ERR_PARSE);
        data_bind_value_free(value);
        value = NULL;
        data_bind_stream_destroy(stream);
        stream = NULL;
      }

      stream = data_bind_stream_xml_path_all_create(codec, "Order", "//order", &value, &err);
      check_not_null(stream);
      if (stream) {
        const char *xml_front = "<orders><order>";
        const char *xml_bad = "</orders>";
        status = data_bind_stream_feed(stream, xml_front, strlen(xml_front));
        check_int_eq(status, DATA_BIND_OK);
        status = data_bind_stream_feed(stream, xml_bad, strlen(xml_bad));
        if (status == DATA_BIND_OK) {
          status = data_bind_stream_finish(stream);
        }
        check_int_eq(status, DATA_BIND_ERR_PARSE);
        check_int_eq(err.code, DATA_BIND_ERR_PARSE);
        data_bind_value_free(value);
        value = NULL;
        data_bind_stream_destroy(stream);
      }

      data_bind_free(codec);
    }
  }

  it("should deep clone dynamic value ownership") {
    const char *schema =
        "composite Header { uint32 seq; uint64 ts; }\n"
        "message Book { Header header; list<uint32> values; map<string,int32> attrs; "
        "string symbol; }\n";
    const char *json = "{\"header\":{\"seq\":7,\"ts\":99},\"values\":[3,4],"
                       "\"attrs\":{\"x\":30,\"y\":40},\"symbol\":\"ABCD\"}";
    DataBind *codec = NULL;
    DataBindValue *source = NULL;
    DataBindValue *copy = NULL;
    DataBindValue *sentinel = (DataBindValue *)(uintptr_t)1u;
    DataBindError err = DATA_BIND_ERROR_INIT;
    DataBindStatus status;
    const DataBindValue *source_header;
    const DataBindValue *copy_header;
    DataBindMapEntry source_entry;
    DataBindMapEntry copy_entry;

    check_int_eq(data_bind_value_clone(NULL, &sentinel), DATA_BIND_ERR_INVALID_ARG);
    check_null(sentinel);
    check_int_eq(data_bind_value_clone((const DataBindValue *)(uintptr_t)1u, NULL),
                 DATA_BIND_ERR_INVALID_ARG);

    status = data_bind_create_from_text(schema, strlen(schema), &codec, &err);
    check_int_eq(status, DATA_BIND_OK);
    check_not_null(codec);
    if (codec) {
      status = data_bind_parse_json(codec, "Book", json, strlen(json), &source, &err);
      check_int_eq(status, DATA_BIND_OK);
      check_not_null(source);
      if (source) {
        source_header = data_bind_value_get(source, "header");
        source_entry = data_bind_value_map_entry_at(data_bind_value_get(source, "attrs"), 0u);
        check_int_eq(data_bind_value_clone(source, &copy), DATA_BIND_OK);
        check_not_null(copy);
        check_ptr_ne(copy, source);
        copy_header = data_bind_value_get(copy, "header");
        copy_entry = data_bind_value_map_entry_at(data_bind_value_get(copy, "attrs"), 0u);
        check_ptr_ne(copy_header, source_header);
        check_ptr_ne(copy_entry.key, source_entry.key);
        check_ptr_ne(data_bind_value_as_string(data_bind_value_get(copy, "symbol")),
                     data_bind_value_as_string(data_bind_value_get(source, "symbol")));
        data_bind_value_free(source);
        source = NULL;

        check_int_eq(data_bind_value_as_int(data_bind_value_get(copy_header, "seq")), 7);
        check_int_eq(data_bind_value_as_int64(data_bind_value_get(copy_header, "ts")), 99);
        check_size_eq(data_bind_value_count(data_bind_value_get(copy, "values")), 2u);
        check_int_eq(
            data_bind_value_as_int(data_bind_value_at(data_bind_value_get(copy, "values"), 1u)), 4);
        check_str_eq(copy_entry.key, "x");
        check_int_eq(data_bind_value_as_int(copy_entry.value), 30);
        check_str_eq(data_bind_value_as_string(data_bind_value_get(copy, "symbol")), "ABCD");
      }
      data_bind_value_free(source);
      data_bind_value_free(copy);
      data_bind_free(codec);
    }
  }

  it("should own and serialize schema-bound objects as lossless JSON") {
    const char *schema =
        "message Payload { int64 id; string emoji; bytes raw; map<string,int32> attrs; }\n";
    const char *json = "{\"id\":\"9007199254740993\",\"emoji\":\"\\uD83D\\uDE00\","
                       "\"raw\":\"Az\",\"attrs\":{\"x\":7}}";
    DataBind *codec = NULL;
    DataBindObject *object = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    char *serialized = NULL;
    DataBindObject *yaml_roundtrip = NULL;
    char *yaml = NULL;
    char *xml = NULL;
    size_t serialized_len = 0;
    serialized_output output = {0};

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    check_not_null(codec);
    if (codec) {
      check_int_eq(data_bind_object_from_json(codec, "Payload", json, strlen(json), &object, &err),
                   DATA_BIND_OK);
      check_not_null(object);
    }
    if (object) {
      check_str_eq(data_bind_object_type_name(object), "Payload");
      check_long_eq(
          data_bind_value_as_int64(data_bind_value_get(data_bind_object_value(object), "id")),
          INT64_C(9007199254740993));
      check_int_eq(data_bind_object_serialize_json(codec, object, &serialized, &serialized_len,
                                                   &err),
                   DATA_BIND_OK);
      check_not_null(serialized);
      check_str_contains(serialized, "\"id\":9007199254740993");
      check_str_contains(serialized, "\"emoji\":\"");
      check_str_contains(serialized, "\"raw\":\"Az\"");
      check(serialized_len == strlen(serialized));
      data_bind_serialized_free(serialized);

      check_int_eq(data_bind_object_serialize_yaml(codec, object, &yaml, NULL, &err), DATA_BIND_OK);
      check_str_contains(yaml, "9007199254740993");
      check_null(strstr(yaml, "\"9007199254740993\""));
      check_int_eq(data_bind_object_from_yaml(codec, "Payload", yaml, strlen(yaml),
                                              &yaml_roundtrip, &err),
                   DATA_BIND_OK);
      check_not_null(yaml_roundtrip);
      if (yaml_roundtrip) {
        check_long_eq(data_bind_value_as_int64(
                          data_bind_value_get(data_bind_object_value(yaml_roundtrip), "id")),
                      INT64_C(9007199254740993));
      }
      data_bind_object_free(yaml_roundtrip);
      data_bind_serialized_free(yaml);

      check_int_eq(data_bind_object_serialize_xml(codec, object, &xml, NULL, &err), DATA_BIND_OK);
      check_str_contains(xml, "<id>9007199254740993</id>");
      data_bind_serialized_free(xml);

      check_int_eq(data_bind_object_write_json(codec, object, collect_serialized, &output, &err),
                   DATA_BIND_OK);
      check_str_contains(output.data, "9007199254740993");
      data_bind_object_free(object);
    }
    data_bind_free(codec);
  }

  it("should serialize standard values to CSV and round trip nested paths") {
    const char *schema =
        "enum Side <u8> { Buy = 1; Sell = 2; } "
        "composite Meta { i32 seq; } "
        "message CsvRow { u64 id; bool active; double ratio; uuid uid; datetime created; "
        "date day; time at; duration span; decimal price; bigint total; money cost; Meta meta; i16[2] fixed; "
        "Side side; string note; bytes raw; list<u32> values; set<i8> tags; "
        "map<string,i64> attrs; }";
    const char *json =
        "{\"id\":18446744073709551615,\"active\":true,\"ratio\":1.25,"
        "\"uid\":\"01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001\","
        "\"created\":\"Sat, 04 Mar 2006 13:27:54 GMT\","
        "\"day\":\"2026-07-15\",\"at\":\"09:30:05.123\",\"span\":\"1h2m3s\","
        "\"price\":\"12.3400\",\"total\":\"123456789012345678901234567890\","
        "\"cost\":{\"amount\":\"12.34\",\"currency\":\"USD\"},"
        "\"meta\":{\"seq\":7},\"fixed\":[-1,32767],\"side\":\"Sell\","
        "\"note\":\"A,\\\"B\\\"\\nC\",\"raw\":\"Az\",\"values\":[1,4294967295],"
        "\"tags\":[-128,127],\"attrs\":{\"min_value\":-9223372036854775808}}";
    DataBind *codec = NULL;
    DataBindObject *object = NULL;
    DataBindObject *roundtrip = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    serialized_output output = {0};
    char *csv = NULL;
    size_t csv_len = 0;
    uint64_t unsigned_value = 0;
    int64_t signed_value = 0;

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    if (codec) {
      check_int_eq(data_bind_object_from_json(codec, "CsvRow", json, strlen(json), &object, &err),
                   DATA_BIND_OK);
      check_not_null(object);
    }
    if (object) {
      const DataBindValue *root;
      DataBindMapEntry entry;
      char text[128];
      size_t bytes_len = 0;
      check_int_eq(data_bind_object_serialize_csv(codec, object, &csv, &csv_len, &err),
                   DATA_BIND_OK);
      check_not_null(csv);
      check_size_eq(csv_len, strlen(csv));
      check_str_contains(csv, "meta.seq");
      check_str_contains(csv, "fixed[1]");
      check_str_contains(csv, "values[1]");
      check_str_contains(csv, "attrs.min_value");
      check_str_contains(csv, "18446744073709551615");
      check_str_contains(csv, "-9223372036854775808");
      check_str_contains(csv, "\"A,\"\"B\"\"");
      check(csv_len >= 2 && csv[csv_len - 2] == '\r' && csv[csv_len - 1] == '\n');

      check_int_eq(data_bind_object_from_csv(codec, "CsvRow", csv, csv_len, 0, &roundtrip, &err),
                   DATA_BIND_OK);
      check_not_null(roundtrip);
      root = data_bind_object_value(roundtrip);
      check_int_eq(data_bind_value_get_uint64(data_bind_value_get(root, "id"), &unsigned_value),
                   DATA_BIND_OK);
      check(unsigned_value == UINT64_MAX);
      check_true(data_bind_value_as_bool(data_bind_value_get(root, "active")));
      check_double_eq(data_bind_value_as_double(data_bind_value_get(root, "ratio")), 1.25, 0.0);
      check_str_eq(data_bind_value_as_uuid_string(data_bind_value_get(root, "uid"), text,
                                                  sizeof(text)),
                   "01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001");
      check_str_eq(data_bind_value_as_datetime_string(data_bind_value_get(root, "created"), text,
                                                      sizeof(text)),
                   "Sat, 04 Mar 2006 13:27:54 GMT");
      check_str_eq(data_bind_value_as_date_string(data_bind_value_get(root, "day"), text,
                                                  sizeof(text)),
                   "2026-07-15");
      check_str_eq(data_bind_value_as_time_string(data_bind_value_get(root, "at"), text,
                                                  sizeof(text)),
                   "09:30:05.123");
      check_int_eq((int)data_bind_value_as_duration_milliseconds(
                       data_bind_value_get(root, "span")),
                   3723000);
      check_str_eq(data_bind_value_as_decimal_string(data_bind_value_get(root, "price"), text,
                                                     sizeof(text)),
                   "12.34");
      check_str_eq(data_bind_value_as_bigint_string(data_bind_value_get(root, "total")),
                   "123456789012345678901234567890");
      check_str_eq(data_bind_value_as_money_string(data_bind_value_get(root, "cost"), text,
                                                   sizeof(text)),
                   "USD 12.34");
      check_str_eq(data_bind_value_as_string(data_bind_value_get(root, "note")), "A,\"B\"\nC");
      check_mem_eq(data_bind_value_as_bytes(data_bind_value_get(root, "raw"), &bytes_len), "Az",
                   2u);
      check_size_eq(bytes_len, 2u);
      check_int_eq(data_bind_value_as_int(
                       data_bind_value_get(data_bind_value_get(root, "meta"), "seq")),
                   7);
      check_int_eq(data_bind_value_as_int(data_bind_value_at(data_bind_value_get(root, "fixed"),
                                                              1u)),
                   32767);
      check_int_eq(data_bind_value_get_uint64(
                       data_bind_value_at(data_bind_value_get(root, "values"), 1u),
                       &unsigned_value),
                   DATA_BIND_OK);
      check(unsigned_value == UINT32_MAX);
      check_int_eq(data_bind_value_as_int(data_bind_value_get(root, "side")), 2);
      check_size_eq(data_bind_value_count(data_bind_value_get(root, "tags")), 2u);
      entry = data_bind_value_map_entry_at(data_bind_value_get(root, "attrs"), 0u);
      check_str_eq(entry.key, "min_value");
      check_int_eq(data_bind_value_get_int64(entry.value, &signed_value), DATA_BIND_OK);
      check(signed_value == INT64_MIN);

      check_int_eq(data_bind_object_write_csv(codec, object, collect_serialized, &output, &err),
                   DATA_BIND_OK);
      check_size_eq(output.len, csv_len);
      check_str_eq(output.data, csv);
      check_int_eq(data_bind_object_serialize_csv(codec, object, NULL, NULL, &err),
                   DATA_BIND_ERR_INVALID_ARG);
    }
    data_bind_serialized_free(csv);
    data_bind_object_free(roundtrip);
    data_bind_object_free(object);
    data_bind_free(codec);
  }

  it("should reject CSV values that have no lossless column representation") {
    const char *schema = "message Empty { list<i32> values; }";
    const char *json = "{\"values\":[]}";
    DataBind *codec = NULL;
    DataBindObject *object = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    char *csv = NULL;

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    if (codec) {
      check_int_eq(data_bind_object_from_json(codec, "Empty", json, strlen(json), &object, &err),
                   DATA_BIND_OK);
    }
    if (object) {
      check_int_eq(data_bind_object_serialize_csv(codec, object, &csv, NULL, &err),
                   DATA_BIND_ERR_TYPE_MISMATCH);
      check_null(csv);
      check_str_contains(err.message, "losslessly");
    }
    data_bind_object_free(object);
    data_bind_free(codec);
  }

  it("should create the same object handle from YAML and XML") {
    const char *schema = "message Item { int32 id; string name; }\n";
    const char *yaml = "id: 3\nname: yaml\n";
    const char *xml = "<Item><id>4</id><name>xml</name></Item>";
    DataBind *codec = NULL;
    DataBindObject *object = NULL;
    DataBindObject *roundtrip = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    char *serialized = NULL;

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    if (codec) {
      check_int_eq(data_bind_object_from_yaml(codec, "Item", yaml, strlen(yaml), &object, &err),
                   DATA_BIND_OK);
      check_str_eq(
          data_bind_value_as_string(data_bind_value_get(data_bind_object_value(object), "name")),
          "yaml");
      check_int_eq(data_bind_object_serialize_yaml(codec, object, &serialized, NULL, &err),
                   DATA_BIND_OK);
      check_int_eq(data_bind_object_from_yaml(codec, "Item", serialized, strlen(serialized),
                                              &roundtrip, &err),
                   DATA_BIND_OK);
      check_str_eq(
          data_bind_value_as_string(data_bind_value_get(data_bind_object_value(roundtrip), "name")),
          "yaml");
      data_bind_serialized_free(serialized);
      data_bind_object_free(roundtrip);
      data_bind_object_free(object);
      object = NULL;
      roundtrip = NULL;

      check_int_eq(data_bind_object_from_xml(codec, "Item", xml, strlen(xml), &object, &err),
                   DATA_BIND_OK);
      check_str_eq(
          data_bind_value_as_string(data_bind_value_get(data_bind_object_value(object), "name")),
          "xml");
      check_int_eq(data_bind_object_serialize_xml(codec, object, &serialized, NULL, &err),
                   DATA_BIND_OK);
      check_int_eq(data_bind_object_from_xml(codec, "Item", serialized, strlen(serialized),
                                             &roundtrip, &err),
                   DATA_BIND_OK);
      check_str_eq(
          data_bind_value_as_string(data_bind_value_get(data_bind_object_value(roundtrip), "name")),
          "xml");
      data_bind_serialized_free(serialized);
      data_bind_object_free(roundtrip);
      data_bind_object_free(object);
      data_bind_free(codec);
    }
  }

  it("should preserve uint64 max, reject fractional integers, parse time, and serialize bigint XML") {
    const char *schema =
        "message Exact { uint64 id; int32 count; time at; bigint huge; }\n";
    const char *huge =
        "12345678901234567890123456789012345678901234567890123456789012345678901234567890"
        "12345678901234567890123456789012345678901234567890123456789012345678901234567890";
    char json[512];
    char fractional[512];
    DataBind *codec = NULL;
    DataBindObject *object = NULL;
    DataBindValue *value = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    const DataBindValue *id;
    DataBindTime time = {0};
    uint64_t exact = 0;
    int has_bits = 0;
    char *serialized = NULL;

    snprintf(json, sizeof(json),
             "{\"id\":18446744073709551615,\"count\":7,\"at\":\"09:30:05\",\"huge\":\"%s\"}",
             huge);
    snprintf(fractional, sizeof(fractional),
             "{\"id\":1,\"count\":1.5,\"at\":\"09:30\",\"huge\":\"%s\"}", huge);
    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    if (codec) {
      check_int_eq(data_bind_object_from_json(codec, "Exact", json, strlen(json), &object, &err),
                   DATA_BIND_OK);
      check_not_null(object);
      if (object) {
        id = data_bind_value_get(data_bind_object_value(object), "id");
        check(data_bind_value_kind(id) == DATA_BIND_VALUE_UINT64);
        check_int_eq(data_bind_value_get_uint64(id, &exact), DATA_BIND_OK);
        check(exact == UINT64_MAX);
        check_int_eq(data_bind_value_has_any_bits(id, UINT64_C(1) << 63, &has_bits), DATA_BIND_OK);
        check_true(has_bits);
        check_int_eq(data_bind_value_has_all_bits(id, UINT64_C(0x8000000000000001), &has_bits),
                     DATA_BIND_OK);
        check_true(has_bits);
        check_int_eq(data_bind_value_has_all_bits(id, 0, &has_bits), DATA_BIND_ERR_INVALID_ARG);
        check_int_eq(data_bind_value_get_time(
                         data_bind_value_get(data_bind_object_value(object), "at"), &time),
                     DATA_BIND_OK);
        check_int_eq(time.hour, 9);
        check_int_eq(time.minute, 30);
        check_int_eq(time.second, 5);
        check_int_eq(data_bind_object_serialize_xml(codec, object, &serialized, NULL, &err),
                     DATA_BIND_OK);
        check_str_contains(serialized, huge);
        data_bind_serialized_free(serialized);
      }
      check_int_eq(data_bind_parse_json(codec, "Exact", fractional, strlen(fractional), &value,
                                        &err),
                   DATA_BIND_ERR_TYPE_MISMATCH);
      check_null(value);
      data_bind_object_free(object);
      data_bind_free(codec);
    }
  }

  it("should own binary and CSV objects and deep clone them") {
    const char *schema = "message Item { uint32 id; string name; }\n";
    const char *csv = "id,name\n4,csv\n";
    uint8_t bin[11] = {5, 0, 0, 0, 3, 0, 0, 0, 'b', 'i', 'n'};
    DataBind *codec = NULL;
    DataBindObject *object = NULL;
    DataBindObject *copy = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    if (codec) {
      check_int_eq(data_bind_object_from_bin(codec, "Item", bin, sizeof(bin), &object, &err),
                   DATA_BIND_OK);
      check_int_eq(data_bind_object_clone(object, &copy), DATA_BIND_OK);
      check_ptr_ne(data_bind_object_value(object), data_bind_object_value(copy));
      check_int_eq(data_bind_value_as_int(data_bind_value_get(data_bind_object_value(copy), "id")),
                   5);
      data_bind_object_free(copy);
      data_bind_object_free(object);
      object = NULL;
      copy = NULL;

      check_int_eq(data_bind_object_from_csv(codec, "Item", csv, strlen(csv), 0, &object, &err),
                   DATA_BIND_OK);
      check_str_eq(
          data_bind_value_as_string(data_bind_value_get(data_bind_object_value(object), "name")),
          "csv");
      data_bind_object_free(object);
      data_bind_free(codec);
    }
  }

  it("should serialize an owned object to the schema binary wire format") {
    const char *schema = "enum Side <uint8> { Buy = 1; Sell = 2; }\n"
                         "message Order { uint32 id; Side side; bool active; string symbol; }\n";
    const char *json = "{\"id\":7,\"side\":\"Buy\",\"active\":true,\"symbol\":\"ABCD\"}";
    const uint8_t expected[] = {7, 0, 0, 0, 1, 1, 4, 0, 0, 0, 'A', 'B', 'C', 'D'};
    DataBind *codec = NULL;
    DataBindObject *object = NULL;
    DataBindObject *roundtrip = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    uint8_t *wire = NULL;
    size_t wire_len = 0;

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    if (codec) {
      check_int_eq(data_bind_object_from_json(codec, "Order", json, strlen(json), &object, &err),
                   DATA_BIND_OK);
      check_int_eq(data_bind_object_serialize_bin(codec, object, &wire, &wire_len, &err),
                   DATA_BIND_OK);
      check_size_eq(wire_len, sizeof(expected));
      if (wire && wire_len == sizeof(expected)) check_mem_eq(wire, expected, sizeof(expected));
      check_int_eq(data_bind_object_from_bin(codec, "Order", wire, wire_len, &roundtrip, &err),
                   DATA_BIND_OK);
      if (roundtrip) {
        const DataBindValue *value = data_bind_object_value(roundtrip);
        check(data_bind_value_kind(data_bind_value_get(value, "id")) == DATA_BIND_VALUE_INT);
        check_int_eq(data_bind_value_as_int(data_bind_value_get(value, "id")), 7);
        check_int_eq(data_bind_value_as_int(data_bind_value_get(value, "side")), 1);
        check_int_eq(data_bind_value_as_bool(data_bind_value_get(value, "active")), 1);
        check_str_eq(data_bind_value_as_string(data_bind_value_get(value, "symbol")), "ABCD");
      }
      data_bind_binary_free(wire);
      data_bind_object_free(roundtrip);
      data_bind_object_free(object);
      data_bind_free(codec);
    }
  }

  it("should round-trip binary fixed and variable collections") {
    const char *schema = "message Values { uint32[2] fixed; list<uint32> values; "
                         "set<uint32> ids; map<string,uint32> attrs; "
                         "set<uint64> large_ids; map<string,uint64> large_attrs; bytes raw; }\n";
    const char *json =
        "{\"fixed\":[11,4294967295],\"values\":[3,4,4294967295],"
        "\"ids\":[7,4294967295],\"attrs\":{\"max\":4294967295},"
        "\"large_ids\":[9,18446744073709551615],"
        "\"large_attrs\":{\"max\":18446744073709551615},\"raw\":\"Az\"}";
    DataBind *codec = NULL;
    DataBindObject *object = NULL;
    DataBindObject *roundtrip = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    uint8_t *wire = NULL;
    size_t wire_len = 0;

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    if (codec) {
      const DataBindValue *root;
      const DataBindValue *values;
      const DataBindValue *ids;
      const DataBindValue *attrs;
      const DataBindValue *large_ids;
      const DataBindValue *large_attrs;
      DataBindMapEntry entry;
      uint64_t exact = 0;
      size_t raw_len = 0;
      check_int_eq(data_bind_object_from_json(codec, "Values", json, strlen(json), &object, &err),
                   DATA_BIND_OK);
      check_int_eq(data_bind_object_serialize_bin(codec, object, &wire, &wire_len, &err),
                   DATA_BIND_OK);
      check_int_eq(data_bind_object_from_bin(codec, "Values", wire, wire_len, &roundtrip, &err),
                   DATA_BIND_OK);
      root = data_bind_object_value(roundtrip);
      values = data_bind_value_get(root, "values");
      ids = data_bind_value_get(root, "ids");
      attrs = data_bind_value_get(root, "attrs");
      large_ids = data_bind_value_get(root, "large_ids");
      large_attrs = data_bind_value_get(root, "large_attrs");
      check_size_eq(data_bind_value_count(data_bind_value_get(root, "fixed")), 2u);
      check(data_bind_value_as_int64(data_bind_value_at(data_bind_value_get(root, "fixed"), 1u)) ==
            INT64_C(4294967295));
      check(data_bind_value_as_int64(data_bind_value_at(values, 2u)) == INT64_C(4294967295));
      check(data_bind_value_as_int64(data_bind_value_at(ids, 1u)) == INT64_C(4294967295));
      entry = data_bind_value_map_entry_at(attrs, 0u);
      check_str_eq(entry.key, "max");
      check(data_bind_value_as_int64(entry.value) == INT64_C(4294967295));
      check_int_eq(data_bind_value_get_uint64(data_bind_value_at(large_ids, 1u), &exact),
                   DATA_BIND_OK);
      check(exact == UINT64_MAX);
      entry = data_bind_value_map_entry_at(large_attrs, 0u);
      check_str_eq(entry.key, "max");
      check_int_eq(data_bind_value_get_uint64(entry.value, &exact), DATA_BIND_OK);
      check(exact == UINT64_MAX);
      check_mem_eq(data_bind_value_as_bytes(data_bind_value_get(root, "raw"), &raw_len), "Az", 2u);
      check_size_eq(raw_len, 2u);
      data_bind_binary_free(wire);
      data_bind_object_free(roundtrip);
      data_bind_object_free(object);
      data_bind_free(codec);
    }
  }

  it("should support all short integer aliases across text and binary containers") {
    const char *schema =
        "message Aliases { i8 a; u8 b; i16 c; u16 d; i32 e; u32 f; i64 g; u64 h; "
        "u16[2] fixed; list<i16> signed_list; set<u32> unsigned_set; "
        "map<string,i64> signed_map; }\n";
    const char *json =
        "{\"a\":-128,\"b\":255,\"c\":-32768,\"d\":65535,"
        "\"e\":-2147483648,\"f\":4294967295,"
        "\"g\":-9223372036854775808,\"h\":18446744073709551615,"
        "\"fixed\":[1,65535],\"signed_list\":[-1,-32768],"
        "\"unsigned_set\":[1,4294967295],\"signed_map\":{\"min\":-9223372036854775808}}";
    DataBind *codec = NULL;
    DataBindObject *object = NULL;
    DataBindObject *roundtrip = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    uint8_t *wire = NULL;
    size_t wire_len = 0;

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    if (codec) {
      const DataBindValue *root;
      const DataBindValue *fixed;
      const DataBindValue *signed_list;
      const DataBindValue *unsigned_set;
      DataBindMapEntry entry;
      int64_t signed_value = 0;
      uint64_t unsigned_value = 0;

      check_int_eq(data_bind_object_from_json(codec, "Aliases", json, strlen(json), &object, &err),
                   DATA_BIND_OK);
      check_int_eq(data_bind_object_serialize_bin(codec, object, &wire, &wire_len, &err),
                   DATA_BIND_OK);
      check_int_eq(data_bind_object_from_bin(codec, "Aliases", wire, wire_len, &roundtrip, &err),
                   DATA_BIND_OK);

      root = data_bind_object_value(roundtrip);
      check_int_eq(data_bind_value_as_int(data_bind_value_get(root, "a")), -128);
      check_int_eq(data_bind_value_as_int(data_bind_value_get(root, "b")), 255);
      check_int_eq(data_bind_value_as_int(data_bind_value_get(root, "c")), -32768);
      check_int_eq(data_bind_value_as_int(data_bind_value_get(root, "d")), 65535);
      check_int_eq(data_bind_value_get_int64(data_bind_value_get(root, "e"), &signed_value),
                   DATA_BIND_OK);
      check(signed_value == INT64_C(-2147483648));
      check_int_eq(data_bind_value_get_uint64(data_bind_value_get(root, "f"), &unsigned_value),
                   DATA_BIND_OK);
      check(unsigned_value == UINT64_C(4294967295));
      check_int_eq(data_bind_value_get_int64(data_bind_value_get(root, "g"), &signed_value),
                   DATA_BIND_OK);
      check(signed_value == INT64_MIN);
      check_int_eq(data_bind_value_get_uint64(data_bind_value_get(root, "h"), &unsigned_value),
                   DATA_BIND_OK);
      check(unsigned_value == UINT64_MAX);

      fixed = data_bind_value_get(root, "fixed");
      signed_list = data_bind_value_get(root, "signed_list");
      unsigned_set = data_bind_value_get(root, "unsigned_set");
      check_size_eq(data_bind_value_count(fixed), 2u);
      check_int_eq(data_bind_value_as_int(data_bind_value_at(fixed, 1u)), 65535);
      check_int_eq(data_bind_value_as_int(data_bind_value_at(signed_list, 1u)), -32768);
      check_int_eq(data_bind_value_get_uint64(data_bind_value_at(unsigned_set, 1u),
                                              &unsigned_value),
                   DATA_BIND_OK);
      check(unsigned_value == UINT64_C(4294967295));
      entry = data_bind_value_map_entry_at(data_bind_value_get(root, "signed_map"), 0u);
      check_str_eq(entry.key, "min");
      check_int_eq(data_bind_value_get_int64(entry.value, &signed_value), DATA_BIND_OK);
      check(signed_value == INT64_MIN);

      data_bind_binary_free(wire);
      data_bind_object_free(roundtrip);
      data_bind_object_free(object);
      data_bind_free(codec);
    }
  }

  it("should reject a truncated map string value after reading its key") {
    const char *schema = "message Values { map<string,string> attrs; }\n";
    const uint8_t wire[] = {1, 0, 0, 0, 1, 0, 0, 0, 'k', 4, 0, 0, 0, 'a', 'b'};
    DataBind *codec = NULL;
    DataBindObject *object = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    if (codec) {
      check(data_bind_object_from_bin(codec, "Values", wire, sizeof(wire), &object, &err) !=
            DATA_BIND_OK);
      check_null(object);
      data_bind_free(codec);
    }
  }

  it("should round-trip binary composite and repeating group fields") {
    const char *schema = "composite Header { uint32 seq; uint64 ts; }\n"
                         "group Level { uint64 price; uint32 qty; }\n"
                         "message Book { Header header; group<Level> bids; string symbol; }\n";
    const char *json = "{\"header\":{\"seq\":7,\"ts\":9},"
                       "\"bids\":[{\"price\":100,\"qty\":10},{\"price\":200,\"qty\":20}],"
                       "\"symbol\":\"ABCD\"}";
    DataBind *codec = NULL;
    DataBindObject *object = NULL;
    DataBindObject *roundtrip = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    uint8_t *wire = NULL;
    size_t wire_len = 0;

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    if (codec) {
      const DataBindValue *root;
      const DataBindValue *bids;
      check_int_eq(data_bind_object_from_json(codec, "Book", json, strlen(json), &object, &err),
                   DATA_BIND_OK);
      check_int_eq(data_bind_object_serialize_bin(codec, object, &wire, &wire_len, &err),
                   DATA_BIND_OK);
      check_int_eq(data_bind_object_from_bin(codec, "Book", wire, wire_len, &roundtrip, &err),
                   DATA_BIND_OK);
      root = data_bind_object_value(roundtrip);
      bids = data_bind_value_get(root, "bids");
      check_int_eq(data_bind_value_as_int(
                       data_bind_value_get(data_bind_value_get(root, "header"), "seq")),
                   7);
      check_int_eq(data_bind_value_as_int64(
                       data_bind_value_get(data_bind_value_get(root, "header"), "ts")),
                   9);
      check_size_eq(data_bind_value_count(bids), 2u);
      check_int_eq(data_bind_value_as_int64(
                       data_bind_value_get(data_bind_value_at(bids, 1u), "price")),
                   200);
      check_str_eq(data_bind_value_as_string(data_bind_value_get(root, "symbol")), "ABCD");
      data_bind_binary_free(wire);
      data_bind_object_free(roundtrip);
      data_bind_object_free(object);
      data_bind_free(codec);
    }
  }

  it("should report required binary capacity without modifying a short buffer") {
    const char *schema = "message Item { uint32 id; string name; }\n";
    const char *json = "{\"id\":5,\"name\":\"bin\"}";
    const uint8_t guard[] = {0xa5, 0xa5, 0xa5, 0xa5};
    DataBind *codec = NULL;
    DataBindObject *object = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    uint8_t output[11];
    size_t wire_len = 0;

    memset(output, 0xa5, sizeof(output));
    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    if (codec) {
      check_int_eq(data_bind_object_from_json(codec, "Item", json, strlen(json), &object, &err),
                   DATA_BIND_OK);
      check_int_eq(data_bind_object_serialize_bin_into(codec, object, output, sizeof(guard),
                                                       &wire_len, &err),
                   DATA_BIND_ERR_INVALID_ARG);
      check_size_eq(wire_len, sizeof(output));
      check_mem_eq(output, guard, sizeof(guard));
      check_int_eq(data_bind_object_serialize_bin_into(codec, object, output, sizeof(output),
                                                       &wire_len, &err),
                   DATA_BIND_OK);
      check_size_eq(wire_len, sizeof(output));
      data_bind_object_free(object);
      data_bind_free(codec);
    }
  }

  it("should reject binary serialization without a compatible wire schema") {
    const char *optional_schema = "message Item { optional uint32 id; }\n";
    const char *other_schema = "message Other { uint32 id; }\n";
    const char *json = "{\"id\":5}";
    DataBind *codec = NULL;
    DataBind *other = NULL;
    DataBindObject *object = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    uint8_t *wire = NULL;
    size_t wire_len = 0;

    check_int_eq(data_bind_create_from_text(optional_schema, strlen(optional_schema), &codec, &err),
                 DATA_BIND_OK);
    check_int_eq(data_bind_create_from_text(other_schema, strlen(other_schema), &other, &err),
                 DATA_BIND_OK);
    if (codec && other) {
      check_int_eq(data_bind_object_from_json(codec, "Item", json, strlen(json), &object, &err),
                   DATA_BIND_OK);
      check_int_eq(data_bind_object_serialize_bin(codec, object, &wire, &wire_len, &err),
                   DATA_BIND_ERR_SCHEMA);
      check_null(wire);
      check_size_eq(wire_len, 0u);
      check_int_eq(data_bind_object_serialize_bin(other, object, &wire, &wire_len, &err),
                   DATA_BIND_ERR_TYPE_NOT_FOUND);
    }
    data_bind_object_free(object);
    data_bind_free(other);
    data_bind_free(codec);
  }

  it("should deep clone set and bytes storage") {
    const char *schema = "message Values { set<string> tags; bytes raw; }\n";
    const char *json = "{\"tags\":[\"alpha\",\"beta\"],\"raw\":\"Az\"}";
    DataBind *codec = NULL;
    DataBindValue *source = NULL;
    DataBindValue *copy = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    const DataBindValue *source_tags;
    const DataBindValue *copy_tags;
    const uint8_t *source_bytes;
    const uint8_t *copy_bytes;
    size_t source_len = 0;
    size_t copy_len = 0;

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    check_not_null(codec);
    if (codec) {
      check_int_eq(data_bind_parse_json(codec, "Values", json, strlen(json), &source, &err),
                   DATA_BIND_OK);
      check_not_null(source);
      if (source) {
        source_tags = data_bind_value_get(source, "tags");
        source_bytes = data_bind_value_as_bytes(data_bind_value_get(source, "raw"), &source_len);
        check_int_eq(data_bind_value_clone(source, &copy), DATA_BIND_OK);
        check_not_null(copy);
        copy_tags = data_bind_value_get(copy, "tags");
        copy_bytes = data_bind_value_as_bytes(data_bind_value_get(copy, "raw"), &copy_len);
        check(data_bind_value_kind(copy_tags) == DATA_BIND_VALUE_SET);
        check_size_eq(data_bind_value_count(copy_tags), 2u);
        check_ptr_ne(copy_tags, source_tags);
        check_ptr_ne(data_bind_value_as_string(data_bind_value_at(copy_tags, 0u)),
                     data_bind_value_as_string(data_bind_value_at(source_tags, 0u)));
        check_size_eq(copy_len, source_len);
        check_ptr_ne(copy_bytes, source_bytes);
        check_mem_eq(copy_bytes, source_bytes, source_len);
      }
      data_bind_value_free(source);
      source = NULL;
      if (copy) {
        check_str_eq(
            data_bind_value_as_string(data_bind_value_at(data_bind_value_get(copy, "tags"), 1u)),
            "beta");
        copy_bytes = data_bind_value_as_bytes(data_bind_value_get(copy, "raw"), &copy_len);
        check_size_eq(copy_len, 2u);
        check_mem_eq(copy_bytes, "Az", 2u);
      }
      data_bind_value_free(copy);
      data_bind_free(codec);
    }
  }

  it("should clone UUID temporal decimal bigint and money values") {
    const char *schema = "message Scalars { uuid id; datetime at; date d; time t; duration span; "
                         "decimal price; bigint count; money total; }\n";
    const char *json = "{\"id\":\"01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001\","
                       "\"at\":\"Sat, 04 Mar 2006 13:27:54 GMT\",\"d\":\"2026-06-28\","
                       "\"t\":\"09:30:05.123\",\"span\":\"1h30m5s250ms\",\"price\":\"123.4500\","
                       "\"count\":\"000123456789012345678901234567890\","
                       "\"total\":{\"amount\":\"99.9900\",\"currency\":\"EUR\"}}";
    DataBind *codec = NULL;
    DataBindValue *source = NULL;
    DataBindValue *copy = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    DataBindDate date;
    DataBindTime time;
    DataBindDecimal decimal;
    DataBindMoney money;
    turbo_uuid_t uuid;
    char text[64];
    const char *source_bigint;

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    check_not_null(codec);
    if (codec) {
      check_int_eq(data_bind_parse_json(codec, "Scalars", json, strlen(json), &source, &err),
                   DATA_BIND_OK);
      check_not_null(source);
      if (source) {
        source_bigint = data_bind_value_as_bigint_string(data_bind_value_get(source, "count"));
        check_int_eq(data_bind_value_clone(source, &copy), DATA_BIND_OK);
        check_not_null(copy);
        check_ptr_ne(copy, source);
        check_ptr_ne(data_bind_value_as_bigint_string(data_bind_value_get(copy, "count")),
                     source_bigint);
      }
      data_bind_value_free(source);
      source = NULL;
      if (copy) {
        check(data_bind_value_as_uuid(data_bind_value_get(copy, "id"), uuid.bytes));
        check_str_eq(
            data_bind_value_as_uuid_string(data_bind_value_get(copy, "id"), text, sizeof(text)),
            "01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001");
        check_double_eq(data_bind_value_as_datetime_timestamp(data_bind_value_get(copy, "at")),
                        1141478874.0, 0.001);
        check_int_eq(data_bind_value_get_date(data_bind_value_get(copy, "d"), &date), DATA_BIND_OK);
        check_int_eq(date.year, 2026);
        check_int_eq(data_bind_value_get_time(data_bind_value_get(copy, "t"), &time), DATA_BIND_OK);
        check_int_eq(time.millisecond, 123);
        check_int_eq(
            (int)data_bind_value_as_duration_milliseconds(data_bind_value_get(copy, "span")),
            5405250);
        check_int_eq(data_bind_value_get_decimal(data_bind_value_get(copy, "price"), &decimal),
                     DATA_BIND_OK);
        check_int_eq((int)decimal.mantissa, 12345);
        check_int_eq(decimal.scale, 2);
        check_str_eq(data_bind_value_as_bigint_string(data_bind_value_get(copy, "count")),
                     "123456789012345678901234567890");
        check_int_eq(data_bind_value_get_money(data_bind_value_get(copy, "total"), &money),
                     DATA_BIND_OK);
        check_str_eq(money.currency, "EUR");
        check_int_eq((int)money.amount.mantissa, 9999);
      }
      data_bind_value_free(copy);
      data_bind_free(codec);
    }
  }

  it("should bind validate select and stream YAML through public APIs") {
    const char *schema = "enum Side <uint8> { Buy = 1; Sell = 2; }\n"
                         "message Order { uint32 id; Side side; bool active; string symbol; }\n";
    const char *yaml = "orders:\n"
                       "  - id: 7\n"
                       "    side: Buy\n"
                       "    active: true\n"
                       "    symbol: ABCD\n"
                       "  - id: 8\n"
                       "    side: Sell\n"
                       "    active: false\n"
                       "    symbol: WXYZ\n";
    const char *yaml_sequence = "- {id: 1, side: Buy, active: true, symbol: ONE}\n"
                                "- {id: 2, side: Sell, active: false, symbol: TWO}\n";
    const char *yaml_root = "id: 9\nside: Buy\nactive: true\nsymbol: ROOT\n";
    DataBind *codec = NULL;
    DataBindValue *value = NULL;
    DataBindValue *all = NULL;
    DataBindValue *stream_value = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    DataBindStatus status = data_bind_create_from_text(schema, strlen(schema), &codec, &err);
    check_int_eq(status, DATA_BIND_OK);
    check_not_null(codec);

    if (codec) {
      data_bind_stream_t *stream;
      record_callback_state state = {0};
      status = data_bind_parse_yaml(codec, "Order", yaml_root, strlen(yaml_root), &value, &err);
      check_int_eq(status, DATA_BIND_OK);
      check_int_eq(data_bind_value_as_int(data_bind_value_get(value, "id")), 9);
      data_bind_value_free(value);
      value = NULL;

      status = data_bind_parse_yaml_all(codec, "Order", yaml_sequence, strlen(yaml_sequence), &all,
                                        &err);
      check_int_eq(status, DATA_BIND_OK);
      check_size_eq(data_bind_value_count(all), 2);
      data_bind_value_free(all);
      all = NULL;
      check_int_eq(
          data_bind_validate_yaml(codec, "Order", yaml_sequence, strlen(yaml_sequence), &err),
          DATA_BIND_OK);

      status =
          data_bind_parse_yaml_path(codec, "Order", yaml, strlen(yaml), "/orders[1]", &value, &err);
      check_int_eq(status, DATA_BIND_OK);
      check_not_null(value);
      check_int_eq(data_bind_value_as_int(data_bind_value_get(value, "id")), 8);
      check_int_eq(data_bind_value_as_int(data_bind_value_get(value, "side")), 2);

      status = data_bind_parse_yaml_path_all(codec, "Order", yaml, strlen(yaml), "/orders[*]", &all,
                                             &err);
      check_int_eq(status, DATA_BIND_OK);
      check_size_eq(data_bind_value_count(all), 2);
      check_int_eq(data_bind_value_as_int(data_bind_value_get(data_bind_value_at(all, 0), "id")),
                   7);
      check_int_eq(
          data_bind_validate_yaml_path(codec, "Order", yaml, strlen(yaml), "/orders[0]", &err),
          DATA_BIND_OK);

      stream = data_bind_stream_yaml_all_create(codec, "Order", &stream_value, &err);
      check_not_null(stream);
      if (stream) {
        check_int_eq(data_bind_stream_set_record_callback(stream, collect_record, &state),
                     DATA_BIND_OK);
        check_int_eq(data_bind_stream_feed(stream, yaml_sequence, 19), DATA_BIND_OK);
        check_int_eq(data_bind_stream_feed(stream, yaml_sequence + 19, strlen(yaml_sequence) - 19),
                     DATA_BIND_OK);
        check_int_eq(data_bind_stream_finish(stream), DATA_BIND_OK);
        check_size_eq(state.count, 2);
        check_int_eq(state.ids[0], 1);
        check_int_eq(state.ids[1], 2);
        check_size_eq(data_bind_value_count(stream_value), 2);
        data_bind_stream_destroy(stream);
      }

      data_bind_value_free(stream_value);
      data_bind_value_free(all);
      data_bind_value_free(value);
      value = NULL;
      status =
          data_bind_parse_yaml_path(codec, "Order", yaml, strlen(yaml), "/missing", &value, &err);
      check_int_eq(status, DATA_BIND_ERR_TYPE_MISMATCH);
      check_null(value);
      status =
          data_bind_parse_yaml_path(codec, "Order", yaml, strlen(yaml), "/orders[", &value, &err);
      check_int_eq(status, DATA_BIND_ERR_PARSE);
      check_null(value);
      status = data_bind_parse_yaml(codec, "Order", "? [a, b]\n: 1\n", 14, &value, &err);
      check_int_eq(status, DATA_BIND_ERR_TYPE_MISMATCH);
      check_null(value);
      data_bind_free(codec);
    }
  }

  it("should deliver schema-bound JSON CSV and XML records during feed") {
    const char *schema = "enum Side <uint8> { Buy = 1; Sell = 2; }\n"
                         "message Order { uint32 id; Side side; string symbol; }\n";
    DataBind *codec = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    DataBindStatus status = data_bind_create_from_text(schema, strlen(schema), &codec, &err);
    check_int_eq(status, DATA_BIND_OK);
    check_not_null(codec);

    if (codec) {
      data_bind_stream_t *stream = NULL;
      DataBindValue *value = NULL;
      record_callback_state state = {0};

      stream = data_bind_stream_json_path_all_create(codec, "Order", "$[*]", &value, &err);
      check_not_null(stream);
      if (stream) {
        const char *first = "[{\"id\":1,\"side\":\"Buy\",\"symbol\":\"A\"},";
        const char *second = "{\"id\":2,\"side\":\"Sell\",\"symbol\":\"B\"}]";
        check_int_eq(data_bind_stream_set_record_callback(stream, collect_record, &state),
                     DATA_BIND_OK);
        check_int_eq(data_bind_stream_feed(stream, first, strlen(first)), DATA_BIND_OK);
        check_size_eq(state.count, 1);
        check_int_eq(state.ids[0], 1);
        check_int_eq(data_bind_stream_feed(stream, second, strlen(second)), DATA_BIND_OK);
        check_size_eq(state.count, 2);
        check_int_eq(data_bind_stream_finish(stream), DATA_BIND_OK);
        check_size_eq(data_bind_value_count(value), 2);
        data_bind_value_free(value);
        value = NULL;
        data_bind_stream_destroy(stream);
      }

      memset(&state, 0, sizeof(state));
      stream = data_bind_stream_csv_path_create(codec, "Order", "side == \"Sell\"", &value, &err);
      check_not_null(stream);
      if (stream) {
        const char *first = "id,side,symbol\n10,Buy,A\n11,Se";
        const char *second = "ll,B\n12,Sell,C\n";
        check_int_eq(data_bind_stream_set_record_callback(stream, collect_record, &state),
                     DATA_BIND_OK);
        check_int_eq(data_bind_stream_feed(stream, first, strlen(first)), DATA_BIND_OK);
        check_size_eq(state.count, 0);
        check_int_eq(data_bind_stream_feed(stream, second, strlen(second)), DATA_BIND_OK);
        check_size_eq(state.count, 2);
        check_int_eq(state.ids[0], 11);
        check_int_eq(state.ids[1], 12);
        check_int_eq(data_bind_stream_finish(stream), DATA_BIND_OK);
        check_size_eq(data_bind_value_count(value), 2);
        data_bind_value_free(value);
        value = NULL;
        data_bind_stream_destroy(stream);
      }

      memset(&state, 0, sizeof(state));
      stream = data_bind_stream_xml_path_all_create(codec, "Order", "//order", &value, &err);
      check_not_null(stream);
      if (stream) {
        const char *first = "<orders><order><id>20</id><side>Buy</side><symbol>A</symbol></order>";
        const char *second =
            "<order><id>21</id><side>Sell</side><symbol>B</symbol></order></orders>";
        check_int_eq(data_bind_stream_set_record_callback(stream, collect_record, &state),
                     DATA_BIND_OK);
        check_int_eq(data_bind_stream_feed(stream, first, strlen(first)), DATA_BIND_OK);
        check_size_eq(state.count, 1);
        check_int_eq(state.ids[0], 20);
        check_int_eq(data_bind_stream_feed(stream, second, strlen(second)), DATA_BIND_OK);
        check_size_eq(state.count, 2);
        check_int_eq(data_bind_stream_finish(stream), DATA_BIND_OK);
        check_size_eq(data_bind_value_count(value), 2);
        data_bind_value_free(value);
        data_bind_stream_destroy(stream);
      }
      data_bind_free(codec);
    }
  }

  it("should preserve exact 64-bit integers in incremental JSON records") {
    const char *schema = "message Exact { i64 min_value; u64 max_value; }\n";
    const char *parts[] = {"[{\"min_value\":9007199254740",
                           "993,\"max_value\":18446744073709551615},",
                           "{\"min_value\":-9223372036854775808,"
                           "\"max_value\":9007199254740993}]"};
    DataBind *codec = NULL;
    DataBindValue *value = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    exact_record_state state = {0};
    data_bind_stream_t *stream;

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    check_not_null(codec);
    stream = data_bind_stream_json_all_create(codec, "Exact", &value, &err);
    check_not_null(stream);
    if (stream) {
      check_int_eq(data_bind_stream_set_record_callback(stream, collect_exact_record, &state),
                   DATA_BIND_OK);
      check_int_eq(data_bind_stream_feed(stream, parts[0], strlen(parts[0])), DATA_BIND_OK);
      check_size_eq(state.count, 0);
      check_int_eq(data_bind_stream_feed(stream, parts[1], strlen(parts[1])), DATA_BIND_OK);
      check_size_eq(state.count, 1);
      check_int_eq(data_bind_stream_feed(stream, parts[2], strlen(parts[2])), DATA_BIND_OK);
      check_size_eq(state.count, 2);
      check_int_eq(data_bind_stream_finish(stream), DATA_BIND_OK);
      check_size_eq(data_bind_value_count(value), 2);
      check(state.signed_values[0] == INT64_C(9007199254740993));
      check(state.unsigned_values[0] == UINT64_MAX);
      check(state.signed_values[1] == INT64_MIN);
      check(state.unsigned_values[1] == UINT64_C(9007199254740993));
      data_bind_stream_destroy(stream);
    }
    data_bind_value_free(value);
    data_bind_free(codec);
  }

  it("should enforce record callback stop error and setup state") {
    const char *schema = "message Order { uint32 id; string symbol; }\n";
    const char *json = "[{\"id\":1,\"symbol\":\"A\"},{\"id\":2,\"symbol\":\"B\"}]";
    DataBind *codec = NULL;
    DataBindValue *value = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    DataBindStatus status = data_bind_create_from_text(schema, strlen(schema), &codec, &err);
    check_int_eq(status, DATA_BIND_OK);

    if (codec) {
      data_bind_stream_t *stream = data_bind_stream_json_all_create(codec, "Order", &value, &err);
      record_callback_state state = {0};
      state.stop_after = 1;
      check_not_null(stream);
      if (stream) {
        check_int_eq(data_bind_stream_set_record_callback(stream, collect_record, &state),
                     DATA_BIND_OK);
        check_int_eq(data_bind_stream_feed(stream, json, strlen(json)), DATA_BIND_OK);
        check_size_eq(state.count, 1);
        check_int_eq(data_bind_stream_set_record_callback(stream, collect_record, &state),
                     DATA_BIND_ERR_INVALID_ARG);
        check_int_eq(data_bind_stream_finish(stream), DATA_BIND_OK);
        check_size_eq(data_bind_value_count(value), 2);
        data_bind_value_free(value);
        value = NULL;
        data_bind_stream_destroy(stream);
      }

      stream = data_bind_stream_json_all_create(codec, "Order", &value, &err);
      memset(&state, 0, sizeof(state));
      state.fail = 1;
      check_not_null(stream);
      if (stream) {
        check_int_eq(data_bind_stream_set_record_callback(stream, collect_record, &state),
                     DATA_BIND_OK);
        check_int_eq(data_bind_stream_feed(stream, json, strlen(json)), DATA_BIND_ERR_RUNTIME);
        check_int_eq(err.code, DATA_BIND_ERR_RUNTIME);
        check_str_contains(err.message, "Record callback failed");
        data_bind_value_free(value);
        data_bind_stream_destroy(stream);
      }
      data_bind_free(codec);
    }
  }

  it("should feed JSON and CSV files through existing streams") {
    const char *schema = "enum Side <uint8> { Buy = 1; Sell = 2; }\n"
                         "message Order { uint32 id; Side side; string symbol; }\n";
    const char *json_path = "test_public_api_stream_orders.json";
    const char *csv_path = "test_public_api_stream_orders.csv";
    DataBind *codec = NULL;
    DataBindValue *value = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    DataBindStatus status = data_bind_create_from_text(schema, strlen(schema), &codec, &err);
    check_int_eq(status, DATA_BIND_OK);
    check_not_null(codec);

    write_schema_file(json_path, "[{\"id\":10,\"side\":\"Buy\",\"symbol\":\"ABCD\"},"
                                 "{\"id\":11,\"side\":\"Sell\",\"symbol\":\"WXYZ\"}]");
    write_schema_file(csv_path, "id,side,symbol\n"
                                "10,Buy,ABCD\n"
                                "11,Sell,WXYZ\n");

    if (codec) {
      const DataBindValue *item = NULL;
      data_bind_stream_t *stream = NULL;

      stream = data_bind_stream_csv_path_create(codec, "Order", "side == \"Sell\"", &value, &err);
      check_not_null(stream);
      if (stream) {
        status = data_bind_stream_feed_file(stream, csv_path);
        check_int_eq(status, DATA_BIND_OK);
        status = data_bind_stream_finish(stream);
        check_int_eq(status, DATA_BIND_OK);
        check_not_null(value);
        check_size_eq(data_bind_value_count(value), 1);
        item = data_bind_value_at(value, 0);
        check_int_eq(data_bind_value_as_int(data_bind_value_get(item, "id")), 11);
        data_bind_value_free(value);
        value = NULL;
        data_bind_stream_destroy(stream);
      }

      stream = data_bind_stream_json_path_all_create(codec, "Order", "$[*]", &value, &err);
      check_not_null(stream);
      if (stream) {
        status = data_bind_stream_feed_file(stream, json_path);
        check_int_eq(status, DATA_BIND_OK);
        status = data_bind_stream_finish(stream);
        check_int_eq(status, DATA_BIND_OK);
        check_not_null(value);
        check_size_eq(data_bind_value_count(value), 2);
        item = data_bind_value_at(value, 1);
        check_int_eq(data_bind_value_as_int(data_bind_value_get(item, "id")), 11);
        data_bind_value_free(value);
        data_bind_stream_destroy(stream);
      }

      data_bind_free(codec);
    }

    remove(json_path);
    remove(csv_path);
  }
  it("should bind temporal scalars without the script parser module") {
    const char *schema = "message Event { datetime at; date d; time t; duration span; }\n";
    const char *json = "{\"at\":\"Sat, 04 Mar 2006 13:27:54 GMT\",\"d\":\"2026-06-28\","
                       "\"t\":\"09:30:05.123\",\"span\":\"1h30m5s250ms\"}";
    DataBind *codec = NULL;
    DataBindValue *event = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    DataBindStatus status = data_bind_create_from_text(schema, strlen(schema), &codec, &err);
    check_int_eq(status, DATA_BIND_OK);
    check_not_null(codec);

    if (codec) {
      turbo_datetime_t dt;
      DataBindDate date;
      DataBindTime time;
      int64_t span_ms = 0;
      char text[64];

      status = data_bind_parse_json(codec, "Event", json, strlen(json), &event, &err);
      check_int_eq(status, DATA_BIND_OK);
      check_not_null(event);
      check_int_eq(data_bind_value_get_datetime(data_bind_value_get(event, "at"), &dt),
                   DATA_BIND_OK);
      check_int_eq(dt.year, 2006);
      check_int_eq(data_bind_value_get_date(data_bind_value_get(event, "d"), &date), DATA_BIND_OK);
      check_int_eq(date.year, 2026);
      check_int_eq(data_bind_value_get_time(data_bind_value_get(event, "t"), &time), DATA_BIND_OK);
      check_int_eq(time.millisecond, 123);
      check_int_eq(
          data_bind_value_get_duration_milliseconds(data_bind_value_get(event, "span"), &span_ms),
          DATA_BIND_OK);
      check_int_eq((int)span_ms, 5405250);
      check_str_eq(data_bind_value_as_duration_string(data_bind_value_get(event, "span"), text,
                                                      sizeof(text)),
                   "1:30:05.250");
      data_bind_value_free(event);
      data_bind_free(codec);
    }
  }

  it("should bind decimal scalars through JSON CSV and XML") {
    const char *schema = "message Quote { decimal price; }\n";
    const char *json = "{\"price\":\"123.4500\"}";
    const char *csv = "price\n-0.1250\n";
    const char *xml = "<quote><price>42.00</price></quote>";
    DataBind *codec = NULL;
    DataBindValue *quote = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    DataBindDecimal decimal;
    char text[64];
    DataBindStatus status = data_bind_create_from_text(schema, strlen(schema), &codec, &err);
    check_int_eq(status, DATA_BIND_OK);
    check_not_null(codec);

    if (codec) {
      status = data_bind_parse_json(codec, "Quote", json, strlen(json), &quote, &err);
      check_int_eq(status, DATA_BIND_OK);
      check_not_null(quote);
      check(data_bind_value_kind(data_bind_value_get(quote, "price")) == DATA_BIND_VALUE_DECIMAL);
      check_int_eq(data_bind_value_get_decimal(data_bind_value_get(quote, "price"), &decimal),
                   DATA_BIND_OK);
      check_int_eq((int)decimal.mantissa, 12345);
      check_int_eq(decimal.scale, 2);
      check_str_eq(data_bind_value_as_decimal_string(data_bind_value_get(quote, "price"), text,
                                                     sizeof(text)),
                   "123.45");
      data_bind_value_free(quote);
      quote = NULL;

      status = data_bind_parse_csv(codec, "Quote", csv, strlen(csv), 0, &quote, &err);
      check_int_eq(status, DATA_BIND_OK);
      check_int_eq(data_bind_value_get_decimal(data_bind_value_get(quote, "price"), &decimal),
                   DATA_BIND_OK);
      check_int_eq((int)decimal.mantissa, -125);
      check_int_eq(decimal.scale, 3);
      data_bind_value_free(quote);
      quote = NULL;

      status = data_bind_parse_xml(codec, "Quote", xml, strlen(xml), &quote, &err);
      check_int_eq(status, DATA_BIND_OK);
      check_str_eq(data_bind_value_as_decimal_string(data_bind_value_get(quote, "price"), text,
                                                     sizeof(text)),
                   "42");
      data_bind_value_free(quote);
      quote = NULL;

      {
        const char *bad_json = "{\"price\":\"bad\"}";
        status = data_bind_validate_json(codec, "Quote", bad_json, strlen(bad_json), &err);
      }
      check_int_eq(status, DATA_BIND_ERR_TYPE_MISMATCH);
      data_bind_free(codec);
    }
  }

  it("should bind bigint and money scalars through the public API") {
    const char *schema = "message Invoice { bigint id; money total; }\n";
    const char *json = "{\"id\":\"000123456789012345678901234567890\","
                       "\"total\":{\"amount\":\"123.4500\",\"currency\":\"USD\"}}";
    DataBind *codec = NULL;
    DataBindValue *invoice = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    const char *bigint = NULL;
    size_t bigint_len = 0;
    DataBindMoney money;
    char text[64];
    DataBindStatus status = data_bind_create_from_text(schema, strlen(schema), &codec, &err);
    check_int_eq(status, DATA_BIND_OK);
    check_not_null(codec);

    if (codec) {
      status = data_bind_parse_json(codec, "Invoice", json, strlen(json), &invoice, &err);
      check_int_eq(status, DATA_BIND_OK);
      check_not_null(invoice);
      check(data_bind_value_kind(data_bind_value_get(invoice, "id")) == DATA_BIND_VALUE_BIGINT);
      check_int_eq(
          data_bind_value_get_bigint(data_bind_value_get(invoice, "id"), &bigint, &bigint_len),
          DATA_BIND_OK);
      check_str_eq(bigint, "123456789012345678901234567890");
      check_size_eq(bigint_len, strlen("123456789012345678901234567890"));
      check(data_bind_value_kind(data_bind_value_get(invoice, "total")) == DATA_BIND_VALUE_MONEY);
      check_int_eq(data_bind_value_get_money(data_bind_value_get(invoice, "total"), &money),
                   DATA_BIND_OK);
      check_str_eq(money.currency, "USD");
      check_int_eq((int)money.amount.mantissa, 12345);
      check_int_eq(money.amount.scale, 2);
      check_str_eq(data_bind_value_as_money_string(data_bind_value_get(invoice, "total"), text,
                                                   sizeof(text)),
                   "USD 123.45");

      data_bind_value_free(invoice);
      data_bind_free(codec);
    }
  }

  it("should bind schema field names and aliases while keeping canonical object names") {
    const char *schema =
        "message Order { [name(\"order-id\"), alias(\"legacy-id\"), alias(\"old-id\")] uint32 id; "
        "[name(displayName)] string name; }\n";
    const char *json = "{\"order-id\":7,\"displayName\":\"JSON\"}";
    const char *legacy_json = "{\"old-id\":8,\"displayName\":\"Legacy\"}";
    const char *yaml = "order-id: 9\ndisplayName: YAML\n";
    DataBind *codec = NULL;
    DataBindObject *object = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    check_not_null(codec);
    if (codec != NULL) {
      check_int_eq(data_bind_object_from_json(codec, "Order", json, strlen(json), &object, &err),
                   DATA_BIND_OK);
      check_int_eq(data_bind_value_as_int(
                       data_bind_value_get(data_bind_object_value(object), "id")),
                   7);
      check_str_eq(data_bind_value_as_string(
                       data_bind_value_get(data_bind_object_value(object), "name")),
                   "JSON");
      data_bind_object_free(object);
      object = NULL;

      check_int_eq(data_bind_object_from_json(codec, "Order", legacy_json, strlen(legacy_json),
                                              &object, &err),
                   DATA_BIND_OK);
      check_int_eq(data_bind_value_as_int(
                       data_bind_value_get(data_bind_object_value(object), "id")),
                   8);
      data_bind_object_free(object);
      object = NULL;

      check_int_eq(data_bind_object_from_yaml(codec, "Order", yaml, strlen(yaml), &object, &err),
                   DATA_BIND_OK);
      check_int_eq(data_bind_value_as_int(
                       data_bind_value_get(data_bind_object_value(object), "id")),
                   9);
      check_str_eq(data_bind_value_as_string(
                       data_bind_value_get(data_bind_object_value(object), "name")),
                   "YAML");
      data_bind_object_free(object);
      data_bind_free(codec);
    }
  }

  it("should bind mapped XML and CSV field names") {
    const char *schema =
        "message Order { [name(\"order-id\"), alias(\"legacy-id\"), alias(\"old-id\")] uint32 id; "
        "[name(displayName)] string name; }\n";
    const char *xml =
        "<order><order-id>10</order-id><displayName>XML</displayName></order>";
    const char *csv = "old-id,displayName\n11,CSV\n";
    DataBind *codec = NULL;
    DataBindObject *object = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    check_not_null(codec);
    if (codec != NULL) {
      check_int_eq(data_bind_object_from_xml(codec, "Order", xml, strlen(xml), &object, &err),
                   DATA_BIND_OK);
      check_int_eq(data_bind_value_as_int(
                       data_bind_value_get(data_bind_object_value(object), "id")),
                   10);
      check_str_eq(data_bind_value_as_string(
                       data_bind_value_get(data_bind_object_value(object), "name")),
                   "XML");
      data_bind_object_free(object);
      object = NULL;

      check_int_eq(data_bind_object_from_csv(codec, "Order", csv, strlen(csv), 0, &object, &err),
                   DATA_BIND_OK);
      check_int_eq(data_bind_value_as_int(
                       data_bind_value_get(data_bind_object_value(object), "id")),
                   11);
      check_str_eq(data_bind_value_as_string(
                       data_bind_value_get(data_bind_object_value(object), "name")),
                   "CSV");
      data_bind_object_free(object);
      data_bind_free(codec);
    }
  }

  it("should serialize schema names by default without mutating the object") {
    const char *schema =
        "composite Address { [name(postalCode)] uint32 zip; }"
        "message Order { [name(orderId)] uint32 id; [name(shipping)] Address address; }\n";
    const char *json = "{\"id\":12,\"address\":{\"zip\":90210}}";
    DataBind *codec = NULL;
    DataBindObject *object = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    char *json_output = NULL;
    char *yaml_output = NULL;
    char *xml_output = NULL;
    char *csv_output = NULL;
    size_t output_len = 0;

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    check_not_null(codec);
    if (codec != NULL) {
      check_int_eq(data_bind_object_from_json(codec, "Order", json, strlen(json), &object, &err),
                   DATA_BIND_OK);
      check_int_eq(data_bind_object_serialize_json(codec, object, &json_output, &output_len, &err),
                   DATA_BIND_OK);
      check_str_contains(json_output, "\"orderId\":12");
      check_str_contains(json_output, "\"shipping\"");
      check_str_contains(json_output, "\"postalCode\":90210");
      check_null(strstr(json_output, "\"id\""));
      check_int_eq(data_bind_object_serialize_yaml(codec, object, &yaml_output, &output_len, &err),
                   DATA_BIND_OK);
      check_str_contains(yaml_output, "orderId");
      check_str_contains(yaml_output, "postalCode");
      check_int_eq(data_bind_object_serialize_xml(codec, object, &xml_output, &output_len, &err),
                   DATA_BIND_OK);
      check_str_contains(xml_output, "<orderId>12</orderId>");
      check_str_contains(xml_output, "<postalCode>90210</postalCode>");
      check_int_eq(data_bind_object_serialize_csv(codec, object, &csv_output, &output_len, &err),
                   DATA_BIND_OK);
      check_str_contains(csv_output, "orderId");
      check_str_contains(csv_output, "shipping.postalCode");

      check_not_null(data_bind_value_get(data_bind_object_value(object), "id"));
      check_null(data_bind_value_get(data_bind_object_value(object), "orderId"));
      data_bind_serialized_free(json_output);
      data_bind_serialized_free(yaml_output);
      data_bind_serialized_free(xml_output);
      data_bind_serialized_free(csv_output);
      data_bind_object_free(object);
      data_bind_free(codec);
    }
  }

  it("should bind mapped and aliased union variant names in every text format") {
    const char *schema =
        "union Choice { [name(buyOrder), alias(oldBuy), alias(legacyBuy)] uint32 buy; "
        "[name(sellOrder)] string sell; }"
        "message Envelope { [name(payload)] Choice choice; }\n";
    const char *json = "{\"payload\":{\"legacyBuy\":21}}";
    const char *yaml = "payload:\n  legacyBuy: 22\n";
    const char *xml =
        "<envelope><payload><legacyBuy>23</legacyBuy></payload></envelope>";
    const char *csv = "payload.legacyBuy\n24\n";
    DataBind *codec = NULL;
    DataBindObject *object = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    const DataBindValue *choice;

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    check_not_null(codec);
    if (codec != NULL) {
      check_int_eq(
          data_bind_object_from_json(codec, "Envelope", json, strlen(json), &object, &err),
          DATA_BIND_OK);
      choice = data_bind_value_get(data_bind_object_value(object), "choice");
      check_int_eq(data_bind_value_as_int(data_bind_value_get(choice, "buy")), 21);
      data_bind_object_free(object);
      object = NULL;

      check_int_eq(
          data_bind_object_from_yaml(codec, "Envelope", yaml, strlen(yaml), &object, &err),
          DATA_BIND_OK);
      choice = data_bind_value_get(data_bind_object_value(object), "choice");
      check_int_eq(data_bind_value_as_int(data_bind_value_get(choice, "buy")), 22);
      data_bind_object_free(object);
      object = NULL;

      check_int_eq(
          data_bind_object_from_xml(codec, "Envelope", xml, strlen(xml), &object, &err),
          DATA_BIND_OK);
      choice = data_bind_value_get(data_bind_object_value(object), "choice");
      check_int_eq(data_bind_value_as_int(data_bind_value_get(choice, "buy")), 23);
      data_bind_object_free(object);
      object = NULL;

      check_int_eq(
          data_bind_object_from_csv(codec, "Envelope", csv, strlen(csv), 0, &object, &err),
          DATA_BIND_OK);
      choice = data_bind_value_get(data_bind_object_value(object), "choice");
      check_int_eq(data_bind_value_as_int(data_bind_value_get(choice, "buy")), 24);
      data_bind_object_free(object);
      data_bind_free(codec);
    }
  }

  it("should recursively serialize schema union variant names") {
    const char *schema =
        "union Choice { [name(buyOrder)] uint32 buy; [name(sellOrder)] string sell; }"
        "message Envelope { [name(payload)] Choice choice; }\n";
    const char *json = "{\"choice\":{\"buy\":25}}";
    DataBind *codec = NULL;
    DataBindObject *object = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    char *mapped_json = NULL;
    char *mapped_xml = NULL;
    char *mapped_csv = NULL;
    size_t output_len = 0;

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err), DATA_BIND_OK);
    check_not_null(codec);
    if (codec != NULL) {
      check_int_eq(
          data_bind_object_from_json(codec, "Envelope", json, strlen(json), &object, &err),
          DATA_BIND_OK);
      check_int_eq(data_bind_object_serialize_json(codec, object, &mapped_json, &output_len, &err),
                   DATA_BIND_OK);
      check_str_contains(mapped_json, "\"payload\":{\"buyOrder\":25}");
      check_int_eq(data_bind_object_serialize_xml(codec, object, &mapped_xml, &output_len, &err),
                   DATA_BIND_OK);
      check_str_contains(mapped_xml, "<buyOrder>25</buyOrder>");
      check_int_eq(data_bind_object_serialize_csv(codec, object, &mapped_csv, &output_len, &err),
                   DATA_BIND_OK);
      check_str_contains(mapped_csv, "payload.buyOrder");

      data_bind_serialized_free(mapped_json);
      data_bind_serialized_free(mapped_xml);
      data_bind_serialized_free(mapped_csv);
      data_bind_object_free(object);
      data_bind_free(codec);
    }
  }

  it("should reject ambiguous schema field bindings") {
    const char *schema =
        "union Collision { [name(shared)] uint32 first; "
        "[alias(other), alias(shared)] uint32 second; }\n";
    DataBind *codec = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err),
                 DATA_BIND_ERR_SCHEMA);
    check_null(codec);
    check_str_eq(err.path, "Collision.first");
    check_str_contains(err.message, "shared");
  }

  it("should reject non-portable generic binding names") {
    const char *schema = "message Invalid { [name(\"nested.value\")] uint32 value; }\n";
    DataBind *codec = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;

    check_int_eq(data_bind_create_from_text(schema, strlen(schema), &codec, &err),
                 DATA_BIND_ERR_SCHEMA);
    check_null(codec);
    check_str_eq(err.path, "Invalid.value");
    check_str_contains(err.message, "not portable");
  }

  it("should honor reflection struct size guards") {
    const char *schema = "schema Market [id(1)];\n"
                         "enum Side <uint8> { Buy = 1; }\n"
                         "message Order { uint32 id; Side side; }\n";
    DataBind *codec = NULL;
    DataBindError err = DATA_BIND_ERROR_INIT;
    DataBindStatus status = data_bind_create_from_text(schema, strlen(schema), &codec, &err);
    check_int_eq(status, DATA_BIND_OK);
    check_not_null(codec);

    if (codec) {
      struct {
        DataBindSchemaType type;
        unsigned char guard[8];
      } type_buf;
      struct {
        DataBindSchemaField field;
        unsigned char guard[8];
      } field_buf;
      struct {
        DataBindSchemaEnumItem item;
        unsigned char guard[8];
      } item_buf;
      struct {
        DataBindSchemaAttribute attr;
        unsigned char guard[8];
      } attr_buf;
      const unsigned char guard[8] = {0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5};

      memset(&type_buf, 0, sizeof(type_buf));
      memcpy(type_buf.guard, guard, sizeof(guard));
      type_buf.type.size = offsetof(DataBindSchemaType, fixed_block_size);
      check(data_bind_schema_find_type(codec, "Order", &type_buf.type) == 1);
      check_size_eq(type_buf.type.size, offsetof(DataBindSchemaType, fixed_block_size));
      check_str_eq(type_buf.type.name, "Order");
      check_mem_eq(type_buf.guard, guard, sizeof(guard));

      memset(&field_buf, 0, sizeof(field_buf));
      memcpy(field_buf.guard, guard, sizeof(guard));
      field_buf.field.size = offsetof(DataBindSchemaField, offset);
      check(data_bind_schema_field_at(codec, "Order", 1, &field_buf.field) == 1);
      check_size_eq(field_buf.field.size, offsetof(DataBindSchemaField, offset));
      check_str_eq(field_buf.field.name, "side");
      check_str_eq(field_buf.field.kind, "enum");
      check_mem_eq(field_buf.guard, guard, sizeof(guard));

      memset(&item_buf, 0, sizeof(item_buf));
      memcpy(item_buf.guard, guard, sizeof(guard));
      item_buf.item.size = offsetof(DataBindSchemaEnumItem, value);
      check(data_bind_schema_enum_item_at(codec, "Side", 0, &item_buf.item) == 1);
      check_size_eq(item_buf.item.size, offsetof(DataBindSchemaEnumItem, value));
      check_str_eq(item_buf.item.name, "Buy");
      check_mem_eq(item_buf.guard, guard, sizeof(guard));

      memset(&attr_buf, 0, sizeof(attr_buf));
      memcpy(attr_buf.guard, guard, sizeof(guard));
      attr_buf.attr.size = offsetof(DataBindSchemaAttribute, value);
      check(data_bind_schema_attribute_at(codec, 0, &attr_buf.attr) == 1);
      check_size_eq(attr_buf.attr.size, offsetof(DataBindSchemaAttribute, value));
      check_str_eq(attr_buf.attr.name, "id");
      check_mem_eq(attr_buf.guard, guard, sizeof(guard));

      type_buf.type.size = offsetof(DataBindSchemaType, fixed_block_size);
      memcpy(type_buf.guard, guard, sizeof(guard));
      check(data_bind_schema_find_type(codec, "Missing", &type_buf.type) == 0);
      check_size_eq(type_buf.type.size, offsetof(DataBindSchemaType, fixed_block_size));
      check_mem_eq(type_buf.guard, guard, sizeof(guard));

      data_bind_free(codec);
    }
  }
}
