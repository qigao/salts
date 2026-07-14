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
  FILE *f = fopen(path, "w");
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

spec("data_bind public API") {
  it("should expose version and ABI metadata") {
    check_int_eq(data_bind_library_version(), DATA_BIND_VERSION);
    check_int_eq(data_bind_abi_version(), DATA_BIND_ABI_VERSION);
    check_str_eq(data_bind_version_string(), "1.10.0");
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

  it("should reject invalid JSON and XML stream chunks before binding") {
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
      data_bind_free(codec);
      codec = NULL;
    }
    if (object) {
      check_str_eq(data_bind_object_type_name(object), "Payload");
      check_long_eq(
          data_bind_value_as_int64(data_bind_value_get(data_bind_object_value(object), "id")),
          INT64_C(9007199254740993));
      check_int_eq(data_bind_object_serialize_json(object, &serialized, &serialized_len, &err),
                   DATA_BIND_OK);
      check_not_null(serialized);
      check_str_contains(serialized, "\"id\":9007199254740993");
      check_str_contains(serialized, "\"emoji\":\"");
      check_str_contains(serialized, "\"raw\":\"Az\"");
      check(serialized_len == strlen(serialized));
      data_bind_serialized_free(serialized);

      check_int_eq(data_bind_object_serialize_yaml(object, &yaml, NULL, &err), DATA_BIND_OK);
      check_str_contains(yaml, "9007199254740993");
      data_bind_serialized_free(yaml);

      check_int_eq(data_bind_object_serialize_xml(object, &xml, NULL, &err), DATA_BIND_OK);
      check_str_contains(xml, "<id>9007199254740993</id>");
      data_bind_serialized_free(xml);

      check_int_eq(data_bind_object_write_json(object, collect_serialized, &output, &err),
                   DATA_BIND_OK);
      check_str_contains(output.data, "9007199254740993");
      data_bind_object_free(object);
    }
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
      check_int_eq(data_bind_object_serialize_yaml(object, &serialized, NULL, &err), DATA_BIND_OK);
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
      check_int_eq(data_bind_object_serialize_xml(object, &serialized, NULL, &err), DATA_BIND_OK);
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
    uint8_t uuid[DATA_BIND_UUID_SIZE];
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
        check(data_bind_value_as_uuid(data_bind_value_get(copy, "id"), uuid));
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
