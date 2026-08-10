/**
 * @file test_json_parser.c
 * @brief Unit tests for JSON parser
 */

#include "json_parser.h"
#include "tinytest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  int null_count;
  int bool_count;
  int number_count;
  int string_count;
  int object_start_count;
  int object_end_count;
  int array_start_count;
  int array_end_count;
  int key_count;
  double last_number;
  char raw_numbers[4][32];
  char last_string[256];
  char last_key[256];
} sax_test_ctx_t;

static int sax_on_null(void *ctx) {
  ((sax_test_ctx_t *)ctx)->null_count++;
  return 0;
}

static int sax_on_bool(void *ctx, bool val) {
  (void)val;
  ((sax_test_ctx_t *)ctx)->bool_count++;
  return 0;
}

static int sax_on_number(void *ctx, double val) {
  sax_test_ctx_t *c = (sax_test_ctx_t *)ctx;
  c->number_count++;
  c->last_number = val;
  return 0;
}

static int sax_on_number_raw(void *ctx, const char *val, size_t len) {
  sax_test_ctx_t *c = (sax_test_ctx_t *)ctx;
  if (c->number_count >= 4 || len >= sizeof(c->raw_numbers[0])) return -1;
  memcpy(c->raw_numbers[c->number_count], val, len);
  c->raw_numbers[c->number_count][len] = '\0';
  c->number_count++;
  return 0;
}

static int sax_on_string(void *ctx, const char *val, size_t len) {
  sax_test_ctx_t *c = (sax_test_ctx_t *)ctx;
  c->string_count++;
  if (len < sizeof(c->last_string)) {
    memcpy(c->last_string, val, len);
    c->last_string[len] = '\0';
  }
  return 0;
}

static int sax_on_object_start(void *ctx) {
  ((sax_test_ctx_t *)ctx)->object_start_count++;
  return 0;
}

static int sax_on_object_end(void *ctx) {
  ((sax_test_ctx_t *)ctx)->object_end_count++;
  return 0;
}

static int sax_on_object_key(void *ctx, const char *key, size_t len) {
  sax_test_ctx_t *c = (sax_test_ctx_t *)ctx;
  c->key_count++;
  if (len < sizeof(c->last_key)) {
    memcpy(c->last_key, key, len);
    c->last_key[len] = '\0';
  }
  return 0;
}

static int sax_on_array_start(void *ctx) {
  ((sax_test_ctx_t *)ctx)->array_start_count++;
  return 0;
}

static int sax_on_array_end(void *ctx) {
  ((sax_test_ctx_t *)ctx)->array_end_count++;
  return 0;
}

static json_sax_handler_t test_handler = {.on_null = sax_on_null,
                                          .on_bool = sax_on_bool,
                                          .on_number = sax_on_number,
                                          .on_string = sax_on_string,
                                          .on_object_start = sax_on_object_start,
                                          .on_object_key = sax_on_object_key,
                                          .on_object_end = sax_on_object_end,
                                          .on_array_start = sax_on_array_start,
                                          .on_array_end = sax_on_array_end};

static json_sax_handler_raw_t test_raw_handler = {
    .on_null = sax_on_null,
    .on_bool = sax_on_bool,
    .on_number = sax_on_number_raw,
    .on_string = sax_on_string,
    .on_object_start = sax_on_object_start,
    .on_object_key = sax_on_object_key,
    .on_object_end = sax_on_object_end,
    .on_array_start = sax_on_array_start,
    .on_array_end = sax_on_array_end};

static int sax_fail_on_string(void *ctx, const char *val, size_t len) {
  (void)ctx;
  (void)val;
  (void)len;
  return -1;
}

typedef struct {
  size_t match_starts;
  size_t match_ends;
  json_type_t last_match_type;
  size_t strings;
  size_t numbers;
  size_t objects_started;
  size_t objects_ended;
  size_t arrays_started;
  size_t arrays_ended;
  size_t keys;
  char string_values[8][64];
  char number_values[8][64];
} json_path_stream_test_ctx_t;

static int json_path_stream_match_start(void *ctx, json_type_t type) {
  json_path_stream_test_ctx_t *capture = (json_path_stream_test_ctx_t *)ctx;
  capture->match_starts++;
  capture->last_match_type = type;
  return 0;
}

static int json_path_stream_match_end(void *ctx, json_type_t type) {
  json_path_stream_test_ctx_t *capture = (json_path_stream_test_ctx_t *)ctx;
  capture->match_ends++;
  capture->last_match_type = type;
  return 0;
}

static int json_path_stream_string(void *ctx, const char *value, size_t len) {
  json_path_stream_test_ctx_t *capture = (json_path_stream_test_ctx_t *)ctx;
  if (capture->strings >= 8 || len >= sizeof(capture->string_values[0])) return -1;
  memcpy(capture->string_values[capture->strings], value, len);
  capture->string_values[capture->strings][len] = '\0';
  capture->strings++;
  return 0;
}

static int json_path_stream_number(void *ctx, const char *value, size_t len) {
  json_path_stream_test_ctx_t *capture = (json_path_stream_test_ctx_t *)ctx;
  if (capture->numbers >= 8 || len >= sizeof(capture->number_values[0])) return -1;
  memcpy(capture->number_values[capture->numbers], value, len);
  capture->number_values[capture->numbers][len] = '\0';
  capture->numbers++;
  return 0;
}

static int json_path_stream_object_start(void *ctx) {
  ((json_path_stream_test_ctx_t *)ctx)->objects_started++;
  return 0;
}

static int json_path_stream_object_end(void *ctx) {
  ((json_path_stream_test_ctx_t *)ctx)->objects_ended++;
  return 0;
}

static int json_path_stream_array_start(void *ctx) {
  ((json_path_stream_test_ctx_t *)ctx)->arrays_started++;
  return 0;
}

static int json_path_stream_array_end(void *ctx) {
  ((json_path_stream_test_ctx_t *)ctx)->arrays_ended++;
  return 0;
}

static int json_path_stream_key(void *ctx, const char *key, size_t len) {
  (void)key;
  (void)len;
  ((json_path_stream_test_ctx_t *)ctx)->keys++;
  return 0;
}

static json_path_stream_handler_t json_path_stream_test_handler(void) {
  json_path_stream_handler_t handler = {0};
  handler.on_match_start = json_path_stream_match_start;
  handler.on_match_end = json_path_stream_match_end;
  handler.events.on_number = json_path_stream_number;
  handler.events.on_string = json_path_stream_string;
  handler.events.on_object_start = json_path_stream_object_start;
  handler.events.on_object_key = json_path_stream_key;
  handler.events.on_object_end = json_path_stream_object_end;
  handler.events.on_array_start = json_path_stream_array_start;
  handler.events.on_array_end = json_path_stream_array_end;
  return handler;
}

spec("json_parser") {
  describe("Basic Types") {
    it("should parse null correctly") {
      json_value_t *v = json_parse("null", 4);
      check_not_null(v);
      check_int_eq(json_type(v), JSON_NULL);
      check(json_is_null(v));
      json_free(v);
    }

    it("should parse true correctly") {
      json_value_t *v = json_parse("true", 4);
      check_not_null(v);
      check_int_eq(json_type(v), JSON_BOOL);
      check(json_bool(v));
      json_free(v);
    }

    it("should parse false correctly") {
      json_value_t *v = json_parse("false", 5);
      check_not_null(v);
      check_int_eq(json_type(v), JSON_BOOL);
      check(!json_bool(v));
      json_free(v);
    }
  }

  describe("Numbers") {
    it("should parse integers correctly") {
      json_value_t *v = json_parse("42", 2);
      check_not_null(v);
      check_int_eq(json_type(v), JSON_NUMBER);
      check_float_eq(json_number(v), 42.0, 0.001);
      json_free(v);
    }

    it("should parse negative numbers correctly") {
      json_value_t *v = json_parse("-123", 4);
      check_not_null(v);
      check_int_eq(json_type(v), JSON_NUMBER);
      check_float_eq(json_number(v), -123.0, 0.001);
      json_free(v);
    }

    it("should parse floating point numbers correctly") {
      json_value_t *v = json_parse("3.14159", 7);
      check_not_null(v);
      check_int_eq(json_type(v), JSON_NUMBER);
      check_float_eq(json_number(v), 3.14159, 0.00001);
      json_free(v);
    }

    it("should parse scientific notation correctly") {
      json_value_t *v = json_parse("1.5e10", 6);
      check_not_null(v);
      check_int_eq(json_type(v), JSON_NUMBER);
      check_float_eq(json_number(v), 1.5e10, 0.001);
      json_free(v);
    }

    it("should preserve exact numeric lexemes and serialize uint64 max") {
      const char *number = "18446744073709551615";
      size_t len = 0;
      char *serialized;
      json_value_t *v = json_parse(number, strlen(number));
      check_not_null(v);
      check_str_eq(json_number_text(v, &len), number);
      check_size_eq(len, strlen(number));
      json_free(v);

      v = json_create_uint64(UINT64_MAX);
      check_not_null(v);
      serialized = json_serialize(v, NULL);
      check_not_null(serialized);
      check_str_eq(serialized, number);
      free(serialized);
      json_free(v);
    }
  }

  describe("Strings") {
    it("should parse simple strings correctly") {
      json_value_t *v = json_parse("\"hello\"", 7);
      check_not_null(v);
      check_int_eq(json_type(v), JSON_STRING);
      check_str_eq(json_string(v), "hello");
      check_size_eq(json_string_len(v), 5);
      json_free(v);
    }

    it("should handle escape sequences in strings") {
      json_value_t *v = json_parse("\"hello\\nworld\"", 14);
      check_not_null(v);
      check_int_eq(json_type(v), JSON_STRING);
      check_str_eq(json_string(v), "hello\nworld");
      json_free(v);
    }

    it("should handle unicode escape sequences") {
      json_value_t *v = json_parse("\"\\u0041\\u0042\"", 14);
      check_not_null(v);
      check_int_eq(json_type(v), JSON_STRING);
      check_str_eq(json_string(v), "AB");
      json_free(v);
    }

    it("should decode UTF-16 surrogate pairs in values and keys") {
      const char *json = "{\"\\uD83D\\uDE00\":\"\\uD83D\\uDE00\"}";
      const char emoji[] = "\xF0\x9F\x98\x80";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      check_size_eq(json_object_key_len(v, 0), 4);
      check_mem_eq(json_object_key(v, 0), emoji, 4);
      check_size_eq(json_string_len(json_object_value(v, 0)), 4);
      check_mem_eq(json_string(json_object_value(v, 0)), emoji, 4);
      json_free(v);
    }

    it("should reject unpaired UTF-16 surrogates") {
      const char *invalid[] = {"\"\\uD83D\"", "\"\\uDE00\"", "\"\\uD83D\\n\""};
      for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        json_value_t *v = json_parse(invalid[i], strlen(invalid[i]));
        check_null(v);
        check_str_contains(json_get_error(), "surrogate");
      }
    }
  }

  describe("Arrays") {
    it("should parse empty arrays correctly") {
      json_value_t *v = json_parse("[]", 2);
      check_not_null(v);
      check_int_eq(json_type(v), JSON_ARRAY);
      check_size_eq(json_array_size(v), 0);
      json_free(v);
    }

    it("should parse simple arrays correctly") {
      json_value_t *v = json_parse("[1, 2, 3]", 9);
      check_not_null(v);
      check_int_eq(json_type(v), JSON_ARRAY);
      check_size_eq(json_array_size(v), 3);
      check_float_eq(json_number(json_array_get(v, 0)), 1.0, 0.001);
      check_float_eq(json_number(json_array_get(v, 1)), 2.0, 0.001);
      check_float_eq(json_number(json_array_get(v, 2)), 3.0, 0.001);
      json_free(v);
    }

    it("should handle nested arrays correctly") {
      json_value_t *v = json_parse("[[1, 2], [3, 4]]", 16);
      check_not_null(v);
      check_int_eq(json_type(v), JSON_ARRAY);
      check_size_eq(json_array_size(v), 2);

      json_value_t *inner = json_array_get(v, 0);
      check_int_eq(json_type(inner), JSON_ARRAY);
      check_size_eq(json_array_size(inner), 2);

      json_free(v);
    }
  }

  describe("Objects") {
    it("should parse empty objects correctly") {
      json_value_t *v = json_parse("{}", 2);
      check_not_null(v);
      check_int_eq(json_type(v), JSON_OBJECT);
      check_size_eq(json_object_size(v), 0);
      json_free(v);
    }

    it("should parse simple objects correctly") {
      const char *json = "{\"name\": \"test\", \"value\": 42}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      check_int_eq(json_type(v), JSON_OBJECT);
      check_size_eq(json_object_size(v), 2);

      check_str_eq(json_get_string(v, "name"), "test");
      check_int_eq(json_get_int(v, "value", 0), 42);

      json_free(v);
    }

    it("should handle nested objects correctly") {
      const char *json = "{\"outer\": {\"inner\": 123}}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_value_t *outer = json_object_get(v, "outer");
      check_not_null(outer);
      check_int_eq(json_type(outer), JSON_OBJECT);

      check_int_eq(json_get_int(outer, "inner", 0), 123);

      json_free(v);
    }

    it("should handle mixed complex structures") {
      const char *json = "{"
                         "  \"string\": \"hello\","
                         "  \"number\": 3.14,"
                         "  \"bool\": true,"
                         "  \"null\": null,"
                         "  \"array\": [1, 2, 3]"
                         "}";

      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      check_str_eq(json_get_string(v, "string"), "hello");
      check_float_eq(json_get_double(v, "number", 0), 3.14, 0.01);
      check(json_get_bool(v, "bool", false));
      check(json_is_null(json_object_get(v, "null")));

      json_value_t *arr = json_object_get(v, "array");
      check_size_eq(json_array_size(arr), 3);

      json_free(v);
    }

    it("should parse large configuration-like JSON correctly") {
      const char *json = "{"
                         "  \"listeners\": ["
                         "    {\"port\": 1883, \"transport\": \"tcp\"},"
                         "    {\"port\": 8883, \"transport\": \"tls\"}"
                         "  ],"
                         "  \"upstreams\": ["
                         "    {\"host\": \"10.0.0.1\", \"port\": 1883, \"weight\": 3}"
                         "  ],"
                         "  \"settings\": {"
                         "    \"max_clients\": 10000,"
                         "    \"connect_timeout_ms\": 5000"
                         "  }"
                         "}";

      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_value_t *listeners = json_object_get(v, "listeners");
      check_size_eq(json_array_size(listeners), 2);

      json_value_t *l0 = json_array_get(listeners, 0);
      check_int_eq(json_get_int(l0, "port", 0), 1883);
      check_str_eq(json_get_string(l0, "transport"), "tcp");

      json_value_t *upstreams = json_object_get(v, "upstreams");
      check_size_eq(json_array_size(upstreams), 1);

      json_value_t *u0 = json_array_get(upstreams, 0);
      check_str_eq(json_get_string(u0, "host"), "10.0.0.1");
      check_int_eq(json_get_int(u0, "weight", 0), 3);

      json_value_t *settings = json_object_get(v, "settings");
      check_int_eq(json_get_int(settings, "max_clients", 0), 10000);

      json_free(v);
    }

    it("should index large parsed objects for key and JSONPath lookup") {
      enum { KEY_COUNT = 128, BUFFER_CAPACITY = 4096 };
      char *json = (char *)malloc(BUFFER_CAPACITY);
      size_t offset = 0;
      json_value_t *v;

      check_not_null(json);
      json[offset++] = '{';
      for (int i = 0; i < KEY_COUNT; ++i) {
        int written = snprintf(json + offset, BUFFER_CAPACITY - offset,
                               "%s\"key_%d\":%d", i == 0 ? "" : ",", i, i);
        check_int_gt(written, 0);
        check((size_t)written < BUFFER_CAPACITY - offset);
        offset += (size_t)written;
      }
      json[offset++] = '}';
      json[offset] = '\0';

      v = json_parse(json, offset);
      check_not_null(v);
      check_size_eq(json_object_size(v), KEY_COUNT);
      check_int_eq(json_get_int(v, "key_0", -1), 0);
      check_int_eq(json_get_int(v, "key_63", -1), 63);
      check_int_eq(json_get_int(v, "key_127", -1), 127);
      check_null(json_object_get(v, "missing"));
      check_str_eq(json_object_key(v, KEY_COUNT - 1), "key_127");
      check_int_eq((int)json_number(json_path_get(v, "$.key_127")), 127);

      json_free(v);
      free(json);
    }

    it("should keep indexes coherent when builder objects grow and update") {
      enum { INITIAL_KEYS = 40 };
      json_value_t *obj = json_create_object();
      char key[32];

      check_not_null(obj);
      for (int i = 0; i < INITIAL_KEYS; ++i) {
        int written = snprintf(key, sizeof(key), "key_%d", i);
        check_int_gt(written, 0);
        json_object_set_number(obj, key, (double)i);
      }
      json_object_set_number(obj, "key_31", 999.0);
      json_object_set_number(obj, "key_40", 40.0);

      check_size_eq(json_object_size(obj), INITIAL_KEYS + 1);
      check_float_eq(json_get_double(obj, "key_31", -1.0), 999.0, 0.001);
      check_float_eq(json_get_double(obj, "key_40", -1.0), 40.0, 0.001);
      check_str_eq(json_object_key(obj, 31), "key_31");
      check_str_eq(json_object_key(obj, INITIAL_KEYS), "key_40");
      json_free(obj);
    }

    it("should preserve first-member semantics for indexed duplicate keys") {
      const char *json =
          "{\"dup\":1,\"k1\":1,\"k2\":2,\"k3\":3,\"k4\":4,"
          "\"k5\":5,\"k6\":6,\"k7\":7,\"k8\":8,\"k9\":9,"
          "\"k10\":10,\"k11\":11,\"k12\":12,\"k13\":13,"
          "\"k14\":14,\"k15\":15,\"dup\":2}";
      json_value_t *v = json_parse(json, strlen(json));
      char *serialized;

      check_not_null(v);
      check_size_eq(json_object_size(v), 17);
      check_int_eq((int)json_number(json_object_get(v, "dup")), 1);
      serialized = json_serialize(v, NULL);
      check_not_null(serialized);
      check_str_eq(serialized, json);
      json_serialize_free(serialized);
      json_object_set_number(v, "dup", 7.0);
      check_size_eq(json_object_size(v), 17);
      check_int_eq((int)json_number(json_object_get(v, "dup")), 7);
      check_int_eq((int)json_number(json_object_value(v, 16)), 2);
      json_free(v);
    }
  }

  describe("Auxiliary") {
    it("should build length-delimited strings and object keys") {
      json_value_t *obj = json_create_object();
      json_value_t *value = json_create_string_n("x\0y", 3);

      check_not_null(obj);
      check_not_null(value);
      check_true(json_object_add_n(obj, "a\0b", 3, value));
      check_size_eq(json_object_size(obj), 1);
      check_size_eq(json_object_key_len(obj, 0), 3);
      check_mem_eq(json_object_key(obj, 0), "a\0b", 3);
      check_size_eq(json_string_len(json_object_value(obj, 0)), 3);
      check_mem_eq(json_string(json_object_value(obj, 0)), "x\0y", 3);

      size_t len = 0;
      char *serialized = json_serialize(obj, &len);
      check_not_null(serialized);
      check_str_eq(serialized, "{\"a\\u0000b\":\"x\\u0000y\"}");
      json_serialize_free(serialized);
      json_free(obj);
    }

    it("should serialize int64 builders without double precision loss") {
      json_value_t *value = json_create_int64(INT64_C(9007199254740993));
      json_value_t *copy = json_clone(value);
      size_t len = 0;
      char *serialized = json_serialize(value, &len);
      check_not_null(serialized);
      check_str_eq(serialized, "9007199254740993");
      check_size_eq(len, 16);
      json_serialize_free(serialized);
      serialized = json_serialize(copy, &len);
      check_str_eq(serialized, "9007199254740993");
      json_serialize_free(serialized);
      json_free(copy);
      json_free(value);
    }

    it("should report checked builder argument failures") {
      json_value_t *array = json_create_array();
      json_value_t *value = json_create_number(1.0);

      check_not_null(array);
      check_not_null(value);
      check_false(json_array_add_checked(NULL, value));
      check_false(json_array_add_checked(array, NULL));
      check_false(json_object_add_checked(array, "key", value));
      check_false(json_object_add_n(array, "key", 3, value));
      check_size_eq(json_array_size(array), 0);

      check_true(json_array_add_checked(array, value));
      check_size_eq(json_array_size(array), 1);
      json_free(array);
    }

    it("should reject adopting a child into a second parent") {
      json_value_t *first = json_create_array();
      json_value_t *second = json_create_array();
      json_value_t *value = json_create_string("owned");

      check_not_null(first);
      check_not_null(second);
      check_not_null(value);
      check_true(json_array_add_checked(first, value));
      check_false(json_array_add_checked(second, value));
      check_size_eq(json_array_size(first), 1);
      check_size_eq(json_array_size(second), 0);

      json_free(second);
      json_free(first);
    }

    it("should reject an arena ownership cycle") {
      json_value_t *parent = json_create_array();
      json_value_t *child = json_create_array();

      check_not_null(parent);
      check_not_null(child);
      check_true(json_array_add_checked(parent, child));
      check_false(json_array_add_checked(child, parent));
      check_size_eq(json_array_size(parent), 1);
      check_size_eq(json_array_size(child), 0);
      json_free(parent);
    }

    it("should keep legacy builder wrappers working") {
      json_value_t *obj = json_create_object();
      json_value_t *array = json_create_array();

      check_not_null(obj);
      check_not_null(array);
      json_array_add(array, json_create_bool(true));
      json_object_add(obj, "items", array);
      check_size_eq(json_array_size(json_object_get(obj, "items")), 1);
      check_true(json_bool(json_array_get(json_object_get(obj, "items"), 0)));
      json_free(obj);
    }

    it("should handle whitespace around JSON") {
      const char *json = "  \n\t { \"key\" : \"value\" } \n";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      check_str_eq(json_get_string(v, "key"), "value");
      json_free(v);
    }

    it("should allow iterating over object keys and values") {
      const char *json = "{\"a\": 1, \"b\": 2, \"c\": 3}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      check_size_eq(json_object_size(v), 3);

      check_str_eq(json_object_key(v, 0), "a");
      check_str_eq(json_object_key(v, 1), "b");
      check_str_eq(json_object_key(v, 2), "c");

      check_float_eq(json_number(json_object_value(v, 0)), 1.0, 0.001);
      check_float_eq(json_number(json_object_value(v, 1)), 2.0, 0.001);
      check_float_eq(json_number(json_object_value(v, 2)), 3.0, 0.001);

      json_free(v);
    }

    it("should skip a long RFC whitespace prefix before lexing JSON") {
      enum { WHITESPACE_BYTES = 4096 };
      const char suffix[] = "{\"value\":7}";
      char *json = (char *)malloc(WHITESPACE_BYTES + sizeof(suffix));

      check_not_null(json);
      for (size_t i = 0; i < WHITESPACE_BYTES; ++i) {
        static const char whitespace[] = {' ', '\t', '\r', '\n'};
        json[i] = whitespace[i % sizeof(whitespace)];
      }
      memcpy(json + WHITESPACE_BYTES, suffix, sizeof(suffix));

      json_value_t *v = json_parse(json, WHITESPACE_BYTES + sizeof(suffix) - 1);
      check_not_null(v);
      check_int_eq(json_get_int(v, "value", 0), 7);

      json_free(v);
      free(json);
    }

    it("should lex a long plain ASCII string without changing its value") {
      enum { STRING_BYTES = 4096 };
      char *json = (char *)malloc(STRING_BYTES + 3);

      check_not_null(json);
      json[0] = '"';
      memset(json + 1, 'x', STRING_BYTES);
      json[STRING_BYTES + 1] = '"';
      json[STRING_BYTES + 2] = '\0';

      json_value_t *v = json_parse(json, STRING_BYTES + 2);
      check_not_null(v);
      check_size_eq(json_string_len(v), STRING_BYTES);
      check_mem_eq(json_string(v), json + 1, STRING_BYTES);

      json_free(v);
      free(json);
    }
  }

  describe("JSONPath") {
    it("should decode surrogate pairs in bracket property names") {
      const char emoji[] = "\xF0\x9F\x98\x80";
      const char *json = "{\"\\uD83D\\uDE00\":7}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_value_t *selected = json_path_get(v, "$['\\uD83D\\uDE00']");
      check_not_null(selected);
      check_int_eq((int)json_number(selected), 7);
      check_mem_eq(json_object_key(v, 0), emoji, 4);

      check_null(json_path_get(v, "$['\\uD83D']"));
      check_not_null(json_path_get_error());
      json_free(v);
    }

    it("should get a nested object member by path") {
      const char *json = "{\"listeners\":[{\"port\":1883,\"transport\":\"tcp\"},"
                         "{\"port\":8883,\"transport\":\"tls\"}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_value_t *port = json_path_get(v, "$.listeners[0].port");
      check_not_null(port);
      check_int_eq((int)json_number(port), 1883);

      json_value_t *last = json_path_get(v, "$.listeners[-1].transport");
      check_not_null(last);
      check_str_eq(json_string(last), "tls");

      json_free(v);
    }

    it("should return all wildcard matches") {
      const char *json = "{\"listeners\":[{\"transport\":\"tcp\"},{\"transport\":\"tls\"}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *result = json_path_query(v, "$.listeners[*].transport");
      check_not_null(result);
      check_size_eq(json_path_result_size(result), 2);
      check_str_eq(json_string(json_path_result_get(result, 0)), "tcp");
      check_str_eq(json_string(json_path_result_get(result, 1)), "tls");

      json_path_result_free(result);
      json_free(v);
    }

    it("should support bracket key union on objects") {
      const char *json = "{\"settings\":{\"max_clients\":10000,\"connect_timeout_ms\":5000,"
                         "\"name\":\"edge\"}}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *result =
          json_path_query(v, "$.settings['max_clients','connect_timeout_ms']");
      check_not_null(result);
      check_size_eq(json_path_result_size(result), 2);
      check_int_eq((int)json_number(json_path_result_get(result, 0)), 10000);
      check_int_eq((int)json_number(json_path_result_get(result, 1)), 5000);

      json_path_result_free(result);
      json_free(v);
    }

    it("should filter array elements using current-node paths") {
      const char *json = "{\"listeners\":[{\"port\":1883,\"transport\":\"tcp\"},"
                         "{\"port\":8883,\"transport\":\"tls\"},"
                         "{\"port\":8080,\"transport\":\"ws\"}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *result = json_path_query(v, "$.listeners[@.port >= 8000].transport");
      check_not_null(result);
      check_size_eq(json_path_result_size(result), 2);
      check_str_eq(json_string(json_path_result_get(result, 0)), "tls");
      check_str_eq(json_string(json_path_result_get(result, 1)), "ws");

      json_path_result_free(result);
      json_free(v);
    }

    it("should report invalid JSONPath expressions") {
      json_value_t *v = json_parse("{\"a\":1}", 7);
      check_not_null(v);

      json_path_result_t *result = json_path_query(v, "$.a[");
      check_null(result);
      check_not_null(json_path_get_error());

      json_free(v);
    }

    it("should provide indexed access to distant array elements") {
      enum { ELEMENTS = 4096 };
      const size_t capacity = (size_t)ELEMENTS * 6 + 3;
      char *json = (char *)malloc(capacity);
      size_t offset = 0;

      check_not_null(json);
      json[offset++] = '[';
      for (int i = 0; i < ELEMENTS; ++i) {
        offset += (size_t)snprintf(json + offset, capacity - offset, "%s%d", i == 0 ? "" : ",", i);
      }
      json[offset++] = ']';
      json[offset] = '\0';

      json_value_t *v = json_parse(json, offset);
      check_not_null(v);
      check_size_eq(json_array_size(v), ELEMENTS);
      check_float_eq(json_number(json_array_get(v, 0)), 0.0, 0.001);
      check_float_eq(json_number(json_array_get(v, 2048)), 2048.0, 0.001);
      check_float_eq(json_number(json_array_get(v, ELEMENTS - 1)), 4095.0, 0.001);

      json_free(v);
      free(json);
    }

    it("should reuse a compiled JSONPath after its source expression changes") {
      char expr[] = "$.listeners[-1].transport";
      const char *first_json =
          "{\"listeners\":[{\"transport\":\"tcp\"},{\"transport\":\"tls\"}]}";
      const char *second_json =
          "{\"listeners\":[{\"transport\":\"quic\"},{\"transport\":\"ws\"}]}";
      json_path_program_t *program = json_path_compile(expr);
      json_value_t *first = json_parse(first_json, strlen(first_json));
      json_value_t *second = json_parse(second_json, strlen(second_json));

      check_not_null(program);
      check_not_null(first);
      check_not_null(second);
      memset(expr, 'x', sizeof(expr) - 1U);
      check_str_eq(json_string(json_path_get_compiled(first, program)), "tls");
      check_str_eq(json_string(json_path_get_compiled(second, program)), "ws");

      json_free(second);
      json_free(first);
      json_path_program_free(program);
    }

    it("should execute compiled filters and unions without reparsing") {
      const char *json =
          "{\"listeners\":[{\"port\":1883,\"transport\":\"tcp\"},"
          "{\"port\":8883,\"transport\":\"tls\"},"
          "{\"port\":8080,\"transport\":\"ws\"}],"
          "\"settings\":{\"max_clients\":10000,\"connect_timeout_ms\":5000}}";
      json_value_t *root = json_parse(json, strlen(json));
      json_path_program_t *filter = json_path_compile(
          "$.listeners[@.port >= 8000].transport");
      json_path_program_t *union_program = json_path_compile(
          "$.settings['max_clients','connect_timeout_ms']");
      json_path_result_t *filtered;
      json_path_result_t *union_result;

      check_not_null(root);
      check_not_null(filter);
      check_not_null(union_program);
      filtered = json_path_query_compiled(root, filter);
      union_result = json_path_query_compiled(root, union_program);
      check_not_null(filtered);
      check_not_null(union_result);
      check_size_eq(json_path_result_size(filtered), 2);
      check_str_eq(json_string(json_path_result_get(filtered, 0)), "tls");
      check_str_eq(json_string(json_path_result_get(filtered, 1)), "ws");
      check_size_eq(json_path_result_size(union_result), 2);
      check_int_eq((int)json_number(json_path_result_get(union_result, 0)), 10000);
      check_int_eq((int)json_number(json_path_result_get(union_result, 1)), 5000);

      json_path_result_free(union_result);
      json_path_result_free(filtered);
      json_path_program_free(union_program);
      json_path_program_free(filter);
      json_free(root);
    }

    it("should reject invalid compiled JSONPath expressions") {
      json_path_program_t *program;
      check_null(json_path_compile("$.listeners["));
      check_not_null(json_path_get_error());
      program = json_path_compile("$.listeners");
      check_not_null(program);
      check_null(json_path_get_error());
      json_path_program_free(program);
    }

    it("should stream wildcard scalar matches without building a DOM") {
      const char *json =
          "{\"items\":[{\"name\":\"Alice\"},{\"name\":\"Bob\"}]}";
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *program = json_path_compile("$.items[*].name");
      json_path_stream_t *stream = json_path_stream_create(program, &handler, &capture);

      check_not_null(program);
      check_not_null(stream);
      for (size_t i = 0; i < strlen(json); ++i)
        check_int_eq(json_path_stream_feed(stream, json + i, 1), 0);
      check_int_eq(json_path_stream_finish(stream), 0);
      check_null(json_path_stream_error(stream));
      check_size_eq(json_path_stream_match_count(stream), 2);
      check_size_eq(capture.match_starts, 2);
      check_size_eq(capture.match_ends, 2);
      check_size_eq(capture.strings, 2);
      check_str_eq(capture.string_values[0], "Alice");
      check_str_eq(capture.string_values[1], "Bob");

      json_path_stream_destroy(stream);
      json_path_program_free(program);
    }

    it("should stream a selected object as balanced SAX events") {
      const char *json =
          "{\"items\":[{\"name\":\"A\",\"id\":1},{\"name\":\"B\",\"id\":2}]}";
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *program = json_path_compile("$.items[1]");
      json_path_stream_t *stream = json_path_stream_create(program, &handler, &capture);

      check_not_null(program);
      check_not_null(stream);
      check_int_eq(json_path_stream_feed(stream, json, strlen(json)), 0);
      check_int_eq(json_path_stream_finish(stream), 0);
      check_size_eq(json_path_stream_match_count(stream), 1);
      check_int_eq(capture.last_match_type, JSON_OBJECT);
      check_size_eq(capture.objects_started, 1);
      check_size_eq(capture.objects_ended, 1);
      check_size_eq(capture.keys, 2);
      check_size_eq(capture.strings, 1);
      check_size_eq(capture.numbers, 1);
      check_str_eq(capture.string_values[0], "B");
      check_str_eq(capture.number_values[0], "2");

      json_path_stream_destroy(stream);
      json_path_program_free(program);
    }

    it("should stream object key unions with exact number tokens") {
      const char *json = "{\"settings\":{\"a\":100,\"skip\":0,\"b\":200}}";
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *program = json_path_compile("$.settings['a','b']");
      json_path_stream_t *stream = json_path_stream_create(program, &handler, &capture);

      check_not_null(program);
      check_not_null(stream);
      check_int_eq(json_path_stream_feed(stream, json, 11), 0);
      check_int_eq(json_path_stream_feed(stream, json + 11, strlen(json) - 11), 0);
      check_int_eq(json_path_stream_finish(stream), 0);
      check_size_eq(json_path_stream_match_count(stream), 2);
      check_size_eq(capture.numbers, 2);
      check_str_eq(capture.number_values[0], "100");
      check_str_eq(capture.number_values[1], "200");

      json_path_stream_destroy(stream);
      json_path_program_free(program);
    }

    it("should stream array index unions") {
      const char *json = "{\"items\":[10,20,30]}";
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *program = json_path_compile("$.items[0,2]");
      json_path_stream_t *stream = json_path_stream_create(program, &handler, &capture);

      check_not_null(program);
      check_not_null(stream);
      check_int_eq(json_path_stream_feed(stream, json, strlen(json)), 0);
      check_int_eq(json_path_stream_finish(stream), 0);
      check_size_eq(json_path_stream_match_count(stream), 2);
      check_size_eq(capture.numbers, 2);
      check_str_eq(capture.number_values[0], "10");
      check_str_eq(capture.number_values[1], "30");

      json_path_stream_destroy(stream);
      json_path_program_free(program);
    }

    it("should reject non-streamable filters and negative indexes") {
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *filter =
          json_path_compile("$.items[@.port >= 8000].transport");
      json_path_program_t *negative = json_path_compile("$.items[-1]");

      check_not_null(filter);
      check_not_null(negative);
      check_null(json_path_stream_create(filter, &handler, &capture));
      check_str_contains(json_path_get_error(), "not streamable");
      check_null(json_path_stream_create(negative, &handler, &capture));
      check_str_contains(json_path_get_error(), "not streamable");

      json_path_program_free(negative);
      json_path_program_free(filter);
    }

    it("should stop a JSONPath stream when a selected callback fails") {
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *program = json_path_compile("$.name");
      json_path_stream_t *stream;
      handler.events.on_string = sax_fail_on_string;
      stream = json_path_stream_create(program, &handler, &capture);

      check_not_null(program);
      check_not_null(stream);
      check_int_eq(json_path_stream_feed(stream, "{\"name\":\"stop\"}", 15), -1);
      check_str_contains(json_path_stream_error(stream), "callback");
      check_size_eq(json_path_stream_match_count(stream), 0);

      json_path_stream_destroy(stream);
      json_path_program_free(program);
    }
  }

  describe("Error Handling") {
    it("should return NULL and set error for invalid JSON") {
      json_value_t *v = json_parse("invalid", 7);
      check_null(v);
      check_not_null(json_get_error());
    }

    it("should return NULL for unclosed braces") {
      json_value_t *v = json_parse("{\"key\": 1", 9);
      check_null(v);
    }
  }

  describe("SAX Parsing") {
    it("should SAX parse a simple object correctly") {
      const char *json = "{\"name\": \"test\", \"value\": 42}";
      sax_test_ctx_t ctx = {0};

      int ret = json_parse_sax(json, strlen(json), &test_handler, &ctx);
      check_int_eq(ret, 0);
      check_int_eq(ctx.object_start_count, 1);
      check_int_eq(ctx.object_end_count, 1);
      check_int_eq(ctx.key_count, 2);
      check_int_eq(ctx.string_count, 1);
      check_int_eq(ctx.number_count, 1);
      check_float_eq(ctx.last_number, 42.0, 0.001);
    }

    it("should SAX parse an array correctly") {
      const char *json = "[1, 2, 3, 4, 5]";
      sax_test_ctx_t ctx = {0};

      int ret = json_parse_sax(json, strlen(json), &test_handler, &ctx);
      check_int_eq(ret, 0);
      check_int_eq(ctx.array_start_count, 1);
      check_int_eq(ctx.array_end_count, 1);
      check_int_eq(ctx.number_count, 5);
      check_float_eq(ctx.last_number, 5.0, 0.001);
    }

    it("should SAX parse nested structures correctly") {
      const char *json = "{\"arr\": [1, 2], \"obj\": {\"x\": true}}";
      sax_test_ctx_t ctx = {0};

      int ret = json_parse_sax(json, strlen(json), &test_handler, &ctx);
      check_int_eq(ret, 0);
      check_int_eq(ctx.object_start_count, 2);
      check_int_eq(ctx.object_end_count, 2);
      check_int_eq(ctx.array_start_count, 1);
      check_int_eq(ctx.array_end_count, 1);
      check_int_eq(ctx.key_count, 3);
      check_int_eq(ctx.number_count, 2);
      check_int_eq(ctx.bool_count, 1);
    }

    it("should SAX parse all JSON types correctly") {
      const char *json =
          "{\"n\": null, \"b\": false, \"i\": 123, \"s\": \"hello\", \"a\": [], \"o\": {}}";
      sax_test_ctx_t ctx = {0};

      int ret = json_parse_sax(json, strlen(json), &test_handler, &ctx);
      check_int_eq(ret, 0);
      check_int_eq(ctx.null_count, 1);
      check_int_eq(ctx.bool_count, 1);
      check_int_eq(ctx.number_count, 1);
      check_int_eq(ctx.string_count, 1);
      check_int_eq(ctx.object_start_count, 2);
      check_int_eq(ctx.object_end_count, 2);
      check_int_eq(ctx.array_start_count, 1);
      check_int_eq(ctx.array_end_count, 1);
    }

    it("should handle empty objects in SAX") {
      const char *json = "{}";
      sax_test_ctx_t ctx = {0};

      int ret = json_parse_sax(json, strlen(json), &test_handler, &ctx);
      check_int_eq(ret, 0);
      check_int_eq(ctx.object_start_count, 1);
      check_int_eq(ctx.object_end_count, 1);
    }

    it("should handle empty arrays in SAX") {
      const char *json = "[]";
      sax_test_ctx_t ctx = {0};

      int ret = json_parse_sax(json, strlen(json), &test_handler, &ctx);
      check_int_eq(ret, 0);
      check_int_eq(ctx.array_start_count, 1);
      check_int_eq(ctx.array_end_count, 1);
    }

    it("should incrementally SAX parse split strings, literals, and numbers") {
      const char *parts[] = {"{\"na", "me\":\"a\\", "\"b\",", "\"items\":[tr",
                             "ue,n",  "ull,12.5",   "e2]}"};
      sax_test_ctx_t ctx = {0};
      json_sax_parser_t *parser = json_sax_parser_create(&test_handler, &ctx);
      check_not_null(parser);

      for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        check_int_eq(json_sax_parser_feed(parser, parts[i], strlen(parts[i])), 0);
      }
      check_int_eq(json_sax_parser_finish(parser), 0);

      check_int_eq(ctx.object_start_count, 1);
      check_int_eq(ctx.object_end_count, 1);
      check_int_eq(ctx.array_start_count, 1);
      check_int_eq(ctx.array_end_count, 1);
      check_int_eq(ctx.key_count, 2);
      check_int_eq(ctx.string_count, 1);
      check_str_eq(ctx.last_string, "a\"b");
      check_int_eq(ctx.bool_count, 1);
      check_int_eq(ctx.null_count, 1);
      check_int_eq(ctx.number_count, 1);
      check_float_eq(ctx.last_number, 1250.0, 0.001);

      json_sax_parser_destroy(parser);
    }

    it("should incrementally SAX parse byte by byte") {
      const char *json = "[1,2,{\"x\":\"y\"}]";
      sax_test_ctx_t ctx = {0};
      json_sax_parser_t *parser = json_sax_parser_create(&test_handler, &ctx);
      check_not_null(parser);

      for (size_t i = 0; i < strlen(json); ++i) {
        check_int_eq(json_sax_parser_feed(parser, json + i, 1), 0);
      }
      check_int_eq(json_sax_parser_finish(parser), 0);

      check_int_eq(ctx.array_start_count, 1);
      check_int_eq(ctx.array_end_count, 1);
      check_int_eq(ctx.object_start_count, 1);
      check_int_eq(ctx.object_end_count, 1);
      check_int_eq(ctx.key_count, 1);
      check_int_eq(ctx.string_count, 1);
      check_int_eq(ctx.number_count, 2);
      check_float_eq(ctx.last_number, 2.0, 0.001);
      check_str_eq(ctx.last_key, "x");
      check_str_eq(ctx.last_string, "y");

      json_sax_parser_destroy(parser);
    }

    it("should preserve exact number tokens in raw SAX mode") {
      const char *parts[] = {"[9007199254740", "993,-922337203685477", "5808,",
                             "18446744073709551615]"};
      sax_test_ctx_t ctx = {0};
      json_sax_parser_t *parser = json_sax_parser_create_raw(&test_raw_handler, &ctx);
      check_not_null(parser);

      for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        check_int_eq(json_sax_parser_feed(parser, parts[i], strlen(parts[i])), 0);
      }
      check_int_eq(json_sax_parser_finish(parser), 0);
      check_int_eq(ctx.number_count, 3);
      check_str_eq(ctx.raw_numbers[0], "9007199254740993");
      check_str_eq(ctx.raw_numbers[1], "-9223372036854775808");
      check_str_eq(ctx.raw_numbers[2], "18446744073709551615");
      json_sax_parser_destroy(parser);

      memset(&ctx, 0, sizeof(ctx));
      check_int_eq(json_parse_sax_raw("1.25e+9", 7, &test_raw_handler, &ctx), 0);
      check_str_eq(ctx.raw_numbers[0], "1.25e+9");
      check_null(json_sax_parser_create_raw(NULL, &ctx));
      check_int_eq(json_parse_sax_raw(NULL, 0, &test_raw_handler, &ctx), -1);
    }

    it("should retain legacy double number callbacks") {
      sax_test_ctx_t ctx = {0};
      check_int_eq(json_parse_sax("9007199254740993", 16, &test_handler, &ctx), 0);
      check_int_eq(ctx.number_count, 1);
      check_double_within_abs(ctx.last_number, 9007199254740992.0, 0.0);
    }

    it("should SAX decode surrogate pairs across chunks") {
      const char *parts[] = {"{\"\\uD83D", "\\uDE00\":\"\\uD83D", "\\uDE00\"}"};
      const char emoji[] = "\xF0\x9F\x98\x80";
      sax_test_ctx_t ctx = {0};
      json_sax_parser_t *parser = json_sax_parser_create(&test_handler, &ctx);
      check_not_null(parser);

      for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i)
        check_int_eq(json_sax_parser_feed(parser, parts[i], strlen(parts[i])), 0);
      check_int_eq(json_sax_parser_finish(parser), 0);
      check_mem_eq(ctx.last_key, emoji, 4);
      check_mem_eq(ctx.last_string, emoji, 4);

      json_sax_parser_destroy(parser);
    }

    it("should SAX reject unpaired surrogates") {
      const char *invalid[] = {"\"\\uD83D\"", "\"\\uDE00\"", "\"\\uD83D\\n\""};
      for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        sax_test_ctx_t ctx = {0};
        check_int_eq(json_parse_sax(invalid[i], strlen(invalid[i]), &test_handler, &ctx), -1);
      }
    }

    it("should report incomplete input at incremental finish") {
      const char *json = "{\"a\": [";
      sax_test_ctx_t ctx = {0};
      json_sax_parser_t *parser = json_sax_parser_create(&test_handler, &ctx);
      check_not_null(parser);

      check_int_eq(json_sax_parser_feed(parser, json, strlen(json)), 0);
      check_int_eq(json_sax_parser_finish(parser), -1);
      check_not_null(json_sax_parser_error(parser));

      json_sax_parser_destroy(parser);
    }

    it("should stop incremental SAX parsing when a callback fails") {
      json_sax_handler_t handler = test_handler;
      sax_test_ctx_t ctx = {0};
      handler.on_string = sax_fail_on_string;

      json_sax_parser_t *parser = json_sax_parser_create(&handler, &ctx);
      check_not_null(parser);
      check_int_eq(json_sax_parser_feed(parser, "\"stop\"", 6), -1);
      check_str_contains(json_sax_parser_error(parser), "callback");

      json_sax_parser_destroy(parser);
    }

    it("should reject extra data after one JSON document in SAX") {
      sax_test_ctx_t ctx = {0};
      int ret = json_parse_sax("true false", strlen("true false"), &test_handler, &ctx);
      check_int_eq(ret, -1);
    }
  }
}
