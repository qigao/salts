#include "tinytest.h"
#include "turbo_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct yaml_sax_counts {
  int documents;
  int mappings;
  int sequences;
  int keys;
  int scalars;
  char values[8][32];
} yaml_sax_counts;

typedef struct csv_write_capture {
  char records[4][128];
  char bytes[256];
  size_t bytes_len;
  size_t count;
} csv_write_capture;

typedef struct csv_sax_capture {
  size_t rows_started;
  size_t rows_ended;
  size_t fields;
  char values[8][32];
} csv_sax_capture;

typedef struct json_raw_number_capture {
  size_t count;
  char values[3][32];
} json_raw_number_capture;

static int capture_json_raw_number(void *user, const char *value, size_t value_len) {
  json_raw_number_capture *capture = (json_raw_number_capture *)user;
  if (capture == NULL || capture->count >= 3 || value_len >= sizeof(capture->values[0])) return -1;
  memcpy(capture->values[capture->count], value, value_len);
  capture->values[capture->count][value_len] = '\0';
  capture->count++;
  return 0;
}

static int capture_csv_write(const void *data, size_t len, void *user) {
  csv_write_capture *capture = (csv_write_capture *)user;
  size_t append_len = len < sizeof(capture->bytes) - capture->bytes_len - 1
                          ? len
                          : sizeof(capture->bytes) - capture->bytes_len - 1;
  memcpy(capture->bytes + capture->bytes_len, data, append_len);
  capture->bytes_len += append_len;
  capture->bytes[capture->bytes_len] = '\0';
  if (capture->count < 4) {
    size_t copy_len = len < sizeof(capture->records[0]) - 1 ? len : sizeof(capture->records[0]) - 1;
    memcpy(capture->records[capture->count], data, copy_len);
    capture->records[capture->count][copy_len] = '\0';
  }
  capture->count++;
  return 0;
}

static int capture_csv_row_start(void *user, size_t row) {
  csv_sax_capture *capture = (csv_sax_capture *)user;
  (void)row;
  capture->rows_started++;
  return 0;
}

static int capture_csv_field(void *user, size_t row, size_t column, const char *value,
                             size_t value_len) {
  csv_sax_capture *capture = (csv_sax_capture *)user;
  size_t copy_len;
  (void)row;
  (void)column;
  if (capture->fields < 8) {
    copy_len = value_len < sizeof(capture->values[0]) - 1 ? value_len
                                                          : sizeof(capture->values[0]) - 1;
    memcpy(capture->values[capture->fields], value, copy_len);
    capture->values[capture->fields][copy_len] = '\0';
  }
  capture->fields++;
  return 0;
}

static int capture_csv_row_end(void *user, size_t row, size_t field_count) {
  csv_sax_capture *capture = (csv_sax_capture *)user;
  (void)row;
  (void)field_count;
  capture->rows_ended++;
  return 0;
}

static int yaml_sax_document_start(void *ctx) {
  ((yaml_sax_counts *)ctx)->documents++;
  return 0;
}

static int yaml_sax_mapping_start(void *ctx, bool is_key) {
  (void)is_key;
  ((yaml_sax_counts *)ctx)->mappings++;
  return 0;
}

static int yaml_sax_sequence_start(void *ctx, bool is_key) {
  (void)is_key;
  ((yaml_sax_counts *)ctx)->sequences++;
  return 0;
}

static int yaml_sax_scalar(void *ctx, turbo_yaml_scalar_kind_t kind, const char *value,
                           size_t value_len, bool is_key) {
  yaml_sax_counts *counts = (yaml_sax_counts *)ctx;
  (void)kind;
  if (counts->scalars < 8) {
    size_t copy_len =
        value_len < sizeof(counts->values[0]) - 1 ? value_len : sizeof(counts->values[0]) - 1;
    memcpy(counts->values[counts->scalars], value, copy_len);
    counts->values[counts->scalars][copy_len] = '\0';
  }
  counts->scalars++;
  if (is_key) counts->keys++;
  return 0;
}

static const turbo_yaml_sax_handler_t yaml_sax_handler = {
    .on_document_start = yaml_sax_document_start,
    .on_scalar = yaml_sax_scalar,
    .on_sequence_start = yaml_sax_sequence_start,
    .on_mapping_start = yaml_sax_mapping_start,
};

spec("turbo_parser") {
  describe("JSON") {
    it("should parse JSON correctly") {
      const char *json_data = "{\"key\": \"value\", \"number\": 123}";
      turbo_json_doc_t *result = NULL;
      int rc = turbo_parse_json((const uint8_t *)json_data, strlen(json_data), &result);

      check_int_eq(rc, 0);
      check_not_null(result);
      check_int_eq(turbo_json_type(result), TURBO_JSON_OBJECT);

      turbo_free_json(&result);
      check_null(result);
    }

    it("should expose exact raw JSON numbers through complete and incremental APIs") {
      const turbo_json_sax_handler_raw_t handler = {.on_number = capture_json_raw_number};
      json_raw_number_capture capture = {0};
      turbo_json_sax_parser_t *parser;

      check_int_eq(turbo_parse_json_sax_raw((const uint8_t *)"18446744073709551615", 20,
                                            &handler, &capture),
                   0);
      check_str_eq(capture.values[0], "18446744073709551615");

      memset(&capture, 0, sizeof(capture));
      parser = turbo_json_sax_parser_create_raw(&handler, &capture);
      check_not_null(parser);
      check_int_eq(turbo_json_sax_parser_feed(parser, "[-922337203685",
                                              strlen("[-922337203685")),
                   0);
      check_int_eq(turbo_json_sax_parser_feed(parser, "4775808,9007199254740993]",
                                              strlen("4775808,9007199254740993]")),
                   0);
      check_int_eq(turbo_json_sax_parser_finish(parser), 0);
      check_size_eq(capture.count, 2);
      check_str_eq(capture.values[0], "-9223372036854775808");
      check_str_eq(capture.values[1], "9007199254740993");
      turbo_json_sax_parser_destroy(parser);
    }

    it("should build JSON correctly") {
      json_value_t *root = turbo_json_create_object();
      check_not_null(root);

      turbo_json_object_set_string(root, "name", "turbo");
      turbo_json_object_set_number(root, "version", 2.0);
      turbo_json_object_set_bool(root, "active", true);

      json_value_t *arr = turbo_json_create_array();
      turbo_json_array_add(arr, turbo_json_create_number(1));
      turbo_json_array_add(arr, turbo_json_create_number(2));
      turbo_json_object_add(root, "items", arr);

      size_t len = 0;
      char *serialized = turbo_json_serialize(root, &len);
      check_not_null(serialized);
      check(len > 0);

      check(strstr(serialized, "\"name\":\"turbo\"") != NULL);
      check(strstr(serialized, "\"version\":2") != NULL);
      check(strstr(serialized, "\"items\":[1,2]") != NULL);

      turbo_json_serialize_free(serialized);
      turbo_free_json(&root);
      check_null(root);
    }

    it("should deep clone nested JSON values") {
      json_value_t *root = turbo_json_create_object();
      json_value_t *items = turbo_json_create_array();
      json_value_t *meta = turbo_json_create_object();
      json_value_t *clone;
      char *original_json;
      char *clone_json;

      check_not_null(root);
      check_not_null(items);
      check_not_null(meta);

      turbo_json_array_add(items, turbo_json_create_number(1));
      turbo_json_object_set_string(meta, "status", "ok");
      turbo_json_object_add(root, "items", items);
      turbo_json_object_add(root, "meta", meta);

      clone = turbo_json_clone(root);
      check_not_null(clone);

      turbo_json_array_add(items, turbo_json_create_number(2));
      turbo_json_object_set_string(meta, "owner", "root");

      original_json = turbo_json_serialize(root, NULL);
      clone_json = turbo_json_serialize(clone, NULL);
      check_not_null(original_json);
      check_not_null(clone_json);
      check(strstr(original_json, "\"items\":[1,2]") != NULL);
      check(strstr(original_json, "\"owner\":\"root\"") != NULL);
      check(strstr(clone_json, "\"items\":[1]") != NULL);
      check(strstr(clone_json, "\"owner\":\"root\"") == NULL);
      check(strstr(clone_json, "\"status\":\"ok\"") != NULL);

      turbo_json_serialize_free(clone_json);
      turbo_json_serialize_free(original_json);
      turbo_free_json(&clone);
      turbo_free_json(&root);
    }

    it("should overwrite existing object keys when setting strings") {
      json_value_t *root = turbo_json_create_object();
      char *serialized;

      check_not_null(root);

      turbo_json_object_set_string(root, "stderr", "");
      turbo_json_object_set_string(root, "stderr", "line 1\\line 2\n\"oops\"");

      check_str_eq(turbo_json_get_string(root, "stderr"), "line 1\\line 2\n\"oops\"");

      serialized = turbo_json_serialize(root, NULL);
      check_not_null(serialized);
      check(strstr(serialized, "\"stderr\":\"line 1\\\\line 2\\n\\\"oops\\\"\"") != NULL);

      turbo_json_serialize_free(serialized);
      turbo_free_json(&root);
    }

    it("should query JSON with JSONPath") {
      const char *json_data = "{\"listeners\":[{\"port\":1883,\"transport\":\"tcp\"},"
                              "{\"port\":8883,\"transport\":\"tls\"}]}";
      json_value_t *root = NULL;
      int rc = turbo_parse_json((const uint8_t *)json_data, strlen(json_data), &root);
      check_int_eq(rc, 0);
      check_not_null(root);

      json_value_t *transport = turbo_json_path_get(root, "$.listeners[-1].transport");
      check_not_null(transport);
      check_str_eq(turbo_json_string(transport), "tls");

      turbo_json_path_result_t *ports = turbo_json_path_query(root, "$.listeners[*].port");
      check_not_null(ports);
      check_size_eq(turbo_json_path_result_size(ports), 2);
      check_int_eq((int)turbo_json_number(turbo_json_path_result_get(ports, 0)), 1883);
      check_int_eq((int)turbo_json_number(turbo_json_path_result_get(ports, 1)), 8883);

      turbo_json_path_result_free(ports);
      turbo_free_json(&root);
    }
  }

  describe("YAML") {
    it("should own input and expose YPATH plus JSON conversion") {
      char yaml_data[] = "users:\n"
                         "  - name: Alice\n"
                         "    active: true\n"
                         "  - name: Bob\n"
                         "    active: false\n";
      turbo_yaml_doc_t *doc = NULL;
      turbo_yaml_path_result_t *matches;
      turbo_yaml_node_t *name;
      json_value_t *json;
      char *text;

      check_int_eq(turbo_parse_yaml((const uint8_t *)yaml_data, strlen(yaml_data), &doc), 0);
      check_not_null(doc);
      memset(yaml_data, 'x', strlen(yaml_data));

      matches = turbo_yaml_path_query(doc, NULL, "/users[*]/name");
      check_not_null(matches);
      check_null(turbo_yaml_path_result_error(matches));
      check_size_eq(turbo_yaml_path_result_size(matches), 2);
      name = turbo_yaml_path_result_get(matches, 1);
      text = turbo_yaml_scalar_dup(doc, name);
      check_str_eq(text, "Bob");
      turbo_yaml_string_free(text);

      json = turbo_yaml_to_json(doc);
      check_not_null(json);
      check_str_eq(turbo_json_string(turbo_json_path_get(json, "$.users[0].name")), "Alice");
      turbo_free_json(&json);
      turbo_yaml_path_result_free(matches);
      turbo_free_yaml(&doc);
      check_null(doc);
    }

    it("should emit YAML documents and selected nodes") {
      const char *yaml_data = "name: turbo\nitems: [1, 2]\n";
      turbo_yaml_doc_t *doc = NULL;
      turbo_yaml_path_result_t *matches;
      char *emitted;
      size_t emitted_len = 0;

      check_int_eq(turbo_parse_yaml((const uint8_t *)yaml_data, strlen(yaml_data), &doc), 0);
      emitted = turbo_yaml_serialize(doc, &emitted_len);
      check_not_null(emitted);
      check(emitted_len > 0);
      check(strstr(emitted, "name: turbo") != NULL);
      turbo_yaml_serialize_free(emitted);

      matches = turbo_yaml_path_query(doc, NULL, "/items");
      check_not_null(matches);
      emitted = turbo_yaml_emit_node(doc, turbo_yaml_path_result_get(matches, 0), &emitted_len);
      check_not_null(emitted);
      check(strstr(emitted, "1") != NULL);
      turbo_yaml_string_free(emitted);
      turbo_yaml_path_result_free(matches);
      turbo_free_yaml(&doc);
    }

    it("should SAX parse YAML from arbitrary chunks") {
      const char *parts[] = {"name: tur", "bo\nitems:\n  - 1", "\n  - 2\n"};
      yaml_sax_counts counts = {0};
      turbo_yaml_sax_parser_t *parser = turbo_yaml_sax_parser_create(&yaml_sax_handler, &counts);
      check_not_null(parser);
      check_int_eq(turbo_yaml_sax_parser_feed(parser, parts[0], strlen(parts[0])), 0);
      check_int_eq(counts.documents, 1);
      check_int_eq(counts.mappings, 1);
      check_int_eq(counts.scalars, 1);
      check_str_eq(counts.values[0], "name");
      check_int_eq(turbo_yaml_sax_parser_feed(parser, parts[1], strlen(parts[1])), 0);
      check_int_eq(counts.sequences, 1);
      check_int_eq(counts.scalars, 3);
      check_str_eq(counts.values[1], "turbo");
      check_int_eq(turbo_yaml_sax_parser_feed(parser, parts[2], strlen(parts[2])), 0);
      check_int_eq(counts.scalars, 4);
      check_int_eq(turbo_yaml_sax_parser_finish(parser), 0);
      check_int_eq(counts.documents, 1);
      check_int_eq(counts.mappings, 1);
      check_int_eq(counts.sequences, 1);
      check_int_eq(counts.keys, 2);
      check_int_eq(counts.scalars, 5);
      check_str_eq(counts.values[4], "2");
      turbo_yaml_sax_parser_destroy(parser);
    }
  }

  describe("XML") {
    it("should build serialize and parse an XML DOM") {
      turbo_xml_doc_t *doc = turbo_xml_create_document("Item");
      turbo_xml_node_t *root = turbo_xml_root_element(doc);
      turbo_xml_node_t *name = turbo_xml_add_element(root, "name");
      turbo_xml_doc_t *parsed = NULL;
      size_t len = 0;
      char *xml;
      check_not_null(doc);
      check_not_null(root);
      check_not_null(name);
      check_int_eq(turbo_xml_set_text(name, "turbo & utils"), 0);
      xml = turbo_xml_serialize(doc, &len);
      check_not_null(xml);
      check(strstr(xml, "turbo &amp; utils") != NULL);
      check_int_eq(turbo_parse_xml((const uint8_t *)xml, len, &parsed), 0);
      check_not_null(parsed);
      turbo_xml_serialize_free(xml);
      turbo_free_xml(&parsed);
      turbo_free_xml(&doc);
    }
  }

  describe("CSV") {
    it("should separate byte-sink writes from logical-record writes") {
      const char *csv = "name,value\n\"line1\nline2\",ok\n";
      turbo_csv_options_t opts = {
          .has_header = true, .delimiter = ',', .quote = '"', .skip_empty_rows = true};
      turbo_csv_doc_t *doc = NULL;
      csv_write_capture capture = {0};
      size_t len = 0;
      char *text;

      check_int_eq(turbo_parse_csv_opts((const uint8_t *)csv, strlen(csv), &opts, &doc), 0);
      check_not_null(doc);
      text = turbo_csv_serialize(doc, &len);
      check_not_null(text);
      check_size_eq(len, strlen(text));
      check_str_eq(text, csv);
      check_int_eq(turbo_csv_write(doc, capture_csv_write, &capture), 0);
      check_str_eq(capture.bytes, csv);
      memset(&capture, 0, sizeof(capture));
      check_int_eq(turbo_csv_write_records(doc, capture_csv_write, &capture), 0);
      check_size_eq(capture.count, 2);
      check_str_eq(capture.records[0], "name,value\n");
      check_str_eq(capture.records[1], "\"line1\nline2\",ok\n");

      turbo_csv_serialize_free(text);
      turbo_free_csv(&doc);
    }

    it("should incrementally SAX parse fields across arbitrary chunks") {
      const char *parts[] = {"name,no", "te\r\n\"line1\n", "line2\",ok\r", "\n"};
      turbo_csv_sax_handler_t handler = {
          .on_row_start = capture_csv_row_start,
          .on_field = capture_csv_field,
          .on_row_end = capture_csv_row_end,
      };
      csv_sax_capture capture = {0};
      turbo_csv_sax_parser_t *parser = turbo_csv_sax_parser_create(&handler, &capture, NULL);
      check_not_null(parser);
      for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i)
        check_int_eq(turbo_csv_sax_parser_feed(parser, parts[i], strlen(parts[i])), 0);
      check_int_eq(turbo_csv_sax_parser_finish(parser), 0);
      check_size_eq(capture.rows_started, 2);
      check_size_eq(capture.rows_ended, 2);
      check_size_eq(capture.fields, 4);
      check_str_eq(capture.values[0], "name");
      check_str_eq(capture.values[1], "note");
      check_str_eq(capture.values[2], "line1\nline2");
      check_str_eq(capture.values[3], "ok");
      turbo_csv_sax_parser_destroy(parser);
    }

    it("should preserve escaped quotes split across SAX chunks") {
      const char *parts[] = {"a\r\n\"say \"", "\"hi\"", "\"\"\r\n"};
      turbo_csv_sax_handler_t handler = {
          .on_row_start = capture_csv_row_start,
          .on_field = capture_csv_field,
          .on_row_end = capture_csv_row_end,
      };
      csv_sax_capture capture = {0};
      turbo_csv_sax_parser_t *parser = turbo_csv_sax_parser_create(&handler, &capture, NULL);
      check_not_null(parser);
      for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i)
        check_int_eq(turbo_csv_sax_parser_feed(parser, parts[i], strlen(parts[i])), 0);
      check_int_eq(turbo_csv_sax_parser_finish(parser), 0);
      check_size_eq(capture.rows_ended, 2);
      check_size_eq(capture.fields, 2);
      check_str_eq(capture.values[1], "say \"hi\"");
      turbo_csv_sax_parser_destroy(parser);
    }
  }

  describe("INI") {
    it("should parse INI correctly") {
      const char *ini_data = "[section]\nkey=value\n";
      void *result = NULL;
      int rc = turbo_parse_ini((const uint8_t *)ini_data, strlen(ini_data), &result);

      check_int_eq(rc, 0);
      check_not_null(result);

      turbo_free_ini(&result);
      check_null(result);
    }
  }

  describe("URI") {
    it("should parse URI correctly") {
      const char *uri_data = "https://example.com:8080/path?query#frag";
      void *result = NULL;
      int rc = turbo_parse_uri((const uint8_t *)uri_data, strlen(uri_data), &result);

      check_int_eq(rc, 0);
      check_not_null(result);

      turbo_free_uri(&result);
      check_null(result);
    }
  }

  describe("CMD Parser") {
    it("should parse command line arguments correctly") {
      turbo_cmd_parser_t *parser = turbo_cmd_create("test_app", "1.0");
      check_not_null(parser);

      bool verbose = false;
      char *output = NULL;
      int64_t count = 0;

      turbo_cmd_add_flag(parser, &verbose, "verbose", "v", "Enable verbose output");
      turbo_cmd_add_string(parser, &output, "output", "o", "Output file");
      turbo_cmd_add_integer(parser, &count, "count", "c", "Count items");

      char *arg0 = strdup("test_app");
      char *arg1 = strdup("--verbose");
      char *arg2 = strdup("-o");
      char *arg3 = strdup("file.txt");
      char *arg4 = strdup("--count=10");

      char *argv[] = {arg0, arg1, arg2, arg3, arg4};
      int argc = 5;

      turbo_cmd_parse(parser, argc, argv, false);

      check(verbose);
      check_not_null(output);
      check_str_eq(output, "file.txt");
      check_long_eq(count, 10);

      free(arg0);
      free(arg1);
      free(arg2);
      free(arg3);
      free(arg4);

      turbo_cmd_destroy(parser);
    }
  }

  describe("TOON") {
    it("should parse TOON data correctly") {
      const char *toon_data = "server:\n"
                              "  host: \"localhost\"\n"
                              "  port: 1883\n"
                              "  enabled: true\n"
                              "  timeout: 5.5\n"
                              "topics: [\"a\", \"b\", \"c\"]\n";

      turbo_toon_node_t *root = NULL;
      int rc = turbo_parse_toon((const uint8_t *)toon_data, strlen(toon_data), &root);

      check_int_eq(rc, 0);
      check_not_null(root);
      check_int_eq(turbo_toon_type(root), TURBO_TOON_OBJECT);

      turbo_toon_node_t *host = turbo_toon_get(root, "server.host");
      check_not_null(host);
      check_int_eq(turbo_toon_type(host), TURBO_TOON_STRING);
      check_str_eq(turbo_toon_string(host), "localhost");

      turbo_toon_node_t *port = turbo_toon_get(root, "server.port");
      check_not_null(port);
      check_int_eq(turbo_toon_int(port), 1883);

      turbo_toon_node_t *enabled = turbo_toon_get(root, "server.enabled");
      check_not_null(enabled);
      check(turbo_toon_bool(enabled));

      turbo_toon_node_t *timeout = turbo_toon_get(root, "server.timeout");
      check_not_null(timeout);
      check_float_eq(turbo_toon_number(timeout), 5.5, 0.001);

      turbo_toon_node_t *topics = turbo_toon_get(root, "topics");
      check_not_null(topics);
      check_int_eq(turbo_toon_type(topics), TURBO_TOON_LIST);
      check_uint_eq(turbo_toon_array_size(topics), 3);

      turbo_toon_node_t *topic0 = turbo_toon_array_get(topics, 0);
      check_not_null(topic0);
      check_str_eq(turbo_toon_string(topic0), "a");

      // Test serialization
      size_t serialized_len = 0;
      char *serialized = turbo_toon_serialize(root, &serialized_len);
      check_not_null(serialized);
      check(serialized_len > 0);
      check(strstr(serialized, "host: \"localhost\"") != NULL);
      check(strstr(serialized, "port: 1883") != NULL);
      check(strstr(serialized, "topics: [\"a\", \"b\", \"c\"]") != NULL);

      turbo_toon_serialize_free(serialized);

      // Test JSON serialization
      char *json_out = turbo_toon_serialize_json(root, NULL);
      check_not_null(json_out);
      check(strstr(json_out, "\"host\": \"localhost\"") != NULL);
      turbo_toon_serialize_json_free(json_out);

      turbo_free_toon(&root);
      check_null(root);
    }

    it("should parse TOON from JSON correctly") {
      const char *json_in = "{\"name\": \"test\", \"value\": 123}";
      turbo_toon_node_t *toon = turbo_toon_from_json(json_in, strlen(json_in));

      check_not_null(toon);
      check_int_eq(turbo_toon_type(toon), TURBO_TOON_OBJECT);
      check_str_eq(turbo_toon_string(turbo_toon_get(toon, "name")), "test");
      check_int_eq(turbo_toon_int(turbo_toon_get(toon, "value")), 123);

      turbo_free_toon(&toon);
      check_null(toon);
    }

    it("should adapt independently owned TOON and JSON DOMs") {
      static const char json_text[] =
          "{\"name\":\"Ada\",\"items\":[1,true]}";
      turbo_json_doc_t *json = NULL;
      turbo_json_doc_t *roundtrip = NULL;
      turbo_toon_node_t *toon = NULL;

      check_int_eq(turbo_parse_json((const uint8_t *)json_text,
                                    sizeof(json_text) - 1U, &json),
                   TURBO_OK);
      check_not_null(json);
      check_int_eq(turbo_toon_from_json_doc(json, &toon), TURBO_OK);
      check_not_null(toon);
      turbo_free_json(&json);
      check_null(json);

      if (toon) {
        check_str_eq(turbo_toon_string(turbo_toon_get(toon, "name")),
                     "Ada");
        check_int_eq(turbo_toon_to_json_doc(toon, &roundtrip), TURBO_OK);
        check_not_null(roundtrip);
      }
      turbo_free_toon(&toon);
      check_null(toon);

      if (roundtrip) {
        check_str_eq(turbo_json_get_string(roundtrip, "name"), "Ada");
        check_size_eq(turbo_json_array_size(
                          turbo_json_object_get(roundtrip, "items")),
                      2U);
      }
      turbo_free_json(&roundtrip);
      check_null(roundtrip);
    }

    it("should report invalid DOM adapter arguments") {
      turbo_json_doc_t *json = (turbo_json_doc_t *)(uintptr_t)1;
      turbo_toon_node_t *toon = (turbo_toon_node_t *)(uintptr_t)1;

      check_int_eq(turbo_toon_from_json_doc(NULL, &toon), TURBO_EINVAL);
      check_null(toon);
      check_int_eq(turbo_toon_to_json_doc(NULL, &json), TURBO_EINVAL);
      check_null(json);
      check_int_eq(turbo_toon_from_json_doc(NULL, NULL), TURBO_EINVAL);
      check_int_eq(turbo_toon_to_json_doc(NULL, NULL), TURBO_EINVAL);
    }
  }

  describe("DotEnv") {
    it("should load DotEnv file correctly") {
      const char *env_file = ".env.turbo_test";
      FILE *f = fopen(env_file, "w");
      if (f) {
        fprintf(f, "TURBO_PARSER_TEST=success\n");
        fclose(f);
      }

      int rc = turbo_dotenv_load(env_file, true);
      check_int_eq(rc, 0);

      char *val = getenv("TURBO_PARSER_TEST");
      check_not_null(val);
      check_str_eq(val, "success");

      remove(env_file);
    }
  }

  describe("TOML") {
    it("should parse TOML correctly") {
      const char *toml_data = "[server]\n"
                              "host = \"localhost\"\n"
                              "port = 8080\n"
                              "enabled = true\n"
                              "timeout = 5.0\n"
                              "started = 1979-05-27T07:32:00Z\n"
                              "\n"
                              "[[databases]]\n"
                              "name = \"db1\"\n"
                              "\n"
                              "[[databases]]\n"
                              "name = \"db2\"\n";

      turbo_toml_t *root = NULL;
      int rc = turbo_parse_toml((const uint8_t *)toml_data, strlen(toml_data), &root);

      check_int_eq(rc, 0);
      check_not_null(root);

      // Test table access
      turbo_toml_t *server = turbo_toml_table(root, "server");
      check_not_null(server);

      // Test string
      turbo_toml_value_t host = turbo_toml_string(server, "host");
      check(host.ok);
      check_str_eq(host.u.s, "localhost");
      free(host.u.s);

      // Test int
      turbo_toml_value_t port = turbo_toml_int(server, "port");
      check(port.ok);
      check_int_eq(port.u.i, 8080);

      // Test bool
      turbo_toml_value_t enabled = turbo_toml_bool(server, "enabled");
      check(enabled.ok);
      check(enabled.u.b);

      // Test double
      turbo_toml_value_t timeout = turbo_toml_double(server, "timeout");
      check(timeout.ok);
      check_float_eq(timeout.u.d, 5.0, 0.001);

      // Test timestamp
      turbo_toml_value_t started = turbo_toml_timestamp(server, "started");
      check(started.ok);
      check_int_eq(started.u.ts.kind, 'd');

      // Test array of tables
      turbo_toml_array_t *dbs = turbo_toml_array(root, "databases");
      check_not_null(dbs);
      check_int_eq(turbo_toml_array_len(dbs), 2);

      turbo_toml_t *db1 = turbo_toml_array_table(dbs, 0);
      check_not_null(db1);
      turbo_toml_value_t name1 = turbo_toml_string(db1, "name");
      check(name1.ok);
      check_str_eq(name1.u.s, "db1");
      free(name1.u.s);

      turbo_free_toml(&root);
      check_null(root);
    }
  }

  describe("Datetime") {
    it("should parse various datetime formats correctly") {
      turbo_datetime_t dt;
      const char *dt_str = "2006-03-14T13:27:54.123+03:45";
      int rc = turbo_parse_datetime(dt_str, strlen(dt_str), &dt);

      check_int_eq(rc, 0);
      check_int_eq(dt.year, 2006);
      check_int_eq(dt.month, 3);
      check_int_eq(dt.day, 14);
      check_int_eq(dt.hour, 13);
      check_int_eq(dt.minute, 27);
      check_int_eq(dt.second, 54);
      check_int_eq(dt.millisecond, 123);
      check_int_eq(dt.tz_offset, 225);
      check(dt.has_tz);

      // Test conversion to time_t
      time_t t = turbo_datetime_to_time(&dt);
      check(t != (time_t)-1);

      // Test RFC 822 formatting
      char buf[64];
      rc = turbo_datetime_format_rfc822(t, buf, sizeof(buf));
      check(rc > 0);
      check(strstr(buf, "2006") != NULL);
      check(strstr(buf, "Mar") != NULL);
    }
  }

  describe("Modbus") {
    it("should write and read a Modbus TCP struct") {
      uint8_t pdu_data[] = {0x00, 0x6B, 0x00, 0x03};
      turbo_modbus_tcp_adu_t adu = {
          .transaction_id = 0x1234,
          .protocol_id = 0,
          .unit_id = 0x11,
          .pdu =
              {
                  .function_code = 0x03,
                  .data = pdu_data,
                  .data_size = sizeof(pdu_data),
              },
      };
      uint8_t buf[TURBO_MODBUS_TCP_MAX_ADU_SIZE];

      size_t written = turbo_modbus_tcp_write(&adu, buf, sizeof(buf));
      check_size_eq(written, 12);

      turbo_modbus_tcp_adu_t parsed;
      int rc = turbo_modbus_tcp_read(buf, written, &parsed);
      check_int_eq(rc, TURBO_MODBUS_PARSE_OK);
      check_int_eq(parsed.transaction_id, 0x1234);
      check_int_eq(parsed.unit_id, 0x11);
      check_int_eq(parsed.pdu.function_code, 0x03);
      check_mem_eq(parsed.pdu.data, pdu_data, sizeof(pdu_data));
    }

    it("should write and read a Modbus RTU struct") {
      uint8_t pdu_data[] = {0x00, 0x00, 0x00, 0x0A};
      turbo_modbus_rtu_adu_t adu = {
          .address = 0x01,
          .pdu =
              {
                  .function_code = 0x03,
                  .data = pdu_data,
                  .data_size = sizeof(pdu_data),
              },
      };
      uint8_t buf[TURBO_MODBUS_RTU_MAX_ADU_SIZE];

      size_t written = turbo_modbus_rtu_write(&adu, buf, sizeof(buf));
      check_size_eq(written, 8);
      check_int_eq(buf[6], 0xC5);
      check_int_eq(buf[7], 0xCD);

      turbo_modbus_rtu_adu_t parsed;
      int rc = turbo_modbus_rtu_read(buf, written, &parsed);
      check_int_eq(rc, TURBO_MODBUS_PARSE_OK);
      check_int_eq(parsed.address, 0x01);
      check_int_eq(parsed.pdu.function_code, 0x03);
      check_mem_eq(parsed.pdu.data, pdu_data, sizeof(pdu_data));
    }

    it("should round trip through generic Modbus read and write") {
      uint8_t pdu_data[] = {0x00, 0x01};
      turbo_modbus_tcp_adu_t tcp = {
          .transaction_id = 9,
          .protocol_id = 0,
          .unit_id = 1,
          .pdu =
              {
                  .function_code = 0x06,
                  .data = pdu_data,
                  .data_size = sizeof(pdu_data),
              },
      };
      turbo_modbus_adu_t adu = {
          .transport = TURBO_MODBUS_TRANSPORT_TCP,
          .frame = {.tcp = tcp},
      };
      uint8_t buf[TURBO_MODBUS_TCP_MAX_ADU_SIZE];
      uint8_t out[TURBO_MODBUS_TCP_MAX_ADU_SIZE];

      size_t written = turbo_modbus_write(&adu, buf, sizeof(buf));
      check(written > 0);

      turbo_modbus_adu_t parsed;
      int rc = turbo_modbus_read(TURBO_MODBUS_TRANSPORT_TCP, buf, written, &parsed);
      check_int_eq(rc, TURBO_MODBUS_PARSE_OK);

      size_t out_len = turbo_modbus_write(&parsed, out, sizeof(out));
      check_size_eq(out_len, written);
      check_mem_eq(out, buf, written);
    }
  }
}
