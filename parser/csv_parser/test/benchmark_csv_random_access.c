#include "csv_parser.h"
#include "tinytest.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
  BENCHMARK_ROWS = 100000,
  LOOKUPS_PER_RUN = 100000,
};

static volatile uint64_t g_sink;

static char *make_csv(size_t rows) {
  const size_t capacity = rows * 24 + 16;
  char *csv = (char *)malloc(capacity);
  size_t offset = 0;
  size_t row;

  if (!csv)
    return NULL;
  offset += (size_t)snprintf(csv + offset, capacity - offset, "id,value\n");
  for (row = 0; row < rows; ++row) {
    offset += (size_t)snprintf(csv + offset, capacity - offset, "%zu,%zu\n", row, row * 3);
  }
  return csv;
}

static uint64_t read_random_rows(const csv_doc_t *doc, size_t rows, size_t lookups) {
  unsigned int state = 0x9e3779b9U;
  uint64_t sum = 0;
  size_t i;

  for (i = 0; i < lookups; ++i) {
    state = state * 1664525U + 1013904223U;
    sum += (uint64_t)csv_get_int(doc, state % rows, 1, 0);
  }
  return sum;
}

suite("CSV random access benchmark") {
  static char *csv;
  static csv_doc_t *doc;

  before_all() {
    csv_options_t options = CSV_OPTIONS_DEFAULT;
    csv = make_csv(BENCHMARK_ROWS);
    check_not_null(csv);
    options.has_header = true;
    doc = csv_parse_opts(csv, strlen(csv), &options);
    check_not_null(doc);
    check_size_eq(csv_row_count(doc), BENCHMARK_ROWS);
  }

  after_all() {
    csv_free(doc);
    free(csv);
  }

  bench("indexed row lookup") {
    benchmark("100K pseudo-random rows", 3, LOOKUPS_PER_RUN) {
      g_sink = read_random_rows(doc, BENCHMARK_ROWS, LOOKUPS_PER_RUN);
    }
  }
}
