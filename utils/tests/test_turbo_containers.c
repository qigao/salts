#include "turbo_containers.h"
#include "tinytest.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

TURBO_VEC_DEFINE(int_vec_t, int)
TURBO_HASH_MAP_DEFINE(u64_int_map_t, uint64_t, int)
TURBO_MAP_DEFINE(u64_int_map_alias_t, uint64_t, int)
TURBO_SET_DEFINE(u64_set_t, uint64_t)
TURBO_DEQUE_DEFINE(int_deque_t, int)
TURBO_LIST_DEFINE(int_list_t, int)
TURBO_MULTI_MAP_DEFINE(u64_multi_map_t, uint64_t, int)

static int int_min_compare(const void *left, const void *right, void *ctx) {
  int a = *(const int *)left;
  int b = *(const int *)right;
  (void)ctx;
  return (a > b) - (a < b);
}

typedef struct large_heap_item {
  int priority;
  unsigned char payload[512];
} large_heap_item_t;

static int large_heap_item_compare(const void *left, const void *right, void *ctx) {
  const large_heap_item_t *a = (const large_heap_item_t *)left;
  const large_heap_item_t *b = (const large_heap_item_t *)right;
  (void)ctx;
  return (a->priority > b->priority) - (a->priority < b->priority);
}

static int int_tree_map_compare(const void *left, const void *right, void *ctx) {
  int a = *(const int *)left;
  int b = *(const int *)right;
  (void)ctx;
  return (a > b) - (a < b);
}

TURBO_HEAP_DEFINE(int_heap_t, int, int_min_compare)
TURBO_TREE_MAP_DEFINE(int_tree_map_t, int, int, int_tree_map_compare)
TURBO_BPLUS_TREE_DEFINE(int_bplus_tree_t, int, int, int_tree_map_compare)

suite("Turbo Containers") {
  group("Vec") {
    it("pushes, indexes, inserts, erases, and pops generic values") {
      turbo_vec_t vec;
      int value = 0;
      int out = 0;

      check_int_eq(turbo_vec_init(&vec, sizeof(int)), TURBO_OK);
      for (value = 0; value < 8; ++value) {
        check_int_eq(turbo_vec_push(&vec, &value), TURBO_OK);
      }
      check_size_eq(turbo_vec_size(&vec), 8);
      check_int_eq(*(int *)turbo_vec_at(&vec, 3), 3);

      value = 99;
      check_int_eq(turbo_vec_insert(&vec, 4, &value), TURBO_OK);
      check_int_eq(*(int *)turbo_vec_at(&vec, 4), 99);
      check_size_eq(turbo_vec_size(&vec), 9);

      check_int_eq(turbo_vec_erase(&vec, 4, &out), TURBO_OK);
      check_int_eq(out, 99);
      check_int_eq(*(int *)turbo_vec_at(&vec, 4), 4);

      check_int_eq(turbo_vec_pop(&vec, &out), TURBO_OK);
      check_int_eq(out, 7);
      check_size_eq(turbo_vec_size(&vec), 7);

      turbo_vec_destroy(&vec);
    }

    it("supports typed wrappers") {
      int_vec_t vec;
      int out = 0;

      check_int_eq(int_vec_t_init(&vec), TURBO_OK);
      check_int_eq(int_vec_t_push(&vec, 10), TURBO_OK);
      check_int_eq(int_vec_t_push(&vec, 20), TURBO_OK);
      check_size_eq(int_vec_t_size(&vec), 2);
      check_int_eq(*int_vec_t_at(&vec, 1), 20);
      check_true(int_vec_t_pop(&vec, &out));
      check_int_eq(out, 20);
      int_vec_t_destroy(&vec);
    }

    it("zero-fills resize growth") {
      turbo_vec_t vec;

      check_int_eq(turbo_vec_init(&vec, sizeof(int)), TURBO_OK);
      check_int_eq(turbo_vec_resize(&vec, 3), TURBO_OK);
      check_int_eq(*(int *)turbo_vec_at(&vec, 0), 0);
      check_int_eq(*(int *)turbo_vec_at(&vec, 2), 0);
      turbo_vec_destroy(&vec);
    }
  }

  group("Heap") {
    it("pops values according to comparator priority") {
      int_heap_t heap;
      int values[] = {5, 1, 3, 2, 4};
      int expected[] = {1, 2, 3, 4, 5};
      int out = 0;
      size_t i = 0;

      check_int_eq(int_heap_t_init(&heap), TURBO_OK);
      for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        check_int_eq(int_heap_t_push(&heap, values[i]), TURBO_OK);
      }
      check_size_eq(int_heap_t_size(&heap), 5);
      check_int_eq(*int_heap_t_peek(&heap), 1);

      for (i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        check_true(int_heap_t_pop(&heap, &out));
        check_int_eq(out, expected[i]);
      }
      check_true(int_heap_t_empty(&heap));
      int_heap_t_destroy(&heap);
    }

    it("orders elements larger than the stack swap buffer") {
      turbo_heap_t heap;
      large_heap_item_t item;
      large_heap_item_t out;
      int priorities[] = {9, 1, 7, 3, 5};
      int expected[] = {1, 3, 5, 7, 9};
      size_t i;

      check_int_eq(turbo_heap_init(&heap, sizeof(item), large_heap_item_compare, NULL), TURBO_OK);
      for (i = 0; i < sizeof(priorities) / sizeof(priorities[0]); ++i) {
        memset(&item, priorities[i], sizeof(item));
        item.priority = priorities[i];
        check_int_eq(turbo_heap_push(&heap, &item), TURBO_OK);
      }
      for (i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        check_int_eq(turbo_heap_pop(&heap, &out), TURBO_OK);
        check_int_eq(out.priority, expected[i]);
      }
      turbo_heap_destroy(&heap);
    }
  }

  group("Hash map") {
    it("puts, updates, gets, removes, and reuses tombstones") {
      u64_int_map_t map;
      uint64_t key = 0;
      int value = 0;
      int removed = 0;
      size_t i = 0;

      check_int_eq(u64_int_map_t_init(&map), TURBO_OK);
      for (i = 0; i < 128; ++i) {
        key = (uint64_t)i;
        value = (int)(i * 10);
        check_int_eq(u64_int_map_t_put(&map, key, value), TURBO_OK);
      }
      check_size_eq(u64_int_map_t_size(&map), 128);

      key = 42;
      check_not_null(u64_int_map_t_get(&map, key));
      check_int_eq(*u64_int_map_t_get(&map, key), 420);

      value = 777;
      check_int_eq(u64_int_map_t_put(&map, key, value), TURBO_OK);
      check_size_eq(u64_int_map_t_size(&map), 128);
      check_int_eq(*u64_int_map_t_get(&map, key), 777);

      check_true(u64_int_map_t_remove(&map, key, &removed));
      check_int_eq(removed, 777);
      check_false(u64_int_map_t_contains(&map, key));
      check_size_eq(u64_int_map_t_size(&map), 127);

      check_int_eq(u64_int_map_t_put(&map, key, 4242), TURBO_OK);
      check_int_eq(*u64_int_map_t_get(&map, key), 4242);
      u64_int_map_t_destroy(&map);
    }

    it("supports byte keys with custom value structs") {
      typedef struct {
        int id;
        char tag[8];
      } item_t;

      turbo_hash_map_t map;
      const char key1[4] = {'a', 'b', 'c', '\0'};
      const char key2[4] = {'x', 'y', 'z', '\0'};
      item_t item = {1, "one"};
      const item_t *found = NULL;

      check_int_eq(turbo_hash_map_init(&map, sizeof(key1), sizeof(item), NULL, NULL, NULL),
                   TURBO_OK);
      check_int_eq(turbo_hash_map_put(&map, key1, &item), TURBO_OK);
      item.id = 2;
      strcpy(item.tag, "two");
      check_int_eq(turbo_hash_map_put(&map, key2, &item), TURBO_OK);

      found = (const item_t *)turbo_hash_map_get_const(&map, key2);
      check_not_null(found);
      check_int_eq(found->id, 2);
      check_str_eq(found->tag, "two");
      turbo_hash_map_destroy(&map);
    }

    it("keeps key and value storage suitably aligned after rehash") {
      turbo_hash_map_t map;
      long double key;
      long double value;
      const long double *found;
      size_t i;

      check_int_eq(turbo_hash_map_init(&map, sizeof(key), sizeof(value), NULL, NULL, NULL),
                   TURBO_OK);
      for (i = 0; i < 128; ++i) {
        key = (long double)i + 0.25L;
        value = (long double)i * 2.0L;
        check_int_eq(turbo_hash_map_put(&map, &key, &value), TURBO_OK);
      }
      key = 63.25L;
      found = (const long double *)turbo_hash_map_get_const(&map, &key);
      check_not_null(found);
      check_true(((uintptr_t)found % _Alignof(long double)) == 0);
      check_true(*found == 126.0L);
      turbo_hash_map_destroy(&map);
    }
  }

  group("Set") {
    it("adds, deduplicates, contains, and removes generic keys") {
      turbo_set_t set;
      uint64_t key = 0;
      size_t i = 0;

      check_int_eq(turbo_set_init(&set, sizeof(key), NULL, NULL, NULL), TURBO_OK);
      for (i = 0; i < 128; ++i) {
        key = (uint64_t)i;
        check_int_eq(turbo_set_add(&set, &key), TURBO_OK);
      }
      check_size_eq(turbo_set_size(&set), 128);

      key = 42;
      check_true(turbo_set_contains(&set, &key));
      check_int_eq(turbo_set_add(&set, &key), TURBO_OK);
      check_size_eq(turbo_set_size(&set), 128);

      check_int_eq(turbo_set_remove(&set, &key), TURBO_OK);
      check_false(turbo_set_contains(&set, &key));
      check_size_eq(turbo_set_size(&set), 127);
      check_int_eq(turbo_set_remove(&set, &key), TURBO_ENOENT);

      turbo_set_destroy(&set);
    }

    it("supports typed wrappers") {
      u64_set_t set;

      check_int_eq(u64_set_t_init(&set), TURBO_OK);
      check_int_eq(u64_set_t_reserve(&set, 32), TURBO_OK);
      check_int_eq(u64_set_t_add(&set, 7), TURBO_OK);
      check_int_eq(u64_set_t_add(&set, 11), TURBO_OK);
      check_true(u64_set_t_contains(&set, 7));
      check_false(u64_set_t_contains(&set, 5));
      check_true(u64_set_t_remove(&set, 7));
      check_false(u64_set_t_remove(&set, 7));
      check_size_eq(u64_set_t_size(&set), 1);
      check_true(u64_set_t_capacity(&set) >= 32);
      u64_set_t_destroy(&set);
    }
  }

  group("Deque") {
    it("pushes and pops from both ends in logical order") {
      turbo_deque_t deque;
      int value = 0;
      int out = 0;

      check_int_eq(turbo_deque_init(&deque, sizeof(int)), TURBO_OK);
      value = 1;
      check_int_eq(turbo_deque_push_back(&deque, &value), TURBO_OK);
      value = 2;
      check_int_eq(turbo_deque_push_back(&deque, &value), TURBO_OK);
      value = 0;
      check_int_eq(turbo_deque_push_front(&deque, &value), TURBO_OK);
      value = -1;
      check_int_eq(turbo_deque_push_front(&deque, &value), TURBO_OK);

      check_size_eq(turbo_deque_size(&deque), 4);
      check_int_eq(*(int *)turbo_deque_front(&deque), -1);
      check_int_eq(*(int *)turbo_deque_back(&deque), 2);
      check_int_eq(*(int *)turbo_deque_at(&deque, 0), -1);
      check_int_eq(*(int *)turbo_deque_at(&deque, 1), 0);
      check_int_eq(*(int *)turbo_deque_at(&deque, 2), 1);
      check_int_eq(*(int *)turbo_deque_at(&deque, 3), 2);

      check_int_eq(turbo_deque_pop_front(&deque, &out), TURBO_OK);
      check_int_eq(out, -1);
      check_int_eq(turbo_deque_pop_back(&deque, &out), TURBO_OK);
      check_int_eq(out, 2);
      check_size_eq(turbo_deque_size(&deque), 2);

      turbo_deque_destroy(&deque);
    }

    it("preserves order across wrap-around and growth") {
      int_deque_t deque;
      const int_deque_t *const_deque = NULL;
      int out = 0;
      int i = 0;

      check_int_eq(int_deque_t_init(&deque), TURBO_OK);
      check_int_eq(turbo_deque_pop_front(&deque.raw, &out), TURBO_ENOENT);
      check_int_eq(turbo_deque_pop_back(&deque.raw, &out), TURBO_ENOENT);
      check_int_eq(int_deque_t_reserve(&deque, 4), TURBO_OK);
      for (i = 0; i < 4; ++i) {
        check_int_eq(int_deque_t_push_back(&deque, i), TURBO_OK);
      }
      check_true(int_deque_t_pop_front(&deque, &out));
      check_int_eq(out, 0);
      check_true(int_deque_t_pop_front(&deque, &out));
      check_int_eq(out, 1);

      check_int_eq(int_deque_t_push_back(&deque, 4), TURBO_OK);
      check_int_eq(int_deque_t_push_back(&deque, 5), TURBO_OK);
      check_int_eq(int_deque_t_push_front(&deque, 1), TURBO_OK);
      check_int_eq(int_deque_t_push_back(&deque, 6), TURBO_OK);

      check_size_eq(int_deque_t_size(&deque), 6);
      const_deque = &deque;
      check_int_eq(*int_deque_t_front_const(const_deque), 1);
      check_int_eq(*int_deque_t_back_const(const_deque), 6);
      for (i = 0; i < 6; ++i) {
        check_int_eq(*int_deque_t_at_const(const_deque, (size_t)i), i + 1);
      }

      for (i = 1; i <= 6; ++i) {
        check_true(int_deque_t_pop_front(&deque, &out));
        check_int_eq(out, i);
      }
      check_true(int_deque_t_empty(&deque));
      int_deque_t_destroy(&deque);
    }
  }

  group("Map") {
    it("reuses map wrappers with clear typed names") {
      u64_int_map_alias_t map;
      uint64_t key = 7;
      int value = 70;
      int out = 0;

      check_int_eq(u64_int_map_alias_t_init(&map), TURBO_OK);
      check_int_eq(u64_int_map_alias_t_put(&map, key, value), TURBO_OK);
      check_size_eq(u64_int_map_alias_t_size(&map), 1U);
      check_true(u64_int_map_alias_t_contains(&map, key));
      value = 77;
      check_int_eq(u64_int_map_alias_t_put(&map, key, value), TURBO_OK);
      check_int_eq(*u64_int_map_alias_t_get(&map, key), 77);
      check_int_eq(u64_int_map_alias_t_remove(&map, key, &out), TURBO_OK);
      check_int_eq(out, 77);
      check_size_eq(u64_int_map_alias_t_size(&map), 0U);
      u64_int_map_alias_t_destroy(&map);
    }
  }

  group("List") {
    it("behaves like a deque-backed list") {
      int_list_t list;
      int value = 0;
      int out = 0;

      check_int_eq(int_list_t_init(&list), TURBO_OK);
      check_int_eq(int_list_t_push_back(&list, 1), TURBO_OK);
      check_int_eq(int_list_t_push_back(&list, 3), TURBO_OK);
      check_int_eq(int_list_t_push_front(&list, 0), TURBO_OK);
      check_int_eq(int_list_t_reserve(&list, 8), TURBO_OK);

      check_size_eq(int_list_t_size(&list), 3U);
      check_size_eq(int_list_t_capacity(&list), 8U);
      check_int_eq(*int_list_t_front(&list), 0);
      check_int_eq(*int_list_t_back(&list), 3);
      check_int_eq(*int_list_t_at(&list, 1), 1);

      value = 2;
      check_true(int_list_t_pop_front(&list, &out));
      check_int_eq(out, 0);
      check_true(int_list_t_pop_back(&list, &out));
      check_int_eq(out, 3);
      check_size_eq(int_list_t_size(&list), 1U);
      check_int_eq(*int_list_t_at(&list, 0), 1);
      int_list_t_destroy(&list);
    }
  }

  group("Multi-map") {
    it("stores repeated keys and removes per-value safely") {
      u64_multi_map_t mm;
      const turbo_vec_t *values = NULL;
      int removed = 0;

      check_int_eq(u64_multi_map_t_init(&mm), TURBO_OK);
      check_int_eq(u64_multi_map_t_put(&mm, 42U, 100), TURBO_OK);
      check_int_eq(u64_multi_map_t_put(&mm, 42U, 200), TURBO_OK);
      check_int_eq(u64_multi_map_t_put(&mm, 7U, 10), TURBO_OK);

      check_size_eq(u64_multi_map_t_size(&mm), 3U);
      check_size_eq(u64_multi_map_t_count(&mm, 42U), 2U);
      check_true(u64_multi_map_t_contains(&mm, 42U));
      values = u64_multi_map_t_values_const(&mm, 42U);
      check_not_null(values);
      check_int_eq(*(const int *)turbo_vec_at_const(values, 0), 100);
      check_int_eq(*(const int *)turbo_vec_at_const(values, 1), 200);

      check_true(u64_multi_map_t_remove(&mm, 42U, &removed));
      check_int_eq(removed, 200);
      check_size_eq(u64_multi_map_t_size(&mm), 2U);
      check_size_eq(u64_multi_map_t_count(&mm, 42U), 1U);
      check_size_eq(u64_multi_map_t_erase(&mm, 42U), 1U);
      check_size_eq(u64_multi_map_t_size(&mm), 1U);
      check_false(u64_multi_map_t_contains(&mm, 42U));
      u64_multi_map_t_destroy(&mm);
    }

    it("supports clear and reuse correctly") {
      u64_multi_map_t mm;
      const turbo_vec_t *values = NULL;

      check_int_eq(u64_multi_map_t_init(&mm), TURBO_OK);
      check_int_eq(u64_multi_map_t_put(&mm, 1U, 10), TURBO_OK);
      check_int_eq(u64_multi_map_t_put(&mm, 1U, 20), TURBO_OK);
      check_int_eq(u64_multi_map_t_size(&mm), 2U);

      u64_multi_map_t_clear(&mm);
      check_size_eq(u64_multi_map_t_size(&mm), 0U);
      check_false(u64_multi_map_t_contains(&mm, 1U));
      values = u64_multi_map_t_values_const(&mm, 1U);
      check_true(values == NULL);

      check_int_eq(u64_multi_map_t_put(&mm, 1U, 30), TURBO_OK);
      check_size_eq(u64_multi_map_t_size(&mm), 1U);
      values = u64_multi_map_t_values_const(&mm, 1U);
      check_not_null(values);
      check_int_eq(*(const int *)turbo_vec_at_const(values, 0), 30);
      u64_multi_map_t_destroy(&mm);
    }

    it("returns false when removing missing keys and keeps size stable") {
      u64_multi_map_t mm;
      int removed = 0;

      check_int_eq(u64_multi_map_t_init(&mm), TURBO_OK);
      check_int_eq(u64_multi_map_t_put(&mm, 1U, 11), TURBO_OK);
      check_size_eq(u64_multi_map_t_size(&mm), 1U);
      check_false(u64_multi_map_t_remove(&mm, 2U, &removed));
      check_size_eq(u64_multi_map_t_size(&mm), 1U);
      check_int_eq(u64_multi_map_t_erase(&mm, 2U), 0U);
      check_size_eq(u64_multi_map_t_size(&mm), 1U);
      u64_multi_map_t_destroy(&mm);
    }
  }

  group("Tree map") {
    it("keeps keys ordered and supports replace/remove by ordered lookup") {
      int_tree_map_t map;
      int value = 0;
      bool found = false;

      check_int_eq(int_tree_map_t_init(&map), TURBO_OK);
      check_int_eq(int_tree_map_t_put(&map, 5, 50), TURBO_OK);
      check_int_eq(int_tree_map_t_put(&map, 1, 10), TURBO_OK);
      check_int_eq(int_tree_map_t_put(&map, 3, 30), TURBO_OK);
      check_size_eq(int_tree_map_t_size(&map), 3U);

      check_int_eq(*int_tree_map_t_get(&map, 1), 10);
      check_int_eq(*int_tree_map_t_get(&map, 3), 30);
      check_true(int_tree_map_t_contains(&map, 5));
      check_false(int_tree_map_t_contains(&map, 2));
      check_int_eq(*int_tree_map_t_key_at(&map, 0), 1);
      check_int_eq(*int_tree_map_t_value_at(&map, 1), 30);
      check_int_eq(*int_tree_map_t_key_at(&map, 2), 5);

      check_int_eq(int_tree_map_t_put(&map, 3, 33), TURBO_OK);
      check_int_eq(*int_tree_map_t_get(&map, 3), 33);
      check_true(int_tree_map_t_remove(&map, 5, &value));
      check_int_eq(value, 50);
      check_false(int_tree_map_t_contains(&map, 5));
      check_size_eq(int_tree_map_t_size(&map), 2U);

      check_size_eq(int_tree_map_t_find_slot(&map, 1, &found), 0U);
      check_true(found);
      check_size_eq(int_tree_map_t_find_slot(&map, 2, &found), 1U);
      check_false(found);
      check_size_eq(int_tree_map_t_find_slot(&map, 4, &found), 2U);
      check_false(found);
      int_tree_map_t_destroy(&map);
    }

    it("keeps ordered slots after internal rebalancing deletions") {
      int_tree_map_t map;
      int out = 0;
      bool found = false;

      check_int_eq(int_tree_map_t_init(&map), TURBO_OK);
      check_int_eq(int_tree_map_t_put(&map, 10, 1), TURBO_OK);
      check_int_eq(int_tree_map_t_put(&map, 20, 2), TURBO_OK);
      check_int_eq(int_tree_map_t_put(&map, 30, 3), TURBO_OK);
      check_int_eq(int_tree_map_t_put(&map, 40, 4), TURBO_OK);
      check_int_eq(int_tree_map_t_put(&map, 50, 5), TURBO_OK);
      check_int_eq(int_tree_map_t_find_slot(&map, 30, &found), 2U);
      check_true(found);

      check_true(int_tree_map_t_remove(&map, 10, &out));
      check_int_eq(out, 1);
      check_size_eq(int_tree_map_t_size(&map), 4U);
      check_size_eq(int_tree_map_t_find_slot(&map, 20, &found), 0U);
      check_true(found);

      check_true(int_tree_map_t_remove(&map, 40, &out));
      check_int_eq(out, 4);
      check_size_eq(int_tree_map_t_size(&map), 3U);
      check_size_eq(int_tree_map_t_find_slot(&map, 50, &found), 2U);
      check_true(found);
      int_tree_map_t_destroy(&map);
    }

    it("keeps in-order invariants through complex multi-key removals") {
      int_tree_map_t map;
      int keys[] = {8, 4, 12, 2, 6, 10, 14, 1, 3, 5, 7, 9, 11, 13, 15};
      int remove_order[] = {8, 6, 14, 1, 10, 2, 12, 13, 3, 15, 4, 5, 7, 9, 11};
      bool present[16] = {false};
      size_t i = 0;
      size_t remaining;
      size_t j = 0;
      bool found = false;

      check_int_eq(int_tree_map_t_init(&map), TURBO_OK);
      for (i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        check_int_eq(int_tree_map_t_put(&map, keys[i], keys[i] * 10), TURBO_OK);
        present[keys[i]] = true;
        check_size_eq(int_tree_map_t_size(&map), i + 1U);
      }

      for (i = 0; i < sizeof(remove_order) / sizeof(remove_order[0]); ++i) {
        check_true(int_tree_map_t_contains(&map, remove_order[i]));
        check_true(int_tree_map_t_remove(&map, remove_order[i], NULL));
        present[remove_order[i]] = false;

        remaining = 0U;
        for (j = 0; j < sizeof(present) / sizeof(present[0]); ++j) {
          if (present[j]) {
            const int *k = int_tree_map_t_key_at_const(&map, remaining);
            check_not_null(k);
            check_int_eq(*k, (int)j);
            check_size_eq(int_tree_map_t_find_slot(&map, (int)j, &found), remaining);
            check_true(found);
            ++remaining;
          } else {
            check_size_eq(int_tree_map_t_find_slot(&map, (int)j, &found), remaining);
            check_false(found);
          }
        }
        check_size_eq(int_tree_map_t_size(&map), remaining);
      }

      check_true(int_tree_map_t_empty(&map));
      int_tree_map_t_destroy(&map);
    }

    it("handles empty and single-node operations safely") {
      int_tree_map_t map;
      int value = 0;
      bool found = false;

      check_int_eq(int_tree_map_t_init(&map), TURBO_OK);
      check_size_eq(int_tree_map_t_size(&map), 0U);
      check_size_eq(int_tree_map_t_find_slot(&map, 1, &found), 0U);
      check_false(found);
      check_null(int_tree_map_t_get(&map, 1));
      check_false(int_tree_map_t_contains(&map, 1));
      check_false(int_tree_map_t_remove(&map, 1, &value));

      check_int_eq(int_tree_map_t_put(&map, 7, 70), TURBO_OK);
      check_size_eq(int_tree_map_t_size(&map), 1U);
      check_true(int_tree_map_t_contains(&map, 7));
      check_int_eq(*int_tree_map_t_get(&map, 7), 70);

      check_int_eq(int_tree_map_t_put(&map, 7, 77), TURBO_OK);
      check_size_eq(int_tree_map_t_size(&map), 1U);
      check_int_eq(*int_tree_map_t_get(&map, 7), 77);

      check_true(int_tree_map_t_remove(&map, 7, &value));
      check_int_eq(value, 77);
      check_true(int_tree_map_t_empty(&map));
      check_false(int_tree_map_t_remove(&map, 7, &value));
      int_tree_map_t_destroy(&map);
    }
  }

  group("B+ tree") {
    it("keeps sorted keys and supports replacement semantics") {
      int_bplus_tree_t map;
      bool found = false;

      check_int_eq(int_bplus_tree_t_init(&map), TURBO_OK);
      check_int_eq(int_bplus_tree_t_put(&map, 50, 500), TURBO_OK);
      check_int_eq(int_bplus_tree_t_put(&map, 20, 200), TURBO_OK);
      check_int_eq(int_bplus_tree_t_put(&map, 70, 700), TURBO_OK);
      check_int_eq(int_bplus_tree_t_put(&map, 10, 100), TURBO_OK);
      check_int_eq(int_bplus_tree_t_put(&map, 40, 400), TURBO_OK);
      check_int_eq(int_bplus_tree_t_put(&map, 60, 600), TURBO_OK);
      check_int_eq(int_bplus_tree_t_put(&map, 80, 800), TURBO_OK);
      check_size_eq(int_bplus_tree_t_size(&map), 7U);

      check_int_eq(*int_bplus_tree_t_key_at(&map, 0), 10);
      check_int_eq(*int_bplus_tree_t_key_at(&map, 2), 40);
      check_int_eq(*int_bplus_tree_t_key_at(&map, 6), 80);
      check_int_eq(*int_bplus_tree_t_value_at(&map, 3), 500);
      check_true(int_bplus_tree_t_contains(&map, 20));
      check_false(int_bplus_tree_t_contains(&map, 99));
      check_true(int_bplus_tree_t_get(&map, 40) != NULL);
      check_int_eq(*int_bplus_tree_t_get(&map, 40), 400);

      check_size_eq(int_bplus_tree_t_find_slot(&map, 40, &found), 2U);
      check_true(found);
      check_size_eq(int_bplus_tree_t_find_slot(&map, 41, &found), 3U);
      check_false(found);
      check_size_eq(int_bplus_tree_t_find_slot(&map, 1, &found), 0U);
      check_false(found);

      check_int_eq(int_bplus_tree_t_put(&map, 40, 441), TURBO_OK);
      check_int_eq(*int_bplus_tree_t_get(&map, 40), 441);
      int_bplus_tree_t_destroy(&map);
    }

    it("removes keys and keeps leaf order intact") {
      int_bplus_tree_t map;
      int value = 0;

      check_int_eq(int_bplus_tree_t_init(&map), TURBO_OK);
      check_int_eq(int_bplus_tree_t_put(&map, 1, 10), TURBO_OK);
      check_int_eq(int_bplus_tree_t_put(&map, 2, 20), TURBO_OK);
      check_int_eq(int_bplus_tree_t_put(&map, 3, 30), TURBO_OK);
      check_int_eq(int_bplus_tree_t_put(&map, 4, 40), TURBO_OK);
      check_int_eq(int_bplus_tree_t_put(&map, 5, 50), TURBO_OK);
      check_int_eq(int_bplus_tree_t_put(&map, 6, 60), TURBO_OK);

      check_int_eq(int_bplus_tree_t_remove(&map, 4, &value), true);
      check_int_eq(value, 40);
      check_size_eq(int_bplus_tree_t_size(&map), 5U);
      check_false(int_bplus_tree_t_contains(&map, 4));
      check_int_eq(*int_bplus_tree_t_key_at(&map, 2), 3);
      check_int_eq(*int_bplus_tree_t_key_at(&map, 3), 5);

      check_false(int_bplus_tree_t_remove(&map, 100, &value));
      check_size_eq(int_bplus_tree_t_size(&map), 5U);
      int_bplus_tree_t_destroy(&map);
    }

    it("preserves order through large batched updates") {
      int_bplus_tree_t map;
      int value = 0;
      size_t i;
      bool found = false;

      check_int_eq(int_bplus_tree_t_init(&map), TURBO_OK);
      for (i = 0; i < 200U; ++i) {
        check_int_eq(int_bplus_tree_t_put(&map, (int)i, (int)(i * 10)), TURBO_OK);
      }
      check_size_eq(int_bplus_tree_t_size(&map), 200U);
      for (i = 0; i < 200U; ++i) {
        bool found = false;
        check_size_eq(int_bplus_tree_t_find_slot(&map, (int)i, &found), i);
        check_true(found);
        check_int_eq(*int_bplus_tree_t_key_at(&map, i), (int)i);
      }

      for (i = 0; i < 200U; i += 2U) {
        check_true(int_bplus_tree_t_remove(&map, (int)i, &value));
        check_int_eq(value, (int)i * 10);
      }
      check_size_eq(int_bplus_tree_t_size(&map), 100U);
      check_false(int_bplus_tree_t_contains(&map, 0));
      check_true(int_bplus_tree_t_contains(&map, 199));
      found = false;
      check_size_eq(int_bplus_tree_t_find_slot(&map, 0, &found), 0U);
      check_false(found);
      found = false;
      check_size_eq(int_bplus_tree_t_find_slot(&map, 200, &found), 100U);
      check_false(found);
      found = false;
      check_size_eq(int_bplus_tree_t_find_slot(&map, 102, &found), 51U);
      check_false(found);

      for (i = 1; i < 200U; i += 2U) {
        check_int_eq(*int_bplus_tree_t_key_at(&map, i / 2U), (int)i);
      }

      for (i = 0; i < 200U; i += 2U) {
        check_int_eq(int_bplus_tree_t_put(&map, (int)i, (int)(i * 10)), TURBO_OK);
      }
      check_size_eq(int_bplus_tree_t_size(&map), 200U);
      for (i = 0; i < 200U; ++i) {
        check_int_eq(*int_bplus_tree_t_key_at(&map, i), (int)i);
      }

      check_false(int_bplus_tree_t_remove(&map, 1000, &value));
      check_size_eq(int_bplus_tree_t_size(&map), 200U);
      int_bplus_tree_t_destroy(&map);
    }
  }
}
