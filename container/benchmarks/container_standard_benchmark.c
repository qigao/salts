#include "tinytest.h"

#include <turbo/container/deque.h>
#include <turbo/container/hash_map.h>
#include <turbo/container/heap.h>
#include <turbo/container/vec.h>

#include <stdint.h>
#include <string.h>

#define CONTAINER_BENCH_ITEMS 1024U

typedef struct container_bench_heap_item {
  uint32_t priority;
  unsigned char payload[508];
} container_bench_heap_item_t;

static volatile uintptr_t container_bench_sink;

static int container_bench_heap_compare(const void *left, const void *right,
                                        void *ctx) {
  const container_bench_heap_item_t *a = (const container_bench_heap_item_t *)left;
  const container_bench_heap_item_t *b = (const container_bench_heap_item_t *)right;
  (void)ctx;
  return (a->priority > b->priority) - (a->priority < b->priority);
}

static size_t container_bench_hash_u64(const void *key, size_t key_size, void *ctx) {
  uint64_t value;
  (void)ctx;
  if (key_size != sizeof(value)) return 0U;
  memcpy(&value, key, sizeof(value));
  value ^= value >> 33U;
  value *= UINT64_C(0xff51afd7ed558ccd);
  value ^= value >> 33U;
  return (size_t)value;
}

static bool container_bench_equal_u64(const void *left, const void *right,
                                      size_t key_size, void *ctx) {
  (void)ctx;
  return key_size == sizeof(uint64_t) &&
         memcmp(left, right, sizeof(uint64_t)) == 0;
}

suite("standard Container operation benchmarks") {
  bench("pre-reserved operation paths") {
    turbo_vec_t vec = {0};
    size_t i;
    int value = 0;
    check_equal(turbo_vec_init_bytes(&vec, sizeof(value), _Alignof(int),
                                     CONTAINER_BENCH_ITEMS),
                CONTAINER_OK);
    check_equal(turbo_vec_reserve(&vec, CONTAINER_BENCH_ITEMS), CONTAINER_OK);
    benchmark_ops("Vec push/pop 1024 ints", 1, CONTAINER_BENCH_ITEMS * 2U) {
      for (i = 0; i < CONTAINER_BENCH_ITEMS; ++i) {
        value = (int)i;
        (void)turbo_vec_push(&vec, &value);
      }
      while (turbo_vec_pop(&vec, &value) == CONTAINER_OK) {
        container_bench_sink ^= (uintptr_t)value;
      }
    }
    check_equal(turbo_vec_size(&vec), 0U);
    turbo_vec_destroy(&vec);

    {
      turbo_deque_t deque = {0};
      check_equal(turbo_deque_init_bytes(&deque, sizeof(value), _Alignof(int),
                                         CONTAINER_BENCH_ITEMS),
                  CONTAINER_OK);
      check_equal(turbo_deque_reserve(&deque, CONTAINER_BENCH_ITEMS), CONTAINER_OK);
      for (i = 0; i < CONTAINER_BENCH_ITEMS; ++i) {
        value = (int)i;
        check_equal(turbo_deque_push_back(&deque, &value), CONTAINER_OK);
      }
      benchmark_ops("Deque wrapped pop/push 1024 ints", 1,
                    CONTAINER_BENCH_ITEMS * 2U) {
        for (i = 0; i < CONTAINER_BENCH_ITEMS; ++i) {
          (void)turbo_deque_pop_front(&deque, &value);
          (void)turbo_deque_push_back(&deque, &value);
        }
      }
      check_equal(turbo_deque_size(&deque), CONTAINER_BENCH_ITEMS);
      container_bench_sink ^= (uintptr_t)*(const int *)turbo_deque_back_const(&deque);
      turbo_deque_destroy(&deque);
    }

    {
      turbo_heap_t heap = {0};
      container_bench_heap_item_t item;
      container_bench_heap_item_t out;
      memset(&item, 0x5a, sizeof(item));
      check_equal(turbo_heap_init_bytes(
                      &heap, sizeof(item), _Alignof(container_bench_heap_item_t),
                      CONTAINER_BENCH_ITEMS, container_bench_heap_compare, NULL),
                  CONTAINER_OK);
      check_equal(turbo_heap_reserve(&heap, CONTAINER_BENCH_ITEMS), CONTAINER_OK);
      benchmark_ops("Heap push/pop 1024 512-byte records", 1,
                    CONTAINER_BENCH_ITEMS * 2U) {
        for (i = 0; i < CONTAINER_BENCH_ITEMS; ++i) {
          item.priority = (uint32_t)(CONTAINER_BENCH_ITEMS - i);
          (void)turbo_heap_push(&heap, &item);
        }
        while (turbo_heap_pop(&heap, &out) == CONTAINER_OK) {
          container_bench_sink ^= out.priority;
        }
      }
      check_equal(turbo_heap_size(&heap), 0U);
      turbo_heap_destroy(&heap);
    }

    {
      turbo_hash_map_t map = {0};
      uint64_t key;
      uint64_t mapped;
      check_equal(turbo_hash_map_init_bytes(
                      &map, sizeof(key), _Alignof(uint64_t), sizeof(mapped),
                      _Alignof(uint64_t), CONTAINER_BENCH_ITEMS,
                      container_bench_hash_u64, container_bench_equal_u64, NULL),
                  CONTAINER_OK);
      check_equal(turbo_hash_map_reserve(&map, CONTAINER_BENCH_ITEMS), CONTAINER_OK);
      benchmark_ops("HashMap put/remove 1024 pairs", 1,
                    CONTAINER_BENCH_ITEMS * 2U) {
        for (i = 0; i < CONTAINER_BENCH_ITEMS; ++i) {
          key = (uint64_t)i;
          mapped = key * 2U;
          (void)turbo_hash_map_put(&map, &key, &mapped);
        }
        for (i = 0; i < CONTAINER_BENCH_ITEMS; ++i) {
          key = (uint64_t)i;
          (void)turbo_hash_map_remove(&map, &key, &mapped);
          container_bench_sink ^= (uintptr_t)mapped;
        }
      }
      check_equal(turbo_hash_map_size(&map), 0U);
      turbo_hash_map_destroy(&map);
    }
  }
}
