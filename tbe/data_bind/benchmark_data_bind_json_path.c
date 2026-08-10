#include "data_bind.h"
#include "tinytest.h"

#include <stdio.h>
#include <string.h>

#define DATA_BIND_JSON_PATH_BENCH_SAMPLES 200U
#define DATA_BIND_JSON_PATH_BENCH_RECORDS 256U
#define DATA_BIND_JSON_PATH_BENCH_CAPACITY 65536U

static const char DATA_BIND_JSON_PATH_BENCH_SCHEMA[] =
    "enum Side <uint8> { Buy = 1; Sell = 2; }\n"
    "message Order { uint32 id; Side side; string symbol; }\n";
static const char DATA_BIND_JSON_PATH_BENCH_PATH[] = "$.payload.orders[*]";
static const char DATA_BIND_JSON_PATH_BENCH_SINGLE_PATH[] = "$.payload.orders[255]";

static DataBind *g_json_path_codec;
static char g_json_path_input[DATA_BIND_JSON_PATH_BENCH_CAPACITY];
static size_t g_json_path_input_len;
static size_t g_json_path_sink;
static size_t g_json_path_failures;

static void data_bind_json_path_bench_prepare(void) {
  size_t offset = 0;
  size_t i;
  int written;

  check_int_eq(data_bind_create_from_text(
                   DATA_BIND_JSON_PATH_BENCH_SCHEMA,
                   sizeof(DATA_BIND_JSON_PATH_BENCH_SCHEMA) - 1U, &g_json_path_codec, NULL),
               DATA_BIND_OK);
  check_not_null(g_json_path_codec);

  written = snprintf(g_json_path_input, sizeof(g_json_path_input),
                     "{\"metadata\":{\"source\":\"benchmark\"},\"payload\":{\"orders\":[");
  check_true(written > 0);
  if (written <= 0) return;
  offset = (size_t)written;
  for (i = 0; i < DATA_BIND_JSON_PATH_BENCH_RECORDS; ++i) {
    written = snprintf(g_json_path_input + offset, sizeof(g_json_path_input) - offset,
                       "%s{\"id\":%u,\"side\":\"%s\",\"symbol\":\"SYM%04u\"}",
                       i == 0 ? "" : ",", (unsigned)(i + 1U),
                       (i & 1U) == 0 ? "Buy" : "Sell", (unsigned)i);
    check_true(written > 0 && (size_t)written < sizeof(g_json_path_input) - offset);
    if (written <= 0 || (size_t)written >= sizeof(g_json_path_input) - offset) return;
    offset += (size_t)written;
  }
  written = snprintf(g_json_path_input + offset, sizeof(g_json_path_input) - offset,
                     "]},\"tail\":[1,2,3,4]}");
  check_true(written > 0 && (size_t)written < sizeof(g_json_path_input) - offset);
  if (written <= 0 || (size_t)written >= sizeof(g_json_path_input) - offset) return;
  g_json_path_input_len = offset + (size_t)written;
}

static DataBindValue *data_bind_json_path_bench_dom(void) {
  DataBindValue *value = NULL;
  if (data_bind_parse_json_path_all(g_json_path_codec, "Order", g_json_path_input,
                                    g_json_path_input_len, DATA_BIND_JSON_PATH_BENCH_PATH, &value,
                                    NULL) != DATA_BIND_OK) {
    g_json_path_failures++;
    return NULL;
  }
  return value;
}

static DataBindValue *data_bind_json_path_bench_stream(void) {
  DataBindValue *value = NULL;
  data_bind_stream_t *stream = data_bind_stream_json_path_all_create(
      g_json_path_codec, "Order", DATA_BIND_JSON_PATH_BENCH_PATH, &value, NULL);
  if (stream == NULL ||
      data_bind_stream_feed(stream, g_json_path_input, g_json_path_input_len) != DATA_BIND_OK ||
      data_bind_stream_finish(stream) != DATA_BIND_OK) {
    g_json_path_failures++;
    data_bind_stream_destroy(stream);
    data_bind_value_free(value);
    return NULL;
  }
  data_bind_stream_destroy(stream);
  return value;
}

static DataBindValue *data_bind_json_path_bench_dom_single(void) {
  DataBindValue *value = NULL;
  if (data_bind_parse_json_path(g_json_path_codec, "Order", g_json_path_input,
                                g_json_path_input_len, DATA_BIND_JSON_PATH_BENCH_SINGLE_PATH,
                                &value, NULL) != DATA_BIND_OK) {
    g_json_path_failures++;
    return NULL;
  }
  return value;
}

static DataBindValue *data_bind_json_path_bench_stream_single(void) {
  DataBindValue *value = NULL;
  data_bind_stream_t *stream = data_bind_stream_json_path_create(
      g_json_path_codec, "Order", DATA_BIND_JSON_PATH_BENCH_SINGLE_PATH, &value, NULL);
  if (stream == NULL ||
      data_bind_stream_feed(stream, g_json_path_input, g_json_path_input_len) != DATA_BIND_OK ||
      data_bind_stream_finish(stream) != DATA_BIND_OK) {
    g_json_path_failures++;
    data_bind_stream_destroy(stream);
    data_bind_value_free(value);
    return NULL;
  }
  data_bind_stream_destroy(stream);
  return value;
}

spec("DataBind JSONPath benchmarks") {
  before_all() {
    DataBindValue *dom;
    DataBindValue *streamed;
    data_bind_json_path_bench_prepare();
    dom = data_bind_json_path_bench_dom();
    streamed = data_bind_json_path_bench_stream();
    check_not_null(dom);
    check_not_null(streamed);
    check_size_eq(data_bind_value_count(dom), DATA_BIND_JSON_PATH_BENCH_RECORDS);
    check_size_eq(data_bind_value_count(streamed), DATA_BIND_JSON_PATH_BENCH_RECORDS);
    data_bind_value_free(streamed);
    data_bind_value_free(dom);
  }

  after_all() {
    data_bind_free(g_json_path_codec);
    g_json_path_codec = NULL;
  }

  bench("DOM and compiled streaming JSONPath") {
    benchmark_bytes("DOM sparse JSONPath + schema bind", DATA_BIND_JSON_PATH_BENCH_SAMPLES,
                    g_json_path_input_len) {
      DataBindValue *value = data_bind_json_path_bench_dom_single();
      if (value != NULL) g_json_path_sink += 1U;
      data_bind_value_free(value);
    }

    benchmark_bytes("compiled sparse stream + schema bind",
                    DATA_BIND_JSON_PATH_BENCH_SAMPLES, g_json_path_input_len) {
      DataBindValue *value = data_bind_json_path_bench_stream_single();
      if (value != NULL) g_json_path_sink += 1U;
      data_bind_value_free(value);
    }

    benchmark_bytes("DOM all-match + JSONPath + schema bind", DATA_BIND_JSON_PATH_BENCH_SAMPLES,
                    g_json_path_input_len) {
      DataBindValue *value = data_bind_json_path_bench_dom();
      if (value != NULL) g_json_path_sink += data_bind_value_count(value);
      data_bind_value_free(value);
    }

    benchmark_bytes("buffered all-match stream + schema bind",
                    DATA_BIND_JSON_PATH_BENCH_SAMPLES, g_json_path_input_len) {
      DataBindValue *value = data_bind_json_path_bench_stream();
      if (value != NULL) g_json_path_sink += data_bind_value_count(value);
      data_bind_value_free(value);
    }

    check_size_eq(g_json_path_failures, 0U);
    check_true(g_json_path_sink != 0U);
  }
}
