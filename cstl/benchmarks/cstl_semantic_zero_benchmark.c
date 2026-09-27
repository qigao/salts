#include "tinytest.h"

#include <cstl.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CSTL_SEMANTIC_ZERO_BENCH_SAMPLES 5000u

static volatile size_t cstl_semantic_zero_bench_sink;

suite("CSTL semantic-zero materialization benchmarks") {
  bench("explicit init, first materialization, and steady mutation") {
    const int key0 = 0;
    const int key1 = 1;
    const int value = 7;
    const long mapped = 70L;
    bool ok = true;

    benchmark_batch("Vec explicit init + first push + destroy",
                    CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
      vec_t vec = {0};
      if (vec_raw_init(&vec, &cmeta_type_int, SIZE_MAX) != STL_OK ||
          vec_push(&vec, &value) != STL_OK)
        ok = false;
      cstl_semantic_zero_bench_sink += vec_size(&vec);
      vec_raw_destroy_storage(&vec);
    }

    benchmark_batch("Vec semantic-zero first push + destroy",
                    CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
      vec_t vec =
          SALTS_STL_VEC_INITIALIZER_WITH_TYPE(int, &cmeta_type_int);
      if (vec_push(&vec, &value) != STL_OK) ok = false;
      cstl_semantic_zero_bench_sink += vec_size(&vec);
      vec_raw_destroy_storage(&vec);
    }

    {
      vec_t vec =
          SALTS_STL_VEC_INITIALIZER_WITH_TYPE(int, &cmeta_type_int);
      if (vec_push(&vec, &value) != STL_OK) ok = false;
      benchmark_batch("Vec materialized steady push + pop",
                      CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
        if (vec_push(&vec, &value) != STL_OK ||
            vec_pop(&vec, NULL) != STL_OK)
          ok = false;
        cstl_semantic_zero_bench_sink += vec_size(&vec);
      }
      vec_raw_destroy_storage(&vec);
    }

    benchmark_batch("List explicit init + first push + destroy",
                    CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
      list_t list = {0};
      if (list_raw_init(&list, &cmeta_type_int, SIZE_MAX) != STL_OK ||
          list_push_back(&list, &value, NULL) != STL_OK)
        ok = false;
      cstl_semantic_zero_bench_sink += list_size(&list);
      list_raw_destroy_storage(&list);
    }

    benchmark_batch("List semantic-zero first push + destroy",
                    CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
      list_t list =
          SALTS_STL_LIST_INITIALIZER_WITH_TYPE(int, &cmeta_type_int);
      if (list_push_back(&list, &value, NULL) != STL_OK) ok = false;
      cstl_semantic_zero_bench_sink += list_size(&list);
      list_raw_destroy_storage(&list);
    }

    {
      list_t list =
          SALTS_STL_LIST_INITIALIZER_WITH_TYPE(int, &cmeta_type_int);
      if (list_push_back(&list, &value, NULL) != STL_OK) ok = false;
      benchmark_batch("List materialized steady push + pop",
                      CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
        if (list_push_back(&list, &value, NULL) != STL_OK ||
            list_pop_back(&list, NULL) != STL_OK)
          ok = false;
        cstl_semantic_zero_bench_sink += list_size(&list);
      }
      list_raw_destroy_storage(&list);
    }

    benchmark_batch("Set explicit init + first add + destroy",
                    CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
      set_t set = {0};
      if (set_raw_init(&set, &cmeta_type_int, SIZE_MAX) != STL_OK ||
          set_add(&set, &key1) != STL_OK)
        ok = false;
      cstl_semantic_zero_bench_sink += set_size(&set);
      set_raw_destroy_storage(&set);
    }

    benchmark_batch("Set semantic-zero first add + destroy",
                    CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
      set_t set =
          SALTS_STL_SET_INITIALIZER_WITH_TYPE(int, &cmeta_type_int);
      if (set_add(&set, &key1) != STL_OK) ok = false;
      cstl_semantic_zero_bench_sink += set_size(&set);
      set_raw_destroy_storage(&set);
    }

    {
      set_t set =
          SALTS_STL_SET_INITIALIZER_WITH_TYPE(int, &cmeta_type_int);
      if (set_add(&set, &key0) != STL_OK) ok = false;
      benchmark_batch("Set materialized steady add + remove",
                      CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
        if (set_add(&set, &key1) != STL_OK ||
            set_remove(&set, &key1) != STL_OK)
          ok = false;
        cstl_semantic_zero_bench_sink += set_size(&set);
      }
      set_raw_destroy_storage(&set);
    }

    benchmark_batch("Map explicit init + first put + destroy",
                    CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
      map_t map = {0};
      if (map_raw_init(&map, &cmeta_type_int, &cmeta_type_long,
                       SIZE_MAX) != STL_OK ||
          map_put(&map, &key1, &mapped) != STL_OK)
        ok = false;
      cstl_semantic_zero_bench_sink += map_size(&map);
      map_raw_destroy_storage(&map);
    }

    benchmark_batch("Map semantic-zero first put + destroy",
                    CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
      map_t map =
          SALTS_STL_MAP_INITIALIZER_WITH_TYPES(
              int, long, &cmeta_type_int, &cmeta_type_long);
      if (map_put(&map, &key1, &mapped) != STL_OK) ok = false;
      cstl_semantic_zero_bench_sink += map_size(&map);
      map_raw_destroy_storage(&map);
    }

    {
      map_t map =
          SALTS_STL_MAP_INITIALIZER_WITH_TYPES(
              int, long, &cmeta_type_int, &cmeta_type_long);
      if (map_put(&map, &key0, &mapped) != STL_OK) ok = false;
      benchmark_batch("Map materialized steady put + remove",
                      CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
        if (map_put(&map, &key1, &mapped) != STL_OK ||
            map_remove(&map, &key1, NULL) != STL_OK)
          ok = false;
        cstl_semantic_zero_bench_sink += map_size(&map);
      }
      map_raw_destroy_storage(&map);
    }

    check_true(ok);
    check_true(cstl_semantic_zero_bench_sink != 0u);
  }
}
