#include "tinytest.h"

#include <turbostl/deque.h>
#include <turbostl/hash_map.h>
#include <turbostl/heap.h>
#include <turbostl/vec.h>

#include <stdint.h>
#include <string.h>

#define TURBO_STL_BENCH_ITEMS 1024U

typedef struct turbostl_bench_heap_item {
  uint32_t priority;
  unsigned char payload[508];
} turbostl_bench_heap_item_t;

static volatile uintptr_t turbostl_bench_sink;

static int turbostl_bench_heap_compare(const void *left, const void *right,
                                        void *ctx) {
  const turbostl_bench_heap_item_t *a = (const turbostl_bench_heap_item_t *)left;
  const turbostl_bench_heap_item_t *b = (const turbostl_bench_heap_item_t *)right;
  (void)ctx;
  return (a->priority > b->priority) - (a->priority < b->priority);
}

static size_t turbostl_bench_hash_u64(const void *key, size_t key_size, void *ctx) {
  uint64_t value;
  (void)ctx;
  if (key_size != sizeof(value)) return 0U;
  memcpy(&value, key, sizeof(value));
  value ^= value >> 33U;
  value *= UINT64_C(0xff51afd7ed558ccd);
  value ^= value >> 33U;
  return (size_t)value;
}

static bool turbostl_bench_equal_u64(const void *left, const void *right,
                                      size_t key_size, void *ctx) {
  (void)ctx;
  return key_size == sizeof(uint64_t) &&
         memcmp(left, right, sizeof(uint64_t)) == 0;
}

suite("standard TurboSTL operation benchmarks") {
  bench("pre-reserved operation paths") {
    vec_t vec = {0};
    size_t i;
    int value = 0;
    check_equal(vec_init_bytes(&vec, sizeof(value), _Alignof(int),
                                     TURBO_STL_BENCH_ITEMS),
                STL_OK);
    check_equal(vec_reserve(&vec, TURBO_STL_BENCH_ITEMS), STL_OK);
    benchmark_ops("Vec push/pop 1024 ints", 1, TURBO_STL_BENCH_ITEMS * 2U) {
      for (i = 0; i < TURBO_STL_BENCH_ITEMS; ++i) {
        value = (int)i;
        (void)vec_push(&vec, &value);
      }
      while (vec_pop(&vec, &value) == STL_OK) {
        turbostl_bench_sink ^= (uintptr_t)value;
      }
    }
    check_equal(vec_size(&vec), 0U);
    vec_destroy(&vec);

    {
      deque_t deque = {0};
      check_equal(deque_init_bytes(&deque, sizeof(value), _Alignof(int),
                                         TURBO_STL_BENCH_ITEMS),
                  STL_OK);
      check_equal(deque_reserve(&deque, TURBO_STL_BENCH_ITEMS), STL_OK);
      for (i = 0; i < TURBO_STL_BENCH_ITEMS; ++i) {
        value = (int)i;
        check_equal(deque_push_back(&deque, &value), STL_OK);
      }
      benchmark_ops("Deque wrapped pop/push 1024 ints", 1,
                    TURBO_STL_BENCH_ITEMS * 2U) {
        for (i = 0; i < TURBO_STL_BENCH_ITEMS; ++i) {
          (void)deque_pop_front(&deque, &value);
          (void)deque_push_back(&deque, &value);
        }
      }
      check_equal(deque_size(&deque), TURBO_STL_BENCH_ITEMS);
      turbostl_bench_sink ^= (uintptr_t)*(const int *)deque_back_const(&deque);
      deque_destroy(&deque);
    }

    {
      heap_t heap = {0};
      turbostl_bench_heap_item_t item;
      turbostl_bench_heap_item_t out;
      memset(&item, 0x5a, sizeof(item));
      check_equal(heap_init_bytes(
                      &heap, sizeof(item), _Alignof(turbostl_bench_heap_item_t),
                      TURBO_STL_BENCH_ITEMS, turbostl_bench_heap_compare, NULL),
                  STL_OK);
      check_equal(heap_reserve(&heap, TURBO_STL_BENCH_ITEMS), STL_OK);
      benchmark_ops("Heap push/pop 1024 512-byte records", 1,
                    TURBO_STL_BENCH_ITEMS * 2U) {
        for (i = 0; i < TURBO_STL_BENCH_ITEMS; ++i) {
          item.priority = (uint32_t)(TURBO_STL_BENCH_ITEMS - i);
          (void)heap_push(&heap, &item);
        }
        while (heap_pop(&heap, &out) == STL_OK) {
          turbostl_bench_sink ^= out.priority;
        }
      }
      check_equal(heap_size(&heap), 0U);
      heap_destroy(&heap);
    }

    {
      hash_map_t map = {0};
      uint64_t key;
      uint64_t mapped;
      check_equal(hash_map_init_bytes(
                      &map, sizeof(key), _Alignof(uint64_t), sizeof(mapped),
                      _Alignof(uint64_t), TURBO_STL_BENCH_ITEMS,
                      turbostl_bench_hash_u64, turbostl_bench_equal_u64, NULL),
                  STL_OK);
      check_equal(hash_map_reserve(&map, TURBO_STL_BENCH_ITEMS), STL_OK);
      benchmark_ops("HashMap put/remove 1024 pairs", 1,
                    TURBO_STL_BENCH_ITEMS * 2U) {
        for (i = 0; i < TURBO_STL_BENCH_ITEMS; ++i) {
          key = (uint64_t)i;
          mapped = key * 2U;
          (void)hash_map_put(&map, &key, &mapped);
        }
        for (i = 0; i < TURBO_STL_BENCH_ITEMS; ++i) {
          key = (uint64_t)i;
          (void)hash_map_remove(&map, &key, &mapped);
          turbostl_bench_sink ^= (uintptr_t)mapped;
        }
      }
      check_equal(hash_map_size(&map), 0U);
      hash_map_destroy(&map);
    }
  }
}
