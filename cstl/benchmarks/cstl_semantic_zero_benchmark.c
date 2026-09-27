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

    benchmark_batch("Deque explicit init + first push + destroy",
                    CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
      deque_t deque = {0};
      if (deque_raw_init(&deque, &cmeta_type_int, SIZE_MAX) != STL_OK ||
          deque_push_back(&deque, &value) != STL_OK)
        ok = false;
      cstl_semantic_zero_bench_sink += deque_size(&deque);
      deque_raw_destroy_storage(&deque);
    }

    benchmark_batch("Deque semantic-zero first push + destroy",
                    CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
      deque_t deque =
          SALTS_STL_DEQUE_INITIALIZER_WITH_TYPE(int, &cmeta_type_int);
      if (deque_push_back(&deque, &value) != STL_OK) ok = false;
      cstl_semantic_zero_bench_sink += deque_size(&deque);
      deque_raw_destroy_storage(&deque);
    }

    {
      deque_t deque =
          SALTS_STL_DEQUE_INITIALIZER_WITH_TYPE(int, &cmeta_type_int);
      if (deque_push_back(&deque, &value) != STL_OK) ok = false;
      benchmark_batch("Deque materialized steady push + pop",
                      CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
        if (deque_push_back(&deque, &value) != STL_OK ||
            deque_pop_back(&deque, NULL) != STL_OK)
          ok = false;
        cstl_semantic_zero_bench_sink += deque_size(&deque);
      }
      deque_raw_destroy_storage(&deque);
    }

    benchmark_batch("Stack explicit init + first push + destroy",
                    CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
      cstl_stack_t stack = {0};
      if (stack_raw_init(&stack, &cmeta_type_int, SIZE_MAX) != STL_OK ||
          stack_push(&stack, &value) != STL_OK)
        ok = false;
      cstl_semantic_zero_bench_sink += stack_size(&stack);
      stack_destroy(&stack);
    }

    benchmark_batch("Stack semantic-zero first push + destroy",
                    CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
      cstl_stack_t stack =
          SALTS_STL_STACK_INITIALIZER_WITH_TYPE(int, &cmeta_type_int);
      if (stack_push(&stack, &value) != STL_OK) ok = false;
      cstl_semantic_zero_bench_sink += stack_size(&stack);
      stack_destroy(&stack);
    }

    {
      cstl_stack_t stack =
          SALTS_STL_STACK_INITIALIZER_WITH_TYPE(int, &cmeta_type_int);
      if (stack_push(&stack, &value) != STL_OK) ok = false;
      benchmark_batch("Stack materialized steady push + pop",
                      CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
        if (stack_push(&stack, &value) != STL_OK ||
            stack_pop(&stack, NULL) != STL_OK)
          ok = false;
        cstl_semantic_zero_bench_sink += stack_size(&stack);
      }
      stack_destroy(&stack);
    }

    benchmark_batch("Queue explicit init + first push + destroy",
                    CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
      queue_t queue = {0};
      if (queue_raw_init(&queue, &cmeta_type_int, SIZE_MAX) != STL_OK ||
          queue_push(&queue, &value) != STL_OK)
        ok = false;
      cstl_semantic_zero_bench_sink += queue_size(&queue);
      queue_destroy(&queue);
    }

    benchmark_batch("Queue semantic-zero first push + destroy",
                    CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
      queue_t queue =
          SALTS_STL_QUEUE_INITIALIZER_WITH_TYPE(int, &cmeta_type_int);
      if (queue_push(&queue, &value) != STL_OK) ok = false;
      cstl_semantic_zero_bench_sink += queue_size(&queue);
      queue_destroy(&queue);
    }

    {
      queue_t queue =
          SALTS_STL_QUEUE_INITIALIZER_WITH_TYPE(int, &cmeta_type_int);
      if (queue_push(&queue, &value) != STL_OK) ok = false;
      benchmark_batch("Queue materialized steady push + pop",
                      CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
        if (queue_push(&queue, &value) != STL_OK ||
            queue_pop(&queue, NULL) != STL_OK)
          ok = false;
        cstl_semantic_zero_bench_sink += queue_size(&queue);
      }
      queue_destroy(&queue);
    }

    benchmark_batch("Heap explicit init + first push + destroy",
                    CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
      heap_t heap = {0};
      if (heap_raw_init(&heap, &cmeta_type_int, SIZE_MAX) != STL_OK ||
          heap_push(&heap, &value) != STL_OK)
        ok = false;
      cstl_semantic_zero_bench_sink += heap_size(&heap);
      heap_raw_destroy_storage(&heap);
    }

    benchmark_batch("Heap semantic-zero first push + destroy",
                    CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
      heap_t heap =
          SALTS_STL_HEAP_INITIALIZER_WITH_TYPE(int, &cmeta_type_int);
      if (heap_push(&heap, &value) != STL_OK) ok = false;
      cstl_semantic_zero_bench_sink += heap_size(&heap);
      heap_raw_destroy_storage(&heap);
    }

    {
      heap_t heap =
          SALTS_STL_HEAP_INITIALIZER_WITH_TYPE(int, &cmeta_type_int);
      if (heap_push(&heap, &value) != STL_OK) ok = false;
      benchmark_batch("Heap materialized steady push + pop",
                      CSTL_SEMANTIC_ZERO_BENCH_SAMPLES) {
        if (heap_push(&heap, &value) != STL_OK ||
            heap_pop(&heap, NULL) != STL_OK)
          ok = false;
        cstl_semantic_zero_bench_sink += heap_size(&heap);
      }
      heap_raw_destroy_storage(&heap);
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
