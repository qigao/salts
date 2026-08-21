/**
 * SQLite public-API reference for VDBE-backed B-tree and R-tree queries.
 * SQLite internals remain owned and updated by vcpkg; this file keeps only the
 * reproducible workload used to evaluate future TurboUtils index code.
 * Architecture reference: https://www.sqlite.org/arch.html
 */
#include "tinytest.h"

#include <sqlite3.h>
#include <sqlite3_vdbe.h>

#include <stddef.h>
#include <stdio.h>

enum {
  SQLITE_BENCH_GRID_WIDTH = 128,
  SQLITE_BENCH_RECT_COUNT = SQLITE_BENCH_GRID_WIDTH * SQLITE_BENCH_GRID_WIDTH,
  SQLITE_BENCH_QUERY_COUNT = 128,
  SQLITE_BENCH_WINDOW_SPAN = 8,
  SQLITE_BENCH_EXPECTED_OVERLAPS = (SQLITE_BENCH_WINDOW_SPAN + 1) * (SQLITE_BENCH_WINDOW_SPAN + 1),
  SQLITE_BENCH_SAMPLES = 40,
  SQLITE_BENCH_QUERIES_PER_SAMPLE = 128,
  SQLITE_BENCH_LOOKUPS_PER_SAMPLE = 4096,
  SQLITE_BENCH_LOOKUP_CURSOR = 0,
  SQLITE_BENCH_LOOKUP_KEY_REGISTER = 1,
  SQLITE_BENCH_LOOKUP_RESULT_REGISTER = 2,
  SQLITE_BENCH_LOOKUP_REGISTER_COUNT = 2,
  SQLITE_BENCH_LOOKUP_CURSOR_COUNT = 1,
  SQLITE_BENCH_RANGE_SPAN = 64,
  SQLITE_BENCH_RANGE_QUERIES_PER_SAMPLE = 128,
  SQLITE_BENCH_RANGE_LOWER_REGISTER = 1,
  SQLITE_BENCH_RANGE_UPPER_REGISTER = 2,
  SQLITE_BENCH_RANGE_RESULT_REGISTER = 3,
  SQLITE_BENCH_RANGE_REGISTER_COUNT = 3,
  SQLITE_BENCH_RANGE_CURSOR_COUNT = 1
};

typedef struct sqlite_bench_window {
  double min_x;
  double max_x;
  double min_y;
  double max_y;
} sqlite_bench_window_t;

static sqlite3 *g_sqlite_bench_db;
static sqlite3_stmt *g_sqlite_bench_btree_lookup;
static sqlite3_stmt *g_sqlite_bench_btree_range;
static sqlite3_vdbe_program *g_sqlite_bench_direct_btree_lookup;
static sqlite3_vdbe_program *g_sqlite_bench_direct_btree_range;
static sqlite3_stmt *g_sqlite_bench_btree_spatial;
static sqlite3_stmt *g_sqlite_bench_rtree_spatial;
static sqlite_bench_window_t g_sqlite_bench_windows[SQLITE_BENCH_QUERY_COUNT];
static volatile sqlite3_int64 g_sqlite_bench_sink;
static size_t g_sqlite_bench_failures;
static int g_sqlite_bench_ready;
static char g_sqlite_bench_error[256];

static int sqlite_bench_fail(const char *operation, int rc) {
  const char *detail = g_sqlite_bench_db ? sqlite3_errmsg(g_sqlite_bench_db) : "no database";
  (void)snprintf(g_sqlite_bench_error, sizeof(g_sqlite_bench_error), "%s: %s (%d)", operation,
                 detail, rc);
  return rc;
}

static int sqlite_bench_exec(const char *sql) {
  int rc = sqlite3_exec(g_sqlite_bench_db, sql, NULL, NULL, NULL);
  return rc == SQLITE_OK ? SQLITE_OK : sqlite_bench_fail("sqlite3_exec", rc);
}

static int sqlite_bench_prepare(const char *sql, sqlite3_stmt **statement) {
  int rc = sqlite3_prepare_v2(g_sqlite_bench_db, sql, -1, statement, NULL);
  return rc == SQLITE_OK ? SQLITE_OK : sqlite_bench_fail("sqlite3_prepare_v2", rc);
}

static int sqlite_bench_insert_rect(sqlite3_stmt *statement, sqlite3_int64 id, double min_x,
                                    double max_x, double min_y, double max_y) {
  int rc;
  if ((rc = sqlite3_bind_int64(statement, 1, id)) != SQLITE_OK ||
      (rc = sqlite3_bind_double(statement, 2, min_x)) != SQLITE_OK ||
      (rc = sqlite3_bind_double(statement, 3, max_x)) != SQLITE_OK ||
      (rc = sqlite3_bind_double(statement, 4, min_y)) != SQLITE_OK ||
      (rc = sqlite3_bind_double(statement, 5, max_y)) != SQLITE_OK) {
    return sqlite_bench_fail("bind rectangle", rc);
  }

  rc = sqlite3_step(statement);
  if (rc != SQLITE_DONE) return sqlite_bench_fail("insert rectangle", rc);
  rc = sqlite3_reset(statement);
  if (rc != SQLITE_OK) return sqlite_bench_fail("reset rectangle insert", rc);
  sqlite3_clear_bindings(statement);
  return SQLITE_OK;
}

static int sqlite_bench_direct_vdbe_btree(void) {
  sqlite3 *db = NULL;
  sqlite3_vdbe_program *program = NULL;
  sqlite3_int64 root_page = 0;
  int finalize_rc = SQLITE_OK;
  int rc = sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
  if (rc != SQLITE_OK) goto cleanup;

  program = sqlite3_vdbe_create(db);
  if (!program) {
    rc = sqlite3_errcode(db);
    goto cleanup;
  }
  if ((rc = sqlite3_vdbe_use_btree(program, 0)) != SQLITE_OK ||
      sqlite3_vdbe_add_op3(program, SQLITE_VDBE_OP_TRANSACTION, 0, 1, 0) < 0 ||
      sqlite3_vdbe_add_op3(program, SQLITE_VDBE_OP_CREATE_BTREE, 0, 1,
                           SQLITE_VDBE_BTREE_INTKEY) < 0 ||
      sqlite3_vdbe_add_op2(program, SQLITE_VDBE_OP_RESULT_ROW, 1, 1) < 0 ||
      sqlite3_vdbe_add_op0(program, SQLITE_VDBE_OP_HALT) < 0) {
    if (rc == SQLITE_OK) rc = sqlite3_errcode(db);
    if (rc == SQLITE_OK) rc = SQLITE_ERROR;
    goto cleanup;
  }
  if ((rc = sqlite3_vdbe_set_num_columns(program, 1)) != SQLITE_OK ||
      (rc = sqlite3_vdbe_make_ready(program, 1, 0)) != SQLITE_OK) {
    goto cleanup;
  }

  rc = sqlite3_vdbe_step(program);
  if (rc != SQLITE_ROW) goto cleanup;
  root_page = sqlite3_vdbe_column_int64(program, 0);
  rc = sqlite3_vdbe_step(program);
  if (rc == SQLITE_DONE && root_page > 0) rc = SQLITE_OK;
  else if (rc == SQLITE_DONE) rc = SQLITE_ERROR;

cleanup:
  if (program) finalize_rc = sqlite3_vdbe_finalize(program);
  if (db) {
    int close_rc = sqlite3_close(db);
    if (rc == SQLITE_OK && finalize_rc != SQLITE_OK) rc = finalize_rc;
    if (rc == SQLITE_OK && close_rc != SQLITE_OK) rc = close_rc;
  }
  return rc;
}

static void sqlite_bench_shutdown(void) {
  sqlite3_vdbe_finalize(g_sqlite_bench_direct_btree_lookup);
  sqlite3_vdbe_finalize(g_sqlite_bench_direct_btree_range);
  sqlite3_finalize(g_sqlite_bench_btree_lookup);
  sqlite3_finalize(g_sqlite_bench_btree_range);
  sqlite3_finalize(g_sqlite_bench_btree_spatial);
  sqlite3_finalize(g_sqlite_bench_rtree_spatial);
  g_sqlite_bench_direct_btree_lookup = NULL;
  g_sqlite_bench_direct_btree_range = NULL;
  g_sqlite_bench_btree_lookup = NULL;
  g_sqlite_bench_btree_range = NULL;
  g_sqlite_bench_btree_spatial = NULL;
  g_sqlite_bench_rtree_spatial = NULL;
  if (g_sqlite_bench_db) sqlite3_close(g_sqlite_bench_db);
  g_sqlite_bench_db = NULL;
  g_sqlite_bench_ready = 0;
}

static int sqlite_bench_build_direct_lookup(void) {
  int seek_address;
  int rc;
  g_sqlite_bench_direct_btree_lookup = sqlite3_vdbe_create(g_sqlite_bench_db);
  if (!g_sqlite_bench_direct_btree_lookup) return sqlite3_errcode(g_sqlite_bench_db);

  if (sqlite3_vdbe_add_op3(g_sqlite_bench_direct_btree_lookup, SQLITE_VDBE_OP_TRANSACTION,
                           0, 0, 0) < 0 ||
      sqlite3_vdbe_add_open_read_table(g_sqlite_bench_direct_btree_lookup,
                                       SQLITE_BENCH_LOOKUP_CURSOR, 0, "btree_rects") < 0) {
    return SQLITE_ERROR;
  }
  seek_address = sqlite3_vdbe_add_op3(g_sqlite_bench_direct_btree_lookup,
                                      SQLITE_VDBE_OP_SEEK_ROWID,
                                      SQLITE_BENCH_LOOKUP_CURSOR, 0,
                                      SQLITE_BENCH_LOOKUP_KEY_REGISTER);
  if (seek_address < 0 ||
      sqlite3_vdbe_add_op2(g_sqlite_bench_direct_btree_lookup, SQLITE_VDBE_OP_ROWID,
                           SQLITE_BENCH_LOOKUP_CURSOR,
                           SQLITE_BENCH_LOOKUP_RESULT_REGISTER) < 0 ||
      sqlite3_vdbe_add_op2(g_sqlite_bench_direct_btree_lookup, SQLITE_VDBE_OP_RESULT_ROW,
                           SQLITE_BENCH_LOOKUP_RESULT_REGISTER, 1) < 0 ||
      sqlite3_vdbe_jump_here(g_sqlite_bench_direct_btree_lookup, seek_address) != SQLITE_OK ||
      sqlite3_vdbe_add_op0(g_sqlite_bench_direct_btree_lookup, SQLITE_VDBE_OP_HALT) < 0) {
    return SQLITE_ERROR;
  }
  rc = sqlite3_vdbe_set_num_columns(g_sqlite_bench_direct_btree_lookup, 1);
  if (rc == SQLITE_OK) {
    rc = sqlite3_vdbe_make_ready(g_sqlite_bench_direct_btree_lookup,
                                 SQLITE_BENCH_LOOKUP_REGISTER_COUNT,
                                 SQLITE_BENCH_LOOKUP_CURSOR_COUNT);
  }
  return rc;
}

static int sqlite_bench_build_direct_range(void) {
  int seek_address;
  int loop_address;
  int upper_address;
  int rc;
  g_sqlite_bench_direct_btree_range = sqlite3_vdbe_create(g_sqlite_bench_db);
  if (!g_sqlite_bench_direct_btree_range) return sqlite3_errcode(g_sqlite_bench_db);

  if (sqlite3_vdbe_add_op3(g_sqlite_bench_direct_btree_range, SQLITE_VDBE_OP_TRANSACTION,
                           0, 0, 0) < 0 ||
      sqlite3_vdbe_add_open_read_table(g_sqlite_bench_direct_btree_range,
                                       SQLITE_BENCH_LOOKUP_CURSOR, 0, "btree_rects") < 0) {
    return SQLITE_ERROR;
  }
  seek_address = sqlite3_vdbe_add_op3(g_sqlite_bench_direct_btree_range,
                                      SQLITE_VDBE_OP_SEEK_GE,
                                      SQLITE_BENCH_LOOKUP_CURSOR, 0,
                                      SQLITE_BENCH_RANGE_LOWER_REGISTER);
  if (seek_address < 0) return SQLITE_ERROR;
  loop_address = sqlite3_vdbe_current_addr(g_sqlite_bench_direct_btree_range);
  if (loop_address < 0 ||
      sqlite3_vdbe_add_op2(g_sqlite_bench_direct_btree_range, SQLITE_VDBE_OP_ROWID,
                           SQLITE_BENCH_LOOKUP_CURSOR,
                           SQLITE_BENCH_RANGE_RESULT_REGISTER) < 0 ||
      (upper_address = sqlite3_vdbe_add_op3(g_sqlite_bench_direct_btree_range,
                                            SQLITE_VDBE_OP_GT,
                                            SQLITE_BENCH_RANGE_UPPER_REGISTER, 0,
                                            SQLITE_BENCH_RANGE_RESULT_REGISTER)) < 0 ||
      sqlite3_vdbe_add_op2(g_sqlite_bench_direct_btree_range, SQLITE_VDBE_OP_RESULT_ROW,
                           SQLITE_BENCH_RANGE_RESULT_REGISTER, 1) < 0) {
    return SQLITE_ERROR;
  }
  if (sqlite3_vdbe_add_op2(g_sqlite_bench_direct_btree_range, SQLITE_VDBE_OP_NEXT,
                           SQLITE_BENCH_LOOKUP_CURSOR, loop_address) < 0 ||
      sqlite3_vdbe_jump_here(g_sqlite_bench_direct_btree_range, seek_address) != SQLITE_OK ||
      sqlite3_vdbe_jump_here(g_sqlite_bench_direct_btree_range, upper_address) != SQLITE_OK ||
      sqlite3_vdbe_add_op0(g_sqlite_bench_direct_btree_range, SQLITE_VDBE_OP_HALT) < 0) {
    return SQLITE_ERROR;
  }
  rc = sqlite3_vdbe_set_num_columns(g_sqlite_bench_direct_btree_range, 1);
  if (rc == SQLITE_OK) {
    rc = sqlite3_vdbe_make_ready(g_sqlite_bench_direct_btree_range,
                                 SQLITE_BENCH_RANGE_REGISTER_COUNT,
                                 SQLITE_BENCH_RANGE_CURSOR_COUNT);
  }
  return rc;
}

static int sqlite_bench_setup(void) {
  sqlite3_stmt *btree_insert = NULL;
  sqlite3_stmt *rtree_insert = NULL;
  int rc;
  int x;
  int y;
  size_t i;

  rc = sqlite3_open_v2(":memory:", &g_sqlite_bench_db,
                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, NULL);
  if (rc != SQLITE_OK) return sqlite_bench_fail("sqlite3_open_v2", rc);

  if (!sqlite3_compileoption_used("ENABLE_RTREE")) {
    return sqlite_bench_fail("SQLite built without ENABLE_RTREE", SQLITE_ERROR);
  }

  rc = sqlite_bench_exec(
      "PRAGMA journal_mode=OFF;"
      "PRAGMA synchronous=OFF;"
      "PRAGMA temp_store=MEMORY;"
      "CREATE TABLE btree_rects("
      "id INTEGER PRIMARY KEY, min_x REAL NOT NULL, max_x REAL NOT NULL,"
      "min_y REAL NOT NULL, max_y REAL NOT NULL);"
      "CREATE INDEX btree_rect_bounds "
      "ON btree_rects(max_x, min_x, max_y, min_y);"
      "CREATE VIRTUAL TABLE rtree_rects USING rtree(id, min_x, max_x, min_y, max_y);"
      "BEGIN IMMEDIATE;");
  if (rc != SQLITE_OK) goto cleanup;

  rc = sqlite_bench_prepare("INSERT INTO btree_rects VALUES(?1, ?2, ?3, ?4, ?5)", &btree_insert);
  if (rc != SQLITE_OK) goto rollback;
  rc = sqlite_bench_prepare("INSERT INTO rtree_rects VALUES(?1, ?2, ?3, ?4, ?5)", &rtree_insert);
  if (rc != SQLITE_OK) goto rollback;

  for (y = 0; y < SQLITE_BENCH_GRID_WIDTH; ++y) {
    for (x = 0; x < SQLITE_BENCH_GRID_WIDTH; ++x) {
      sqlite3_int64 id = (sqlite3_int64)y * SQLITE_BENCH_GRID_WIDTH + x + 1;
      rc = sqlite_bench_insert_rect(btree_insert, id, (double)x, (double)x + 0.5, (double)y,
                                    (double)y + 0.5);
      if (rc != SQLITE_OK) goto rollback;
      rc = sqlite_bench_insert_rect(rtree_insert, id, (double)x, (double)x + 0.5, (double)y,
                                    (double)y + 0.5);
      if (rc != SQLITE_OK) goto rollback;
    }
  }

  rc = sqlite_bench_exec("COMMIT; PRAGMA query_only=ON;");
  if (rc != SQLITE_OK) goto cleanup;

  rc = sqlite_bench_prepare("SELECT id FROM btree_rects WHERE id=?1", &g_sqlite_bench_btree_lookup);
  if (rc != SQLITE_OK) goto cleanup;
  rc = sqlite_bench_prepare("SELECT id FROM btree_rects WHERE id>=?1 AND id<=?2",
                            &g_sqlite_bench_btree_range);
  if (rc != SQLITE_OK) goto cleanup;
  rc = sqlite_bench_build_direct_lookup();
  if (rc != SQLITE_OK) goto cleanup;
  rc = sqlite_bench_build_direct_range();
  if (rc != SQLITE_OK) goto cleanup;
  rc = sqlite_bench_prepare("SELECT count(*) FROM btree_rects INDEXED BY btree_rect_bounds "
                            "WHERE max_x>=?1 AND min_x<=?2 AND max_y>=?3 AND min_y<=?4",
                            &g_sqlite_bench_btree_spatial);
  if (rc != SQLITE_OK) goto cleanup;
  rc = sqlite_bench_prepare("SELECT count(*) FROM rtree_rects "
                            "WHERE max_x>=?1 AND min_x<=?2 AND max_y>=?3 AND min_y<=?4",
                            &g_sqlite_bench_rtree_spatial);
  if (rc != SQLITE_OK) goto cleanup;

  for (i = 0; i < SQLITE_BENCH_QUERY_COUNT; ++i) {
    int min_x = (int)(i % (SQLITE_BENCH_GRID_WIDTH - SQLITE_BENCH_WINDOW_SPAN));
    int min_y = (int)((i * 37U) % (SQLITE_BENCH_GRID_WIDTH - SQLITE_BENCH_WINDOW_SPAN));
    g_sqlite_bench_windows[i].min_x = (double)min_x;
    g_sqlite_bench_windows[i].max_x = (double)(min_x + SQLITE_BENCH_WINDOW_SPAN);
    g_sqlite_bench_windows[i].min_y = (double)min_y;
    g_sqlite_bench_windows[i].max_y = (double)(min_y + SQLITE_BENCH_WINDOW_SPAN);
  }

  sqlite3_finalize(btree_insert);
  sqlite3_finalize(rtree_insert);
  g_sqlite_bench_ready = 1;
  return SQLITE_OK;

rollback:
  (void)sqlite_bench_exec("ROLLBACK;");
cleanup:
  sqlite3_finalize(btree_insert);
  sqlite3_finalize(rtree_insert);
  sqlite_bench_shutdown();
  return rc;
}

static sqlite3_int64 sqlite_bench_lookup(sqlite3_int64 id) {
  sqlite3_int64 result = -1;
  int rc = sqlite3_bind_int64(g_sqlite_bench_btree_lookup, 1, id);
  if (rc == SQLITE_OK) rc = sqlite3_step(g_sqlite_bench_btree_lookup);
  if (rc == SQLITE_ROW) {
    result = sqlite3_column_int64(g_sqlite_bench_btree_lookup, 0);
  } else {
    ++g_sqlite_bench_failures;
  }
  if (sqlite3_reset(g_sqlite_bench_btree_lookup) != SQLITE_OK) ++g_sqlite_bench_failures;
  return result;
}

static sqlite3_int64 sqlite_bench_direct_lookup(sqlite3_int64 id) {
  sqlite3_int64 result = -1;
  int rc = sqlite3_vdbe_set_int64(g_sqlite_bench_direct_btree_lookup,
                                  SQLITE_BENCH_LOOKUP_KEY_REGISTER, id);
  if (rc == SQLITE_OK) rc = sqlite3_vdbe_step(g_sqlite_bench_direct_btree_lookup);
  if (rc == SQLITE_ROW) {
    result = sqlite3_vdbe_column_int64(g_sqlite_bench_direct_btree_lookup, 0);
  } else {
    ++g_sqlite_bench_failures;
  }
  if (sqlite3_vdbe_reset(g_sqlite_bench_direct_btree_lookup) != SQLITE_OK) {
    ++g_sqlite_bench_failures;
  }
  return result;
}

static sqlite3_int64 sqlite_bench_range_checksum(sqlite3_int64 lower, sqlite3_int64 upper) {
  sqlite3_int64 result = 0;
  int rc = sqlite3_bind_int64(g_sqlite_bench_btree_range, 1, lower);
  if (rc == SQLITE_OK) rc = sqlite3_bind_int64(g_sqlite_bench_btree_range, 2, upper);
  if (rc == SQLITE_OK) {
    while ((rc = sqlite3_step(g_sqlite_bench_btree_range)) == SQLITE_ROW) {
      result += sqlite3_column_int64(g_sqlite_bench_btree_range, 0);
    }
  }
  if (rc != SQLITE_DONE) ++g_sqlite_bench_failures;
  if (sqlite3_reset(g_sqlite_bench_btree_range) != SQLITE_OK) ++g_sqlite_bench_failures;
  sqlite3_clear_bindings(g_sqlite_bench_btree_range);
  return result;
}

static sqlite3_int64 sqlite_bench_direct_range_checksum(sqlite3_int64 lower,
                                                        sqlite3_int64 count) {
  sqlite3_int64 result = 0;
  sqlite3_int64 rows = 0;
  int rc = sqlite3_vdbe_set_int64(g_sqlite_bench_direct_btree_range,
                                  SQLITE_BENCH_RANGE_LOWER_REGISTER, lower);
  if (rc == SQLITE_OK) {
    rc = sqlite3_vdbe_set_int64(g_sqlite_bench_direct_btree_range,
                                SQLITE_BENCH_RANGE_UPPER_REGISTER, lower + count - 1);
  }
  if (rc == SQLITE_OK) {
    while ((rc = sqlite3_vdbe_step(g_sqlite_bench_direct_btree_range)) == SQLITE_ROW) {
      result += sqlite3_vdbe_column_int64(g_sqlite_bench_direct_btree_range, 0);
      ++rows;
    }
  }
  if (rc != SQLITE_DONE || rows != count) ++g_sqlite_bench_failures;
  if (sqlite3_vdbe_reset(g_sqlite_bench_direct_btree_range) != SQLITE_OK) {
    ++g_sqlite_bench_failures;
  }
  return result;
}

static sqlite3_int64 sqlite_bench_spatial_count(sqlite3_stmt *statement,
                                                const sqlite_bench_window_t *window) {
  sqlite3_int64 result = -1;
  int rc;
  if ((rc = sqlite3_bind_double(statement, 1, window->min_x)) == SQLITE_OK &&
      (rc = sqlite3_bind_double(statement, 2, window->max_x)) == SQLITE_OK &&
      (rc = sqlite3_bind_double(statement, 3, window->min_y)) == SQLITE_OK &&
      (rc = sqlite3_bind_double(statement, 4, window->max_y)) == SQLITE_OK) {
    rc = sqlite3_step(statement);
  }
  if (rc == SQLITE_ROW) {
    result = sqlite3_column_int64(statement, 0);
  } else {
    ++g_sqlite_bench_failures;
  }
  if (sqlite3_reset(statement) != SQLITE_OK) ++g_sqlite_bench_failures;
  sqlite3_clear_bindings(statement);
  return result;
}

spec("SQLite VDBE index reference benchmarks") {
  before_all() {
    int rc = sqlite_bench_setup();
    if (rc != SQLITE_OK) info("%s", g_sqlite_bench_error);
    check_equal(rc, SQLITE_OK);
  }

  after_all() { sqlite_bench_shutdown(); }

  it("executes B-tree bytecode without SQL preparation") {
    check_equal(sqlite_bench_direct_vdbe_btree(), SQLITE_OK);
  }

  it("returns equivalent B-tree and R-tree overlap results") {
    if (g_sqlite_bench_ready) {
      size_t mismatches = 0;
      size_t i;
      for (i = 0; i < SQLITE_BENCH_QUERY_COUNT; ++i) {
        sqlite3_int64 btree_count =
            sqlite_bench_spatial_count(g_sqlite_bench_btree_spatial, &g_sqlite_bench_windows[i]);
        sqlite3_int64 rtree_count =
            sqlite_bench_spatial_count(g_sqlite_bench_rtree_spatial, &g_sqlite_bench_windows[i]);
        if (btree_count != SQLITE_BENCH_EXPECTED_OVERLAPS || rtree_count != btree_count)
          ++mismatches;
      }
      check_equal(mismatches, 0U);
      check_equal(sqlite_bench_lookup(1), 1);
      check_equal(sqlite_bench_lookup(SQLITE_BENCH_RECT_COUNT), SQLITE_BENCH_RECT_COUNT);
      check_equal(sqlite_bench_direct_lookup(1), 1);
      check_equal(sqlite_bench_direct_lookup(SQLITE_BENCH_RECT_COUNT),
                    SQLITE_BENCH_RECT_COUNT);
      check_equal(sqlite_bench_range_checksum(100, 163),
                    (sqlite3_int64)(100 + 163) * SQLITE_BENCH_RANGE_SPAN / 2);
      check_equal(sqlite_bench_direct_range_checksum(100, SQLITE_BENCH_RANGE_SPAN),
                    (sqlite3_int64)(100 + 163) * SQLITE_BENCH_RANGE_SPAN / 2);
      check_equal(g_sqlite_bench_failures, 0U);
    }
  }

  /* A direct range scan uses one B-tree seek, then cursor Next for each row: O(log n + k). */
  bench("B-tree cursor range scan comparison") {
    if (g_sqlite_bench_ready) {
      size_t failures_before = g_sqlite_bench_failures;
      size_t query_index = 0;
      benchmark_ops("SQLite B-tree prepared range scan", SQLITE_BENCH_SAMPLES,
                    SQLITE_BENCH_RANGE_QUERIES_PER_SAMPLE * SQLITE_BENCH_RANGE_SPAN) {
        size_t i;
        for (i = 0; i < SQLITE_BENCH_RANGE_QUERIES_PER_SAMPLE; ++i) {
          sqlite3_int64 lower = (sqlite3_int64)(query_index %
                                                (SQLITE_BENCH_RECT_COUNT - SQLITE_BENCH_RANGE_SPAN + 1)) +
                                1;
          sqlite3_int64 upper = lower + SQLITE_BENCH_RANGE_SPAN - 1;
          g_sqlite_bench_sink ^= sqlite_bench_range_checksum(lower, upper);
          ++query_index;
        }
      }
      check_equal(g_sqlite_bench_failures, failures_before);

      failures_before = g_sqlite_bench_failures;
      query_index = 0;
      benchmark_ops("SQLite B-tree direct VDBE SeekGE+Next range scan", SQLITE_BENCH_SAMPLES,
                    SQLITE_BENCH_RANGE_QUERIES_PER_SAMPLE * SQLITE_BENCH_RANGE_SPAN) {
        size_t i;
        for (i = 0; i < SQLITE_BENCH_RANGE_QUERIES_PER_SAMPLE; ++i) {
          sqlite3_int64 lower = (sqlite3_int64)(query_index %
                                                (SQLITE_BENCH_RECT_COUNT - SQLITE_BENCH_RANGE_SPAN + 1)) +
                                1;
          g_sqlite_bench_sink ^= sqlite_bench_direct_range_checksum(
              lower, SQLITE_BENCH_RANGE_SPAN);
          ++query_index;
        }
      }
      check_equal(g_sqlite_bench_failures, failures_before);
    }
  }

  /* Primary-key B-tree lookup is O(log n); each timed operation reuses VDBE bytecode. */
  bench("B-tree primary-key lookup comparison") {
    if (g_sqlite_bench_ready) {
      size_t failures_before = g_sqlite_bench_failures;
      size_t query_index = 0;
      benchmark_ops("SQLite B-tree prepared lookup", SQLITE_BENCH_SAMPLES,
                    SQLITE_BENCH_LOOKUPS_PER_SAMPLE) {
        size_t i;
        for (i = 0; i < SQLITE_BENCH_LOOKUPS_PER_SAMPLE; ++i) {
          sqlite3_int64 id = (sqlite3_int64)(query_index % SQLITE_BENCH_RECT_COUNT) + 1;
          g_sqlite_bench_sink ^= sqlite_bench_lookup(id);
          ++query_index;
        }
      }
      check_equal(g_sqlite_bench_failures, failures_before);

      failures_before = g_sqlite_bench_failures;
      query_index = 0;
      benchmark_ops("SQLite B-tree direct VDBE lookup", SQLITE_BENCH_SAMPLES,
                    SQLITE_BENCH_LOOKUPS_PER_SAMPLE) {
        size_t i;
        for (i = 0; i < SQLITE_BENCH_LOOKUPS_PER_SAMPLE; ++i) {
          sqlite3_int64 id = (sqlite3_int64)(query_index % SQLITE_BENCH_RECT_COUNT) + 1;
          g_sqlite_bench_sink ^= sqlite_bench_direct_lookup(id);
          ++query_index;
        }
      }
      check_equal(g_sqlite_bench_failures, failures_before);
    }
  }

  /* A multi-range B-tree may scan candidates (worst O(n)); R-tree is expected O(log n + k). */
  bench("B-tree spatial overlap reference") {
    if (g_sqlite_bench_ready) {
      size_t failures_before = g_sqlite_bench_failures;
      size_t query_index = 0;
      benchmark_ops("SQLite B-tree rectangle overlap", SQLITE_BENCH_SAMPLES,
                    SQLITE_BENCH_QUERIES_PER_SAMPLE) {
        size_t i;
        for (i = 0; i < SQLITE_BENCH_QUERIES_PER_SAMPLE; ++i) {
          const sqlite_bench_window_t *window =
              &g_sqlite_bench_windows[query_index % SQLITE_BENCH_QUERY_COUNT];
          g_sqlite_bench_sink ^= sqlite_bench_spatial_count(g_sqlite_bench_btree_spatial, window);
          ++query_index;
        }
      }
      check_equal(g_sqlite_bench_failures, failures_before);
    }
  }

  bench("R-tree spatial overlap reference") {
    if (g_sqlite_bench_ready) {
      size_t failures_before = g_sqlite_bench_failures;
      size_t query_index = 0;
      benchmark_ops("SQLite R-tree rectangle overlap", SQLITE_BENCH_SAMPLES,
                    SQLITE_BENCH_QUERIES_PER_SAMPLE) {
        size_t i;
        for (i = 0; i < SQLITE_BENCH_QUERIES_PER_SAMPLE; ++i) {
          const sqlite_bench_window_t *window =
              &g_sqlite_bench_windows[query_index % SQLITE_BENCH_QUERY_COUNT];
          g_sqlite_bench_sink ^= sqlite_bench_spatial_count(g_sqlite_bench_rtree_spatial, window);
          ++query_index;
        }
      }
      check_equal(g_sqlite_bench_failures, failures_before);
    }
  }
}
