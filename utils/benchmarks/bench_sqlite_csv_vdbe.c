/**
 * Direct SQLite VDBE CSV scan. No SQL text, parser, schema registration, or
 * prepared statement participates in this benchmark.
 */
#include "tinytest.h"

#include "csv_grammar_gen.h"
#include "csv_lexer.h"

#include <sqlite3.h>
#include <sqlite3_vdbe.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  DIRECT_CSV_ROWS = 32768,
  DIRECT_CSV_COLUMNS = 32,
  DIRECT_CSV_PROJECTED_COLUMNS = 4,
  DIRECT_CSV_BUFFER_CAPACITY = 8 * 1024 * 1024,
  DIRECT_CSV_COUNTRY_CAPACITY = 16,
  DIRECT_CSV_BENCHMARK_SAMPLES = 8,
  DIRECT_CSV_PLAN_COUNTRY_SCORE = 1,
  DIRECT_CSV_COL_ID = 0,
  DIRECT_CSV_COL_AGE = 1,
  DIRECT_CSV_COL_COUNTRY = 2,
  DIRECT_CSV_COL_SCORE = 3,
  DIRECT_CSV_REG_PLAN = 1,
  DIRECT_CSV_REG_ARGC = 2,
  DIRECT_CSV_REG_COUNTRY = 3,
  DIRECT_CSV_REG_SCORE = 4,
  DIRECT_CSV_REG_RESULT = 5,
  DIRECT_CSV_REGISTER_COUNT = 5,
  DIRECT_CSV_CURSOR_COUNT = 1,
  DIRECT_CSV_CURSOR = 0,
  DIRECT_CSV_MIN_SCORE = 90
};

typedef struct direct_csv_field {
  const char *data;
  size_t length;
  int needs_unescape;
} direct_csv_field_t;

typedef struct direct_csv_vtab {
  sqlite3_vtab base;
  const char *content;
  size_t content_length;
  size_t filter_calls;
  size_t column_calls;
} direct_csv_vtab_t;

typedef struct direct_csv_cursor {
  sqlite3_vtab_cursor base;
  csv_lexer_t lexer;
  direct_csv_field_t fields[DIRECT_CSV_PROJECTED_COLUMNS];
  sqlite3_int64 rowid;
  sqlite3_int64 minimum_score;
  char country[DIRECT_CSV_COUNTRY_CAPACITY];
  size_t country_length;
  int eof;
} direct_csv_cursor_t;

static sqlite3 *g_direct_csv_db;
static sqlite3_vdbe_program *g_direct_csv_program;
static direct_csv_vtab_t *g_direct_csv_vtab;
static int g_direct_csv_vtab_owned_by_program;
static char *g_direct_csv_content;
static size_t g_direct_csv_content_length;
static size_t g_direct_csv_expected_count;
static sqlite3_int64 g_direct_csv_expected_sum;
static volatile sqlite3_int64 g_direct_csv_sink;
static size_t g_direct_csv_failures;

static int direct_csv_disconnect(sqlite3_vtab *vtab);
static int direct_csv_open(sqlite3_vtab *vtab, sqlite3_vtab_cursor **cursor);
static int direct_csv_close(sqlite3_vtab_cursor *cursor);
static int direct_csv_filter(sqlite3_vtab_cursor *cursor, int idx_num, const char *idx_string,
                             int argc, sqlite3_value **argv);
static int direct_csv_next(sqlite3_vtab_cursor *cursor);
static int direct_csv_eof(sqlite3_vtab_cursor *cursor);
static int direct_csv_column(sqlite3_vtab_cursor *cursor, sqlite3_context *context, int column);
static int direct_csv_rowid(sqlite3_vtab_cursor *cursor, sqlite3_int64 *rowid);

static const sqlite3_module g_direct_csv_module = {
    .iVersion = 3,
    .xDisconnect = direct_csv_disconnect,
    .xOpen = direct_csv_open,
    .xClose = direct_csv_close,
    .xFilter = direct_csv_filter,
    .xNext = direct_csv_next,
    .xEof = direct_csv_eof,
    .xColumn = direct_csv_column,
    .xRowid = direct_csv_rowid,
};

static int direct_csv_set_error(direct_csv_cursor_t *cursor, const char *message) {
  sqlite3_vtab *vtab = cursor->base.pVtab;
  sqlite3_free(vtab->zErrMsg);
  vtab->zErrMsg = sqlite3_mprintf("direct CSV VDBE: %s", message);
  cursor->eof = 1;
  return vtab->zErrMsg ? SQLITE_ERROR : SQLITE_NOMEM;
}

static int direct_csv_parse_int64(const direct_csv_field_t *field, sqlite3_int64 *value) {
  uint64_t magnitude = 0;
  uint64_t limit = (uint64_t)INT64_MAX;
  size_t index = 0;
  int negative = 0;

  if (!field || !value || field->length == 0) return SQLITE_MISMATCH;
  if (field->data[index] == '-' || field->data[index] == '+') {
    negative = field->data[index] == '-';
    ++index;
  }
  if (index == field->length) return SQLITE_MISMATCH;
  if (negative) ++limit;

  for (; index < field->length; ++index) {
    unsigned int digit;
    unsigned char ch = (unsigned char)field->data[index];
    if (ch < '0' || ch > '9') return SQLITE_MISMATCH;
    digit = (unsigned int)(ch - '0');
    if (magnitude > (limit - digit) / 10U) return SQLITE_RANGE;
    magnitude = magnitude * 10U + digit;
  }

  if (negative) {
    *value = magnitude == (uint64_t)INT64_MAX + 1U
                 ? INT64_MIN
                 : -(sqlite3_int64)magnitude;
  } else {
    *value = (sqlite3_int64)magnitude;
  }
  return SQLITE_OK;
}

static int direct_csv_field_equals(const direct_csv_field_t *field, const char *value,
                                   size_t value_length) {
  size_t source_index = 0;
  size_t value_index = 0;
  if (!field || !value) return 0;
  while (source_index < field->length && value_index < value_length) {
    char ch = field->data[source_index++];
    if (field->needs_unescape && ch == '"' && source_index < field->length &&
        field->data[source_index] == '"') {
      ++source_index;
    }
    if (ch != value[value_index++]) return 0;
  }
  return source_index == field->length && value_index == value_length;
}

/* O(bytes in one logical record), O(1) retained row memory. */
static int direct_csv_read_row(direct_csv_cursor_t *cursor) {
  csv_token_t token;
  size_t column = 0;
  int has_field = 0;
  int lexer_rc;

  memset(cursor->fields, 0, sizeof(cursor->fields));
  while ((lexer_rc = csv_lexer_next(&cursor->lexer, &token)) > 0) {
    if (token.type == CSV_TOKEN_FIELD) {
      if (column < DIRECT_CSV_PROJECTED_COLUMNS) {
        cursor->fields[column].data = token.value;
        cursor->fields[column].length = token.length;
        cursor->fields[column].needs_unescape = token.needs_unescape;
      }
      ++column;
      has_field = 1;
    } else if (token.type == CSV_TOKEN_NEWLINE && has_field) {
      break;
    }
  }

  if (lexer_rc < 0) return direct_csv_set_error(cursor, cursor->lexer.error);
  if (!has_field) {
    cursor->eof = 1;
    return SQLITE_OK;
  }
  if (column < DIRECT_CSV_PROJECTED_COLUMNS) {
    return direct_csv_set_error(cursor, "record has fewer than four columns");
  }
  ++cursor->rowid;
  return SQLITE_ROW;
}

static int direct_csv_advance_to_match(direct_csv_cursor_t *cursor) {
  int rc;
  while ((rc = direct_csv_read_row(cursor)) == SQLITE_ROW) {
    sqlite3_int64 score;
    rc = direct_csv_parse_int64(&cursor->fields[DIRECT_CSV_COL_SCORE], &score);
    if (rc != SQLITE_OK) return direct_csv_set_error(cursor, "score is not a valid integer");
    if (score > cursor->minimum_score &&
        direct_csv_field_equals(&cursor->fields[DIRECT_CSV_COL_COUNTRY], cursor->country,
                                cursor->country_length)) {
      cursor->eof = 0;
      return SQLITE_OK;
    }
  }
  return rc == SQLITE_OK ? SQLITE_OK : rc;
}

static int direct_csv_disconnect(sqlite3_vtab *vtab) {
  sqlite3_free(vtab);
  return SQLITE_OK;
}

static int direct_csv_open(sqlite3_vtab *vtab, sqlite3_vtab_cursor **out_cursor) {
  direct_csv_cursor_t *cursor;
  if (!vtab || !out_cursor) return SQLITE_MISUSE;
  cursor = (direct_csv_cursor_t *)sqlite3_malloc64(sizeof(*cursor));
  if (!cursor) return SQLITE_NOMEM;
  memset(cursor, 0, sizeof(*cursor));
  cursor->base.pVtab = vtab;
  cursor->rowid = -1;
  *out_cursor = &cursor->base;
  return SQLITE_OK;
}

static int direct_csv_close(sqlite3_vtab_cursor *cursor) {
  sqlite3_free(cursor);
  return SQLITE_OK;
}

static int direct_csv_filter(sqlite3_vtab_cursor *base, int idx_num, const char *idx_string,
                             int argc, sqlite3_value **argv) {
  direct_csv_cursor_t *cursor = (direct_csv_cursor_t *)base;
  direct_csv_vtab_t *vtab = (direct_csv_vtab_t *)base->pVtab;
  const unsigned char *country;
  int country_bytes;
  (void)idx_string;

  if (idx_num != DIRECT_CSV_PLAN_COUNTRY_SCORE || argc != 2) {
    return direct_csv_set_error(cursor, "unsupported direct query plan");
  }
  country = sqlite3_value_text(argv[0]);
  country_bytes = sqlite3_value_bytes(argv[0]);
  if (!country || country_bytes < 0 || country_bytes >= DIRECT_CSV_COUNTRY_CAPACITY) {
    return direct_csv_set_error(cursor, "country predicate is too long");
  }

  memcpy(cursor->country, country, (size_t)country_bytes);
  cursor->country[country_bytes] = '\0';
  cursor->country_length = (size_t)country_bytes;
  cursor->minimum_score = sqlite3_value_int64(argv[1]);
  cursor->rowid = -1;
  cursor->eof = 0;
  csv_lexer_init(&cursor->lexer, vtab->content, vtab->content_length);
  csv_lexer_reset_state();
  ++vtab->filter_calls;
  return direct_csv_advance_to_match(cursor);
}

static int direct_csv_next(sqlite3_vtab_cursor *base) {
  return direct_csv_advance_to_match((direct_csv_cursor_t *)base);
}

static int direct_csv_eof(sqlite3_vtab_cursor *base) {
  return ((direct_csv_cursor_t *)base)->eof;
}

static void direct_csv_result_text(sqlite3_context *context, const direct_csv_field_t *field) {
  char *decoded;
  size_t source_index;
  size_t output_index = 0;

  if (!field->needs_unescape) {
    sqlite3_result_text64(context, field->data, (sqlite3_uint64)field->length, SQLITE_STATIC,
                          SQLITE_UTF8);
    return;
  }
  if (field->length == SIZE_MAX) {
    sqlite3_result_error_toobig(context);
    return;
  }
  decoded = (char *)sqlite3_malloc64((sqlite3_uint64)field->length + 1U);
  if (!decoded) {
    sqlite3_result_error_nomem(context);
    return;
  }
  for (source_index = 0; source_index < field->length; ++source_index) {
    decoded[output_index++] = field->data[source_index];
    if (field->data[source_index] == '"' && source_index + 1 < field->length &&
        field->data[source_index + 1] == '"') {
      ++source_index;
    }
  }
  decoded[output_index] = '\0';
  sqlite3_result_text64(context, decoded, (sqlite3_uint64)output_index, sqlite3_free, SQLITE_UTF8);
}

static int direct_csv_column(sqlite3_vtab_cursor *base, sqlite3_context *context, int column) {
  direct_csv_cursor_t *cursor = (direct_csv_cursor_t *)base;
  direct_csv_vtab_t *vtab = (direct_csv_vtab_t *)base->pVtab;
  sqlite3_int64 value;
  int rc;

  if (column < 0 || column >= DIRECT_CSV_PROJECTED_COLUMNS) {
    sqlite3_result_null(context);
    return SQLITE_OK;
  }
  ++vtab->column_calls;
  if (column == DIRECT_CSV_COL_COUNTRY) {
    direct_csv_result_text(context, &cursor->fields[column]);
    return SQLITE_OK;
  }
  rc = direct_csv_parse_int64(&cursor->fields[column], &value);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, "direct CSV VDBE: integer conversion failed", -1);
    return rc;
  }
  sqlite3_result_int64(context, value);
  return SQLITE_OK;
}

static int direct_csv_rowid(sqlite3_vtab_cursor *base, sqlite3_int64 *rowid) {
  if (!rowid) return SQLITE_MISUSE;
  *rowid = ((direct_csv_cursor_t *)base)->rowid;
  return SQLITE_OK;
}

static int direct_csv_generate_data(void) {
  size_t used = 0;
  int row;
  g_direct_csv_content = (char *)malloc(DIRECT_CSV_BUFFER_CAPACITY);
  if (!g_direct_csv_content) return SQLITE_NOMEM;

  for (row = 0; row < DIRECT_CSV_ROWS; ++row) {
    const char *country = (row & 1) == 0 ? "CN" : "US";
    int age = 18 + row % 50;
    int score = 80 + row % 21;
    int written;
    int column;
    if (used >= DIRECT_CSV_BUFFER_CAPACITY) return SQLITE_TOOBIG;
    written = snprintf(g_direct_csv_content + used, DIRECT_CSV_BUFFER_CAPACITY - used,
                       "%d,%d,%s,%d", row + 1, age, country, score);
    if (written < 0 || (size_t)written >= DIRECT_CSV_BUFFER_CAPACITY - used) {
      return SQLITE_TOOBIG;
    }
    used += (size_t)written;
    for (column = DIRECT_CSV_PROJECTED_COLUMNS; column < DIRECT_CSV_COLUMNS; ++column) {
      if (DIRECT_CSV_BUFFER_CAPACITY - used < 2U) return SQLITE_TOOBIG;
      g_direct_csv_content[used++] = ',';
      g_direct_csv_content[used++] = 'x';
    }
    if (DIRECT_CSV_BUFFER_CAPACITY - used < 1U) return SQLITE_TOOBIG;
    g_direct_csv_content[used++] = '\n';
    if (strcmp(country, "CN") == 0 && score > DIRECT_CSV_MIN_SCORE) {
      ++g_direct_csv_expected_count;
      g_direct_csv_expected_sum += age;
    }
  }
  g_direct_csv_content_length = used;
  return SQLITE_OK;
}

static int direct_csv_build_program(void) {
  int vopen_address;
  int filter_address;
  int loop_address;
  int rc;

  g_direct_csv_vtab = (direct_csv_vtab_t *)sqlite3_malloc64(sizeof(*g_direct_csv_vtab));
  if (!g_direct_csv_vtab) return SQLITE_NOMEM;
  memset(g_direct_csv_vtab, 0, sizeof(*g_direct_csv_vtab));
  g_direct_csv_vtab->base.pModule = &g_direct_csv_module;
  g_direct_csv_vtab->content = g_direct_csv_content;
  g_direct_csv_vtab->content_length = g_direct_csv_content_length;

  g_direct_csv_program = sqlite3_vdbe_create(g_direct_csv_db);
  if (!g_direct_csv_program) return sqlite3_errcode(g_direct_csv_db);
  rc = sqlite3_vdbe_use_btree(g_direct_csv_program, 0);
  if (rc != SQLITE_OK) return rc;
  if (sqlite3_vdbe_add_op3(g_direct_csv_program, SQLITE_VDBE_OP_TRANSACTION, 0, 0, 0) < 0) {
    return SQLITE_ERROR;
  }
  vopen_address = sqlite3_vdbe_add_vopen(g_direct_csv_program, DIRECT_CSV_CURSOR,
                                         &g_direct_csv_vtab->base);
  if (vopen_address < 0) return SQLITE_ERROR;
  g_direct_csv_vtab_owned_by_program = 1;

  /* VFilter requires the argc OP_Integer to be immediately before it. */
  if (sqlite3_vdbe_add_text(g_direct_csv_program, DIRECT_CSV_REG_COUNTRY, "CN") < 0 ||
      sqlite3_vdbe_add_op2(g_direct_csv_program, SQLITE_VDBE_OP_INTEGER,
                           DIRECT_CSV_MIN_SCORE, DIRECT_CSV_REG_SCORE) < 0 ||
      sqlite3_vdbe_add_op2(g_direct_csv_program, SQLITE_VDBE_OP_INTEGER,
                           DIRECT_CSV_PLAN_COUNTRY_SCORE, DIRECT_CSV_REG_PLAN) < 0 ||
      sqlite3_vdbe_add_op2(g_direct_csv_program, SQLITE_VDBE_OP_INTEGER, 2,
                           DIRECT_CSV_REG_ARGC) < 0) {
    return SQLITE_ERROR;
  }
  filter_address = sqlite3_vdbe_add_vfilter(g_direct_csv_program, DIRECT_CSV_CURSOR, 0,
                                            DIRECT_CSV_REG_PLAN, "country-score");
  if (filter_address < 0) return SQLITE_ERROR;
  loop_address = sqlite3_vdbe_current_addr(g_direct_csv_program);
  if (loop_address < 0 ||
      sqlite3_vdbe_add_op3(g_direct_csv_program, SQLITE_VDBE_OP_VCOLUMN, DIRECT_CSV_CURSOR,
                           DIRECT_CSV_COL_AGE, DIRECT_CSV_REG_RESULT) < 0 ||
      sqlite3_vdbe_add_op2(g_direct_csv_program, SQLITE_VDBE_OP_RESULT_ROW,
                           DIRECT_CSV_REG_RESULT, 1) < 0 ||
      sqlite3_vdbe_add_op2(g_direct_csv_program, SQLITE_VDBE_OP_VNEXT, DIRECT_CSV_CURSOR,
                           loop_address) < 0 ||
      sqlite3_vdbe_jump_here(g_direct_csv_program, filter_address) != SQLITE_OK ||
      sqlite3_vdbe_add_op0(g_direct_csv_program, SQLITE_VDBE_OP_HALT) < 0) {
    return SQLITE_ERROR;
  }
  rc = sqlite3_vdbe_set_num_columns(g_direct_csv_program, 1);
  if (rc == SQLITE_OK) {
    rc = sqlite3_vdbe_make_ready(g_direct_csv_program, DIRECT_CSV_REGISTER_COUNT,
                                 DIRECT_CSV_CURSOR_COUNT);
  }
  return rc;
}

static int direct_csv_execute(size_t *row_count, sqlite3_int64 *sum) {
  int rc;
  size_t count = 0;
  sqlite3_int64 total = 0;
  while ((rc = sqlite3_vdbe_step(g_direct_csv_program)) == SQLITE_ROW) {
    total += sqlite3_vdbe_column_int64(g_direct_csv_program, 0);
    ++count;
  }
  if (row_count) *row_count = count;
  if (sum) *sum = total;
  return rc;
}

static int direct_csv_rewind_and_execute(void) {
  size_t count = 0;
  sqlite3_int64 sum = 0;
  int rc = sqlite3_vdbe_reset(g_direct_csv_program);
  if (rc == SQLITE_OK) rc = direct_csv_execute(&count, &sum);
  if (rc != SQLITE_DONE || count != g_direct_csv_expected_count ||
      sum != g_direct_csv_expected_sum) {
    ++g_direct_csv_failures;
    return rc == SQLITE_DONE ? SQLITE_ERROR : rc;
  }
  g_direct_csv_sink ^= sum;
  return SQLITE_OK;
}

static int direct_csv_setup(void) {
  int rc = direct_csv_generate_data();
  if (rc != SQLITE_OK) return rc;
  rc = sqlite3_open_v2(":memory:", &g_direct_csv_db,
                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, NULL);
  if (rc != SQLITE_OK) return rc;
  return direct_csv_build_program();
}

static void direct_csv_shutdown(void) {
  sqlite3_vdbe_finalize(g_direct_csv_program);
  g_direct_csv_program = NULL;
  if (g_direct_csv_vtab && !g_direct_csv_vtab_owned_by_program) {
    direct_csv_disconnect(&g_direct_csv_vtab->base);
  }
  g_direct_csv_vtab = NULL;
  g_direct_csv_vtab_owned_by_program = 0;
  if (g_direct_csv_db) sqlite3_close(g_direct_csv_db);
  g_direct_csv_db = NULL;
  free(g_direct_csv_content);
  g_direct_csv_content = NULL;
}

spec("direct SQLite VDBE CSV scan") {
  before_all() { check_int_eq(direct_csv_setup(), SQLITE_OK); }

  after_all() { direct_csv_shutdown(); }

  it("filters and projects CSV without SQL preparation") {
    size_t count = 0;
    sqlite3_int64 sum = 0;
    size_t columns_before = g_direct_csv_vtab ? g_direct_csv_vtab->column_calls : 0;
    if (g_direct_csv_program) {
      check_int_eq(direct_csv_execute(&count, &sum), SQLITE_DONE);
      check_size_eq(count, g_direct_csv_expected_count);
      check_long_eq(sum, g_direct_csv_expected_sum);
      check_size_eq(g_direct_csv_vtab->filter_calls, 1U);
      check_size_eq(g_direct_csv_vtab->column_calls - columns_before,
                    g_direct_csv_expected_count);
    }
  }

  bench("streaming predicate pushdown") {
    if (g_direct_csv_program) {
      size_t failures_before = g_direct_csv_failures;
      benchmark_bytes("direct VDBE CSV country/score filter", DIRECT_CSV_BENCHMARK_SAMPLES,
                      g_direct_csv_content_length) {
        (void)direct_csv_rewind_and_execute();
      }
      check_size_eq(g_direct_csv_failures, failures_before);
    }
  }
}
