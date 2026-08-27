#define TINYTEST_NO_MAIN
#include "tinytest.h"
#include <cflow/cflow.h>

#include <stdint.h>
#include <string.h>

enum {
  CFLOW_RANGE_BENCH_MAX_ITEMS = 4096u,
  CFLOW_RANGE_BENCH_SMALL_ITEMS = 16u,
  CFLOW_RANGE_BENCH_MEDIUM_ITEMS = 256u,
  CFLOW_RANGE_BENCH_SMALL_SAMPLES = 20000u,
  CFLOW_RANGE_BENCH_MEDIUM_SAMPLES = 2000u,
  CFLOW_RANGE_BENCH_LARGE_SAMPLES = 200u
};

typedef struct cflow_range_bench_owner {
  const int *values;
  size_t count;
} cflow_range_bench_owner;

typedef struct cflow_range_bench_fixture {
  cflow_range_bench_owner owner;
  cflow_stream unsized;
  cflow_stream sized;
} cflow_range_bench_fixture;

static volatile uintptr_t cflow_range_bench_sink;

static size_t cflow_range_bench_size(const void *object) {
  const cflow_range_bench_owner *owner = (const cflow_range_bench_owner *)object;
  return owner->count;
}

static cmeta_gen_status cflow_range_bench_next(const void *object,
                                               cmeta_range_cursor *cursor,
                                               void *out_value) {
  const cflow_range_bench_owner *owner = (const cflow_range_bench_owner *)object;
  if (cursor->index >= owner->count) return CMETA_GEN_DONE;
  *(int *)out_value = owner->values[cursor->index++];
  return cursor->index == owner->count ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE;
}

static bool cflow_range_bench_fixture_init(cflow_range_bench_fixture *fixture,
                                           const int *values, size_t count) {
  cmeta_range unsized_range;
  cmeta_range sized_range;

  if (!fixture || !values || count == 0u) return false;
  memset(fixture, 0, sizeof(*fixture));
  fixture->owner.values = values;
  fixture->owner.count = count;
  unsized_range = (cmeta_range){
      &fixture->owner, &cmeta_type_int, CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE,
      NULL, cflow_range_bench_next, 0u, NULL};
  sized_range = (cmeta_range){
      &fixture->owner, &cmeta_type_int,
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE,
      cflow_range_bench_size, cflow_range_bench_next, 0u, NULL};
  if (!cflow_stream_from_range(&fixture->unsized, unsized_range)) return false;
  if (!cflow_stream_from_range(&fixture->sized, sized_range)) {
    cflow_stream_destroy(&fixture->unsized);
    return false;
  }
  return true;
}

static void cflow_range_bench_fixture_destroy(cflow_range_bench_fixture *fixture) {
  if (!fixture) return;
  cflow_stream_destroy(&fixture->sized);
  cflow_stream_destroy(&fixture->unsized);
}

static bool cflow_range_bench_eval(const cflow_stream *stream,
                                   size_t expected_count) {
  cflow_result result = {0};
  bool ok = cflow_eval_stream(stream, &result);
  if (ok && result.count == expected_count)
    cflow_range_bench_sink ^= (uintptr_t)((const int *)result.data)[result.count - 1u];
  else
    ok = false;
  cflow_result_destroy(&result);
  return ok;
}

#define CFLOW_RANGE_BENCH_PAIR(label, sample_count, item_count)                    \
  do {                                                                            \
    cflow_range_bench_fixture fixture;                                             \
    bool unsized_ok = false;                                                       \
    bool sized_ok = false;                                                         \
    check_true(cflow_range_bench_fixture_init(&fixture, values, (item_count)));     \
    check_true(cflow_range_bench_eval(&fixture.unsized, (item_count)));             \
    benchmark_ops("Range result / geometric growth / " label, (sample_count),       \
                  (item_count)) {                                                  \
      unsized_ok = cflow_range_bench_eval(&fixture.unsized, (item_count));          \
    }                                                                              \
    check_true(unsized_ok);                                                        \
    benchmark_ops("Range result / SIZED hint / " label, (sample_count),             \
                  (item_count)) {                                                  \
      sized_ok = cflow_range_bench_eval(&fixture.sized, (item_count));              \
    }                                                                              \
    check_true(sized_ok);                                                          \
    cflow_range_bench_fixture_destroy(&fixture);                                   \
  } while (0)

suite("CFlow Range collection benchmarks") {
  bench("compares SIZED capacity hints with geometric growth") {
    int values[CFLOW_RANGE_BENCH_MAX_ITEMS];
    size_t index;

    for (index = 0u; index < CFLOW_RANGE_BENCH_MAX_ITEMS; ++index)
      values[index] = (int)index;

    CFLOW_RANGE_BENCH_PAIR("16 items", CFLOW_RANGE_BENCH_SMALL_SAMPLES,
                           CFLOW_RANGE_BENCH_SMALL_ITEMS);
    CFLOW_RANGE_BENCH_PAIR("256 items", CFLOW_RANGE_BENCH_MEDIUM_SAMPLES,
                           CFLOW_RANGE_BENCH_MEDIUM_ITEMS);
    CFLOW_RANGE_BENCH_PAIR("4096 items", CFLOW_RANGE_BENCH_LARGE_SAMPLES,
                           CFLOW_RANGE_BENCH_MAX_ITEMS);
  }
}

#undef CFLOW_RANGE_BENCH_PAIR
