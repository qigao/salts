#include "data_bind.h"
#include "tinytest.h"

#include <string.h>

spec("data_bind record API") {
  it("reads native text formats and MIR binary records") {
    static const char schema[] = "message Order { uint32 id; string symbol; }";
    static const char json[] = "{\"id\":7,\"symbol\":\"JSON\"}";
    static const char yaml[] = "id: 8\nsymbol: YAML\n";
    static const char xml[] = "<order><id>9</id><symbol>XML</symbol></order>";
    static const char csv[] = "id,symbol\n10,CSV\n";
    DataBind *codec = NULL;
    DataBindRecord *record = NULL;
    DataBindRecord *decoded = NULL;
    DataBindRecordField id_field = DATA_BIND_RECORD_FIELD_INIT;
    DataBindStringView symbol = DATA_BIND_STRING_VIEW_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    uint8_t *wire = NULL;
    size_t wire_len = 0;
    uint32_t id = 0;
    char *text = NULL;
    size_t text_len = 0;

    check_int_eq(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;

    check_int_eq(
        data_bind_record_from_json(codec, "Order", json, sizeof(json) - 1, &record, &error),
        DATA_BIND_OK);
    check_not_null(record);
    check_str_eq(data_bind_record_type_name(record), "Order");
    check_int_eq(data_bind_record_get_u32(record, "id", &id, &error), DATA_BIND_OK);
    check_uint_eq(id, 7);
    check_int_eq(data_bind_record_get_string(record, "symbol", &symbol, &error), DATA_BIND_OK);
    check_size_eq(symbol.length, 4);
    check_mem_eq(symbol.data, "JSON", 4);

    check_int_eq(data_bind_record_find_field(record, "id", &id_field, &error), DATA_BIND_OK);
    id = 0;
    check_int_eq(data_bind_record_field_get_u32(&id_field, &id, &error), DATA_BIND_OK);
    check_uint_eq(id, 7);

    check_int_eq(data_bind_record_serialize_json(record, &text, &text_len, &error), DATA_BIND_OK);
    check_str_contains(text, "\"id\":7");
    data_bind_serialized_free(text);
    text = NULL;
    check_int_eq(data_bind_record_serialize_yaml(record, &text, &text_len, &error), DATA_BIND_OK);
    check_str_contains(text, "\"id\": 7");
    data_bind_serialized_free(text);
    text = NULL;
    check_int_eq(data_bind_record_serialize_xml(record, &text, &text_len, &error), DATA_BIND_OK);
    check_str_contains(text, "<id>7</id>");
    data_bind_serialized_free(text);
    text = NULL;
    check_int_eq(data_bind_record_serialize_csv(record, &text, &text_len, &error), DATA_BIND_OK);
    check_str_contains(text, "id,symbol");
    check_str_contains(text, "7,JSON");
    data_bind_serialized_free(text);
    text = NULL;

    check_int_eq(data_bind_record_serialize_bin(codec, record, &wire, &wire_len, &error),
                 DATA_BIND_OK);
    check_not_null(wire);
    check_size_gt(wire_len, 0);
    if (wire != NULL) {
      check_int_eq(data_bind_record_from_bin(codec, "Order", wire, wire_len, &decoded, &error),
                   DATA_BIND_OK);
      check_int_eq(data_bind_record_get_u32(decoded, "id", &id, &error), DATA_BIND_OK);
      check_uint_eq(id, 7);
    }
    data_bind_record_free(decoded);
    decoded = NULL;
    data_bind_binary_free(wire);
    wire = NULL;
    data_bind_record_free(record);
    record = NULL;

    check_int_eq(
        data_bind_record_from_yaml(codec, "Order", yaml, sizeof(yaml) - 1, &record, &error),
        DATA_BIND_OK);
    check_int_eq(data_bind_record_get_u32(record, "id", &id, &error), DATA_BIND_OK);
    check_uint_eq(id, 8);
    data_bind_record_free(record);
    record = NULL;

    check_int_eq(data_bind_record_from_xml(codec, "Order", xml, sizeof(xml) - 1, &record, &error),
                 DATA_BIND_OK);
    check_int_eq(data_bind_record_get_u32(record, "id", &id, &error), DATA_BIND_OK);
    check_uint_eq(id, 9);
    data_bind_record_free(record);
    record = NULL;

    check_int_eq(
        data_bind_record_from_csv(codec, "Order", csv, sizeof(csv) - 1, 0, &record, &error),
        DATA_BIND_OK);
    check_int_eq(data_bind_record_get_u32(record, "id", &id, &error), DATA_BIND_OK);
    check_uint_eq(id, 10);
    data_bind_record_free(record);
    data_bind_free(codec);
  }

  it("traverses nested objects lists maps and borrowed data") {
    static const char schema[] =
        "composite Header { uint32 seq; }"
        "group Fill { int32 price; uint32 qty; }"
        "message Order { Header header; group<Fill> fills; map<string,uint32> attrs; "
        "bytes payload; }";
    static const char json[] = "{\"header\":{\"seq\":3},\"fills\":[{\"price\":100,\"qty\":2}],"
                               "\"attrs\":{\"desk\":7},\"payload\":\"raw\"}";
    DataBind *codec = NULL;
    DataBindRecord *record = NULL;
    DataBindRecordView header = DATA_BIND_RECORD_VIEW_INIT;
    DataBindRecordView fill = DATA_BIND_RECORD_VIEW_INIT;
    DataBindListView fills = DATA_BIND_LIST_VIEW_INIT;
    DataBindRecordMapView attrs = DATA_BIND_RECORD_MAP_VIEW_INIT;
    DataBindBytesView payload = DATA_BIND_BYTES_VIEW_INIT;
    DataBindStringView key = DATA_BIND_STRING_VIEW_INIT;
    DataBindRecordField field = DATA_BIND_RECORD_FIELD_INIT;
    DataBindRecordField item = DATA_BIND_RECORD_FIELD_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    uint32_t value = 0;
    int32_t price = 0;

    check_int_eq(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;
    check_int_eq(
        data_bind_record_from_json(codec, "Order", json, sizeof(json) - 1, &record, &error),
        DATA_BIND_OK);
    check_not_null(record);
    if (record == NULL) {
      data_bind_free(codec);
      return;
    }

    check_int_eq(data_bind_record_get_object(record, "header", &header, &error), DATA_BIND_OK);
    check_int_eq(data_bind_record_view_find_field(&header, "seq", &field, &error), DATA_BIND_OK);
    check_int_eq(data_bind_record_field_get_u32(&field, &value, &error), DATA_BIND_OK);
    check_uint_eq(value, 3);

    check_int_eq(data_bind_record_get_list(record, "fills", &fills, &error), DATA_BIND_OK);
    check_size_eq(data_bind_list_view_count(&fills), 1);
    check_int_eq(data_bind_list_view_at(&fills, 0, &item, &error), DATA_BIND_OK);
    check_int_eq(data_bind_record_field_get_object(&item, &fill, &error), DATA_BIND_OK);
    field = (DataBindRecordField)DATA_BIND_RECORD_FIELD_INIT;
    check_int_eq(data_bind_record_view_find_field(&fill, "price", &field, &error), DATA_BIND_OK);
    check_int_eq(data_bind_record_field_get_i32(&field, &price, &error), DATA_BIND_OK);
    check_int_eq(price, 100);

    check_int_eq(data_bind_record_get_map(record, "attrs", &attrs, &error), DATA_BIND_OK);
    check_size_eq(data_bind_record_map_view_count(&attrs), 1);
    item = (DataBindRecordField)DATA_BIND_RECORD_FIELD_INIT;
    check_int_eq(data_bind_record_map_view_at(&attrs, 0, &key, &item, &error), DATA_BIND_OK);
    check_size_eq(key.length, 4);
    check_mem_eq(key.data, "desk", 4);
    check_int_eq(data_bind_record_field_get_u32(&item, &value, &error), DATA_BIND_OK);
    check_uint_eq(value, 7);

    check_int_eq(data_bind_record_get_bytes(record, "payload", &payload, &error), DATA_BIND_OK);
    check_size_eq(payload.length, 3);
    check_mem_eq(payload.data, "raw", 3);

    data_bind_record_free(record);
    data_bind_free(codec);
  }

  it("uses slot-backed binary records while preserving the legacy parser") {
    static const char schema[] =
        "composite Header { uint32 seq; }"
        "group Fill { int32 price; uint32 qty; }"
        "message Order { Header header; group<Fill> fills; string symbol; }";
    static const char json[] =
        "{\"header\":{\"seq\":3},\"fills\":[{\"price\":100,\"qty\":2},"
        "{\"price\":101,\"qty\":4}],\"symbol\":\"XYZ\"}";
    DataBind *codec = NULL;
    DataBindRecord *source = NULL;
    DataBindRecord *decoded = NULL;
    DataBindRecord *failed = NULL;
    DataBindValue *legacy = NULL;
    DataBindRecordView header = DATA_BIND_RECORD_VIEW_INIT;
    DataBindRecordView fill = DATA_BIND_RECORD_VIEW_INIT;
    DataBindListView fills = DATA_BIND_LIST_VIEW_INIT;
    DataBindRecordField field = DATA_BIND_RECORD_FIELD_INIT;
    DataBindRecordField item = DATA_BIND_RECORD_FIELD_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    uint8_t *wire = NULL;
    size_t wire_len = 0;
    char *roundtrip_json = NULL;
    size_t roundtrip_json_len = 0;
    uint32_t u32 = 0;
    int32_t i32 = 0;

    check_int_eq(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    check_int_eq(
        data_bind_record_from_json(codec, "Order", json, sizeof(json) - 1, &source, &error),
        DATA_BIND_OK);
    check_int_eq(data_bind_record_serialize_bin(codec, source, &wire, &wire_len, &error),
                 DATA_BIND_OK);
    check_size_gt(wire_len, 1);

    check_int_eq(data_bind_record_from_bin(codec, "Order", wire, wire_len, &decoded, &error),
                 DATA_BIND_OK);
    check_int_eq(data_bind_record_get_object(decoded, "header", &header, &error), DATA_BIND_OK);
    check_int_eq(data_bind_record_view_find_field(&header, "seq", &field, &error), DATA_BIND_OK);
    check_int_eq(data_bind_record_field_get_u32(&field, &u32, &error), DATA_BIND_OK);
    check_uint_eq(u32, 3);
    check_int_eq(data_bind_record_get_list(decoded, "fills", &fills, &error), DATA_BIND_OK);
    check_size_eq(data_bind_list_view_count(&fills), 2);
    check_int_eq(data_bind_list_view_at(&fills, 1, &item, &error), DATA_BIND_OK);
    check_int_eq(data_bind_record_field_get_object(&item, &fill, &error), DATA_BIND_OK);
    field = (DataBindRecordField)DATA_BIND_RECORD_FIELD_INIT;
    check_int_eq(data_bind_record_view_find_field(&fill, "price", &field, &error), DATA_BIND_OK);
    check_int_eq(data_bind_record_field_get_i32(&field, &i32, &error), DATA_BIND_OK);
    check_int_eq(i32, 101);
    check_int_eq(data_bind_record_serialize_json(decoded, &roundtrip_json, &roundtrip_json_len,
                                                 &error),
                 DATA_BIND_OK);
    check_str_contains(roundtrip_json, "\"header\":{\"seq\":3}");
    check_str_contains(roundtrip_json, "\"fills\":[{\"price\":100,\"qty\":2}");

    check_int_eq(data_bind_parse(codec, "Order", wire, wire_len, &legacy, &error), DATA_BIND_OK);
    check_not_null(data_bind_value_get(legacy, "header.seq"));
    check_null(data_bind_value_get(legacy, "header"));

    check_int_eq(data_bind_record_from_bin(codec, "Order", wire, wire_len - 1, &failed, &error),
                 DATA_BIND_ERR_PARSE);
    check_null(failed);

    data_bind_free(codec);
    codec = NULL;
    header = (DataBindRecordView)DATA_BIND_RECORD_VIEW_INIT;
    field = (DataBindRecordField)DATA_BIND_RECORD_FIELD_INIT;
    check_int_eq(data_bind_record_get_object(decoded, "header", &header, &error), DATA_BIND_OK);
    check_int_eq(data_bind_record_view_find_field(&header, "seq", &field, &error), DATA_BIND_OK);
    check_int_eq(data_bind_record_field_get_u32(&field, &u32, &error), DATA_BIND_OK);
    check_uint_eq(u32, 3);

    data_bind_value_free(legacy);
    data_bind_serialized_free(roundtrip_json);
    data_bind_record_free(decoded);
    data_bind_binary_free(wire);
    data_bind_record_free(source);
  }

  it("fails strictly for missing fields type mismatches and invalid handles") {
    static const char schema[] = "message Order { uint32 id; string symbol; }";
    static const char json[] = "{\"id\":7,\"symbol\":\"ABC\"}";
    DataBind *codec = NULL;
    DataBindRecord *record = NULL;
    DataBindRecordField field = DATA_BIND_RECORD_FIELD_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    uint32_t id = 99;

    check_int_eq(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    check_int_eq(
        data_bind_record_from_json(codec, "Order", json, sizeof(json) - 1, &record, &error),
        DATA_BIND_OK);

    check_int_eq(data_bind_record_get_u32(record, "missing", &id, &error),
                 DATA_BIND_ERR_TYPE_MISMATCH);
    check_uint_eq(id, 99);
    check_str_eq(error.path, "missing");

    check_int_eq(data_bind_record_get_u32(record, "symbol", &id, &error),
                 DATA_BIND_ERR_TYPE_MISMATCH);
    check_uint_eq(id, 99);
    check_str_eq(error.path, "symbol");

    field.size = 0;
    check_int_eq(data_bind_record_find_field(record, "id", &field, &error),
                 DATA_BIND_ERR_INVALID_ARG);

    data_bind_record_free(record);
    data_bind_free(codec);
  }
}
