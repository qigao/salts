#include "stream_turbo_containers.h"
#include "tinytest.h"

static bool int_is_even(const void *value)
{
    return *(const int *)value % 2 == 0;
}

static stream_result_t identity_mapper(const void *value, void *output)
{
    *(int *)output = *(const int *)value;
    return STREAM_OK;
}

static stream_result_t key_by_remainder_two(const void *value, void *out_key)
{
    *(int *)out_key = *(const int *)value % 2;
    return STREAM_OK;
}

static stream_result_t key_by_identity(const void *value, void *out_key)
{
    *(int *)out_key = *(const int *)value;
    return STREAM_OK;
}

static stream_result_t fail_key_selector(const void *value, void *out_key)
{
    (void)value;
    (void)out_key;
    return STREAM_ERROR;
}

static stream_result_t value_as_one(const void *value, void *output)
{
    (void)value;
    *(int *)output = 1;
    return STREAM_OK;
}

static stream_result_t sum_reducer(void *accumulator, const void *value)
{
    *(int *)accumulator += *(const int *)value;
    return STREAM_OK;
}

static stream_result_t fail_value_mapper(const void *value, void *output)
{
    (void)value;
    (void)output;
    return STREAM_ERROR;
}

static stream_result_t fail_reducer(void *accumulator, const void *value)
{
    (void)accumulator;
    (void)value;
    return STREAM_ERROR;
}

static int int_cmp(const void *left, const void *right, void *ctx)
{
    const int lhs = *(const int *)left;
    const int rhs = *(const int *)right;

    (void)ctx;
    return lhs < rhs ? -1 : lhs > rhs ? 1 : 0;
}

static void sort_ints(int *values, size_t count)
{
    size_t i;
    size_t j;

    for (i = 1; i < count; ++i) {
        const int key = values[i];
        j = i;
        while (j > 0 && values[j - 1] > key) {
            values[j] = values[j - 1];
            --j;
        }
        values[j] = key;
    }
}

spec("TurboUtils container streams") {
    it("supports fluent turbo vector bootstrap") {
        turbo_vec_t vec;
        stream_t stream;
        stream_t *s;
        stream_item_t item;
        int output = 0;

        check_equal(turbo_vec_init(&vec, sizeof(int)), TURBO_OK);
        check_equal(turbo_vec_push(&vec, &(int){1}), TURBO_OK);
        check_equal(turbo_vec_push(&vec, &(int){2}), TURBO_OK);
        check_equal(turbo_vec_push(&vec, &(int){3}), TURBO_OK);

        s = STREAM_FROM_TURBO_VEC(&stream, &vec);
        check_not_null((const void *)s);
        check_equal((const void *)(s), (const void *)(&stream));
        s->filter(s, int_is_even)->limit(s, 1);

        item.data = &output;
        item.size = sizeof(output);
        check_equal(s->next(s, &item), STREAM_OK);
        check_equal(output, 2);
        check_equal(s->next(s, &item), STREAM_END);

        turbo_vec_destroy(&vec);
    }

    it("supports fluent turbo map bootstrap") {
        turbo_map_t map;
        stream_t stream;
        stream_t *s;
        stream_result_t result;
        stream_item_t item;
        int output[3];
        size_t index = 0;

        check_equal(turbo_map_init(&map, sizeof(int), sizeof(int), NULL, NULL, NULL), TURBO_OK);
        check_equal(turbo_map_put(&map, &(int){1}, &(int){10}), TURBO_OK);
        check_equal(turbo_map_put(&map, &(int){2}, &(int){20}), TURBO_OK);
        check_equal(turbo_map_put(&map, &(int){3}, &(int){30}), TURBO_OK);

        s = STREAM_FROM_TURBO_MAP_KEYS(&stream, &map);
        check_not_null((const void *)s);
        check_equal((const void *)(s), (const void *)(&stream));

        do {
            item.data = &output[index];
            item.size = sizeof(output[index]);
            result = s->next(s, &item);
            if (result == STREAM_OK) {
                ++index;
            }
        } while (result == STREAM_OK);

        check_equal(result, STREAM_END);
        check_equal(index, 3);
        check_equal(output[0] + output[1] + output[2], 6);

        turbo_map_destroy(&map);
    }

    it("returns NULL for fluent turbo bootstrap on null source") {
        stream_t stream;

        check_true(STREAM_FROM_TURBO_VEC(&stream, NULL) == NULL);
        check_true(STREAM_FROM_TURBO_MAP_VALUES(&stream, NULL) == NULL);
    }

    it("streams a turbo vector with fluent operators") {
        turbo_vec_t vec;
        stream_t stream;
        stream_t *s = &stream;
        stream_item_t item;
        int output = 0;

        check_equal(turbo_vec_init(&vec, sizeof(int)), TURBO_OK);
        check_equal(turbo_vec_push(&vec, &(int){1}), TURBO_OK);
        check_equal(turbo_vec_push(&vec, &(int){2}), TURBO_OK);
        check_equal(turbo_vec_push(&vec, &(int){4}), TURBO_OK);
        check_equal(stream_from_turbo_vec(s, &vec), STREAM_OK);
        s->filter(s, int_is_even)->take(s, 1);

        item.data = &output;
        item.size = sizeof(output);
        check_equal(s->next(s, &item), STREAM_OK);
        check_equal(output, 2);
        check_equal(s->next(s, &item), STREAM_END);
        turbo_vec_destroy(&vec);
    }

    it("preserves logical deque order across wraparound") {
        turbo_deque_t deque;
        stream_t stream;
        stream_item_t item;
        int removed = 0;
        int output = 0;

        check_equal(turbo_deque_init(&deque, sizeof(int)), TURBO_OK);
        check_equal(turbo_deque_reserve(&deque, 3), TURBO_OK);
        check_equal(turbo_deque_push_back(&deque, &(int){1}), TURBO_OK);
        check_equal(turbo_deque_push_back(&deque, &(int){2}), TURBO_OK);
        check_equal(turbo_deque_push_back(&deque, &(int){3}), TURBO_OK);
        check_equal(turbo_deque_pop_front(&deque, &removed), TURBO_OK);
        check_equal(turbo_deque_push_back(&deque, &(int){4}), TURBO_OK);
        check_equal(stream_from_turbo_deque(&stream, &deque), STREAM_OK);

        item.data = &output;
        item.size = sizeof(output);
        check_equal(stream_next(&stream, &item), STREAM_OK);
        check_equal(output, 2);
        item.size = sizeof(output);
        check_equal(stream_next(&stream, &item), STREAM_OK);
        check_equal(output, 3);
        item.size = sizeof(output);
        check_equal(stream_next(&stream, &item), STREAM_OK);
        check_equal(output, 4);
        turbo_deque_destroy(&deque);
    }

    it("fails fast after structural vector modification") {
        turbo_vec_t vec;
        stream_t stream;
        stream_item_t item;

        check_equal(turbo_vec_init(&vec, sizeof(int)), TURBO_OK);
        check_equal(turbo_vec_push(&vec, &(int){1}), TURBO_OK);
        check_equal(stream_from_turbo_vec(&stream, &vec), STREAM_OK);
        check_equal(stream_next_view(&stream, &item), STREAM_OK);
        check_equal(turbo_vec_push(&vec, &(int){2}), TURBO_OK);
        check_equal(stream_next_view(&stream, &item), STREAM_MODIFIED);
        turbo_vec_destroy(&vec);
    }

    it("streams hash map values") {
        turbo_hash_map_t map;
        stream_t stream;
        stream_item_t item;
        int total = 0;

        check_equal(turbo_hash_map_init(
                         &map, sizeof(int), sizeof(int), NULL, NULL, NULL),
                     TURBO_OK);
        check_equal(turbo_hash_map_put(&map, &(int){1}, &(int){10}), TURBO_OK);
        check_equal(turbo_hash_map_put(&map, &(int){2}, &(int){20}), TURBO_OK);
        check_equal(stream_from_turbo_hash_values(&stream, &map), STREAM_OK);

        while (stream_next_view(&stream, &item) == STREAM_OK) {
            total += *(const int *)item.data;
        }
        check_equal(total, 30);
        turbo_hash_map_destroy(&map);
    }

    it("streams turbo multimap keys with duplicate key flattening") {
        turbo_multimap_t map;
        stream_t stream;
        stream_result_t result;
        stream_item_t item;
        int values[5];
        const size_t expected_count = 4;
        size_t count = 0;

        check_equal(
            turbo_multimap_init(&map, sizeof(int), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(turbo_multimap_put(&map, &(int){2}, &(int){20}), TURBO_OK);
        check_equal(turbo_multimap_put(&map, &(int){1}, &(int){10}), TURBO_OK);
        check_equal(turbo_multimap_put(&map, &(int){2}, &(int){21}), TURBO_OK);
        check_equal(turbo_multimap_put(&map, &(int){3}, &(int){30}), TURBO_OK);
        check_equal(turbo_multimap_size(&map), 4);
        check_equal(turbo_multimap_key_count(&map, &(int){1}), 1);
        check_equal(turbo_multimap_key_count(&map, &(int){2}), 2);
        check_equal(turbo_multimap_key_count(&map, &(int){3}), 1);
        check_equal(stream_from_turbo_multimap_keys(&stream, &map), STREAM_OK);

        while (count < expected_count) {
            item.data = &values[count];
            item.size = sizeof(values[0]);
            result = stream_next(&stream, &item);
            check_equal(result, STREAM_OK);
            ++count;
        }
        result = stream_next(&stream, &item);
        check_equal(result, STREAM_END);
        sort_ints(values, count);
        check_equal(values[0], 1);
        check_equal(values[1], 2);
        check_equal(values[2], 2);
        check_equal(values[3], 3);
        turbo_multimap_destroy(&map);
    }

    it("streams turbo multimap values with duplicate key flattening") {
        turbo_multimap_t map;
        stream_t stream;
        stream_result_t result;
        stream_item_t item;
        int values[5];
        const size_t expected_count = 4;
        size_t count = 0;

        check_equal(
            turbo_multimap_init(&map, sizeof(int), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(turbo_multimap_put(&map, &(int){2}, &(int){20}), TURBO_OK);
        check_equal(turbo_multimap_put(&map, &(int){1}, &(int){10}), TURBO_OK);
        check_equal(turbo_multimap_put(&map, &(int){2}, &(int){21}), TURBO_OK);
        check_equal(turbo_multimap_put(&map, &(int){3}, &(int){30}), TURBO_OK);
        check_equal(turbo_multimap_size(&map), 4);
        check_equal(turbo_multimap_key_count(&map, &(int){1}), 1);
        check_equal(turbo_multimap_key_count(&map, &(int){2}), 2);
        check_equal(turbo_multimap_key_count(&map, &(int){3}), 1);
        check_equal(stream_from_turbo_multimap_values(&stream, &map), STREAM_OK);

        while (count < expected_count) {
            item.data = &values[count];
            item.size = sizeof(values[0]);
            result = stream_next(&stream, &item);
            check_equal(result, STREAM_OK);
            ++count;
        }
        result = stream_next(&stream, &item);
        check_equal(result, STREAM_END);
        sort_ints(values, count);
        check_equal(values[0], 10);
        check_equal(values[1], 20);
        check_equal(values[2], 21);
        check_equal(values[3], 30);
        turbo_multimap_destroy(&map);
    }

    it("streams turbo heap values in internal heap order") {
        turbo_heap_t heap;
        stream_t stream;
        stream_item_t item;
        int values[4];
        int i;

        check_equal(turbo_heap_init(&heap, sizeof(int), int_cmp, NULL), TURBO_OK);
        check_equal(turbo_heap_push(&heap, &(int){5}), TURBO_OK);
        check_equal(turbo_heap_push(&heap, &(int){2}), TURBO_OK);
        check_equal(turbo_heap_push(&heap, &(int){9}), TURBO_OK);
        check_equal(turbo_heap_push(&heap, &(int){7}), TURBO_OK);
        check_equal(turbo_heap_push(&heap, &(int){3}), TURBO_OK);
        check_equal(stream_from_turbo_heap(&stream, &heap), STREAM_OK);

        for (i = 0; i < 5; ++i) {
            item.data = &values[i];
            item.size = sizeof(values[i]);
            check_equal(stream_next(&stream, &item), STREAM_OK);
        }
        check_equal(stream_next(&stream, &item), STREAM_END);
        sort_ints(values, 5);
        check_equal(values[0], 2);
        check_equal(values[1], 3);
        check_equal(values[2], 5);
        check_equal(values[3], 7);
        check_equal(values[4], 9);
        turbo_heap_destroy(&heap);
    }

    it("fails fast after turbo heap structural modification") {
        turbo_heap_t heap;
        stream_t stream;
        stream_item_t item;
        int value;

        check_equal(turbo_heap_init(&heap, sizeof(int), int_cmp, NULL), TURBO_OK);
        check_equal(turbo_heap_push(&heap, &(int){4}), TURBO_OK);
        check_equal(stream_from_turbo_heap(&stream, &heap), STREAM_OK);
        item.data = &value;
        item.size = sizeof(value);
        check_equal(stream_next(&stream, &item), STREAM_OK);
        check_equal(turbo_heap_push(&heap, &(int){1}), TURBO_OK);
        check_equal(stream_next(&stream, &item), STREAM_MODIFIED);
        turbo_heap_destroy(&heap);
    }

    it("fails fast after turbo multimap structural modification") {
        turbo_multimap_t map;
        stream_t stream;
        stream_item_t item;
        int value;

        check_equal(
            turbo_multimap_init(&map, sizeof(int), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(turbo_multimap_put(&map, &(int){1}, &(int){10}), TURBO_OK);
        check_equal(stream_from_turbo_multimap_values(&stream, &map), STREAM_OK);
        item.data = &value;
        item.size = sizeof(value);
        check_equal(stream_next(&stream, &item), STREAM_OK);
        check_equal(turbo_multimap_put(&map, &(int){1}, &(int){11}), TURBO_OK);
        check_equal(stream_next(&stream, &item), STREAM_MODIFIED);
        turbo_multimap_destroy(&map);
    }

    it("streams set keys") {
        turbo_set_t set;
        stream_t stream;
        stream_item_t item;
        int total = 0;

        check_equal(turbo_set_init(&set, sizeof(int), NULL, NULL, NULL), TURBO_OK);
        check_equal(turbo_set_add(&set, &(int){3}), TURBO_OK);
        check_equal(turbo_set_add(&set, &(int){7}), TURBO_OK);
        check_equal(stream_from_turbo_set(&stream, &set), STREAM_OK);

        while (stream_next_view(&stream, &item) == STREAM_OK) {
            total += *(const int *)item.data;
        }
        check_equal(total, 10);
        turbo_set_destroy(&set);
    }

    it("streams turbo tree map keys in key order") {
        turbo_tree_map_t map;
        stream_t stream;
        stream_item_t item;
        int value;

        check_equal(turbo_tree_map_init(&map, sizeof(int), sizeof(int), int_cmp, NULL), TURBO_OK);
        check_equal(turbo_tree_map_put(&map, &(int){5}, &(int){50}), TURBO_OK);
        check_equal(turbo_tree_map_put(&map, &(int){1}, &(int){10}), TURBO_OK);
        check_equal(turbo_tree_map_put(&map, &(int){3}, &(int){30}), TURBO_OK);
        check_equal(stream_from_turbo_tree_map_keys(&stream, &map), STREAM_OK);

        value = 0;
        item.data = &value;
        item.size = sizeof(value);
        check_equal(stream_next(&stream, &item), STREAM_OK);
        check_equal(value, 1);
        check_equal(stream_next(&stream, &item), STREAM_OK);
        check_equal(value, 3);
        check_equal(stream_next(&stream, &item), STREAM_OK);
        check_equal(value, 5);
        check_equal(stream_next(&stream, &item), STREAM_END);
        turbo_tree_map_destroy(&map);
    }

    it("streams turbo tree map values in key order") {
        turbo_tree_map_t map;
        stream_t stream;
        stream_item_t item;
        int value;

        check_equal(turbo_tree_map_init(&map, sizeof(int), sizeof(int), int_cmp, NULL), TURBO_OK);
        check_equal(turbo_tree_map_put(&map, &(int){5}, &(int){50}), TURBO_OK);
        check_equal(turbo_tree_map_put(&map, &(int){1}, &(int){10}), TURBO_OK);
        check_equal(turbo_tree_map_put(&map, &(int){3}, &(int){30}), TURBO_OK);
        check_equal(stream_from_turbo_tree_map_values(&stream, &map), STREAM_OK);

        value = 0;
        item.data = &value;
        item.size = sizeof(value);
        check_equal(stream_next(&stream, &item), STREAM_OK);
        check_equal(value, 10);
        check_equal(stream_next(&stream, &item), STREAM_OK);
        check_equal(value, 30);
        check_equal(stream_next(&stream, &item), STREAM_OK);
        check_equal(value, 50);
        check_equal(stream_next(&stream, &item), STREAM_END);
        turbo_tree_map_destroy(&map);
    }

    it("streams turbo bplus tree keys in sorted order") {
        turbo_bplus_tree_t tree;
        stream_t stream;
        stream_item_t item;
        int value;

        check_equal(turbo_bplus_tree_init(&tree, sizeof(int), sizeof(int), int_cmp, NULL), TURBO_OK);
        check_equal(turbo_bplus_tree_put(&tree, &(int){7}, &(int){70}), TURBO_OK);
        check_equal(turbo_bplus_tree_put(&tree, &(int){2}, &(int){20}), TURBO_OK);
        check_equal(turbo_bplus_tree_put(&tree, &(int){9}, &(int){90}), TURBO_OK);
        check_equal(stream_from_turbo_bplus_tree_keys(&stream, &tree), STREAM_OK);

        value = 0;
        item.data = &value;
        item.size = sizeof(value);
        check_equal(stream_next(&stream, &item), STREAM_OK);
        check_equal(value, 2);
        check_equal(stream_next(&stream, &item), STREAM_OK);
        check_equal(value, 7);
        check_equal(stream_next(&stream, &item), STREAM_OK);
        check_equal(value, 9);
        check_equal(stream_next(&stream, &item), STREAM_END);
        turbo_bplus_tree_destroy(&tree);
    }

    it("streams turbo bplus tree values in sorted key order") {
        turbo_bplus_tree_t tree;
        stream_t stream;
        stream_item_t item;
        int value;

        check_equal(turbo_bplus_tree_init(&tree, sizeof(int), sizeof(int), int_cmp, NULL), TURBO_OK);
        check_equal(turbo_bplus_tree_put(&tree, &(int){7}, &(int){70}), TURBO_OK);
        check_equal(turbo_bplus_tree_put(&tree, &(int){2}, &(int){20}), TURBO_OK);
        check_equal(turbo_bplus_tree_put(&tree, &(int){9}, &(int){90}), TURBO_OK);
        check_equal(stream_from_turbo_bplus_tree_values(&stream, &tree), STREAM_OK);

        value = 0;
        item.data = &value;
        item.size = sizeof(value);
        check_equal(stream_next(&stream, &item), STREAM_OK);
        check_equal(value, 20);
        check_equal(stream_next(&stream, &item), STREAM_OK);
        check_equal(value, 70);
        check_equal(stream_next(&stream, &item), STREAM_OK);
        check_equal(value, 90);
        check_equal(stream_next(&stream, &item), STREAM_END);
        turbo_bplus_tree_destroy(&tree);
    }

    it("collects a filtered stream into a TurboUtils vector") {
        turbo_vec_t output;
        stream_t stream;
        const int *values;

        check_equal(turbo_vec_init(&output, sizeof(int)), TURBO_OK);
        check_equal(STREAM_OF(&stream, int, 1, 2, 3, 4), STREAM_OK);
        stream_filter(&stream, int_is_even);
        check_equal(
            stream_collect_turbo_vec(&stream, &output, 4),
            STREAM_END);
        check_equal(turbo_vec_size(&output), 2);
        values = (const int *)turbo_vec_data_const(&output);
        check_equal(values[0], 2);
        check_equal(values[1], 4);
        turbo_vec_destroy(&output);
    }

    it("collects a filtered stream into a TurboUtils list") {
        turbo_list_t output;
        stream_t stream;
        const int *values;

        check_equal(turbo_list_init(&output, sizeof(int)), TURBO_OK);
        check_equal(STREAM_OF(&stream, int, 1, 2, 3, 4), STREAM_OK);
        stream_filter(&stream, int_is_even);
        check_equal(
            stream_collect_turbo_list(&stream, &output, 4),
            STREAM_END);
        check_equal(turbo_list_size(&output), 2);
        values = (const int *)turbo_list_at_const(&output, 0);
        check_not_null((const void *)values);
        check_equal(values[0], 2);
        values = (const int *)turbo_list_at_const(&output, 1);
        check_not_null((const void *)values);
        check_equal(values[0], 4);
        turbo_list_destroy(&output);
    }

    it("collects a stream into a TurboUtils set") {
        turbo_set_t output;
        stream_t stream;

        check_equal(turbo_set_init(&output, sizeof(int), NULL, NULL, NULL), TURBO_OK);
        check_equal(STREAM_OF(&stream, int, 1, 2, 2, 3), STREAM_OK);
        check_equal(stream_collect_turbo_set(&stream, &output, 4), STREAM_END);
        check_equal(turbo_set_size(&output), 3);
        check_true(turbo_set_contains(&output, &(int){1}));
        check_true(turbo_set_contains(&output, &(int){2}));
        check_true(turbo_set_contains(&output, &(int){3}));
        turbo_set_destroy(&output);
    }

    it("collects a stream into a TurboUtils map with frequency counts") {
        turbo_map_t output;
        stream_t stream;
        const size_t *odd_count;
        const size_t *even_count;
        int even = 0;
        int odd = 1;

        check_equal(
            turbo_map_init(&output, sizeof(int), sizeof(size_t), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(STREAM_OF(&stream, int, 1, 2, 1, 3, 2, 2), STREAM_OK);
        check_equal(
            stream_collect_turbo_map_count(
                &stream, &output, 4, sizeof(int), key_by_remainder_two),
            STREAM_END);
        check_equal(turbo_map_size(&output), 2);
        even_count = (const size_t *)turbo_map_get_const(&output, &even);
        odd_count = (const size_t *)turbo_map_get_const(&output, &odd);
        check_not_null((const void *)even_count);
        check_not_null((const void *)odd_count);
        check_equal(*even_count, 3);
        check_equal(*odd_count, 3);
        turbo_map_destroy(&output);
    }

    it("partitions stream values into true/false turbo lists by predicate") {
        turbo_list_t true_values;
        turbo_list_t false_values;
        stream_t stream;
        const int *value;

        check_equal(turbo_list_init(&true_values, sizeof(int)), TURBO_OK);
        check_equal(turbo_list_init(&false_values, sizeof(int)), TURBO_OK);
        check_equal(STREAM_OF(&stream, int, 1, 2, 3, 4, 5, 6), STREAM_OK);
        check_equal(
            stream_collect_turbo_partition(
                &stream,
                &true_values,
                6,
                &false_values,
                6,
                int_is_even),
            STREAM_END);

        check_equal(turbo_list_size(&true_values), 3);
        check_equal(turbo_list_size(&false_values), 3);

        value = (const int *)turbo_list_at_const(&true_values, 0);
        check_not_null((const void *)value);
        check_equal(*value, 2);
        value = (const int *)turbo_list_at_const(&true_values, 1);
        check_not_null((const void *)value);
        check_equal(*value, 4);
        value = (const int *)turbo_list_at_const(&true_values, 2);
        check_not_null((const void *)value);
        check_equal(*value, 6);

        value = (const int *)turbo_list_at_const(&false_values, 0);
        check_not_null((const void *)value);
        check_equal(*value, 1);
        value = (const int *)turbo_list_at_const(&false_values, 1);
        check_not_null((const void *)value);
        check_equal(*value, 3);
        value = (const int *)turbo_list_at_const(&false_values, 2);
        check_not_null((const void *)value);
        check_equal(*value, 5);

        turbo_list_destroy(&true_values);
        turbo_list_destroy(&false_values);
    }

    it("returns full when a turbo partition destination reaches its limit") {
        stream_array_source_state_t source_state;
        turbo_list_t true_values;
        turbo_list_t false_values;
        stream_t stream;
        stream_result_t init_result;
        int source_values[] = {1, 2, 3, 4, 5};

        check_equal(turbo_list_init(&true_values, sizeof(int)), TURBO_OK);
        check_equal(turbo_list_init(&false_values, sizeof(int)), TURBO_OK);
        init_result = STREAM_ARRAY_INIT(&stream, &source_state, source_values);
        check_equal(init_result, STREAM_OK);
        check_equal(
            stream_collect_turbo_partition(
                &stream,
                &true_values,
                1,
                &false_values,
                1,
                int_is_even),
            STREAM_FULL);

        check_equal(turbo_list_size(&true_values), 1);
        check_equal(turbo_list_size(&false_values), 1);
        check_equal(*(const int *)turbo_list_front_const(&true_values), 2);
        check_equal(*(const int *)turbo_list_front_const(&false_values), 1);
        check_equal(source_state.pos, 3);
        check_equal(stream.error, STREAM_ERR_NONE);
        check_equal(*(const int *)turbo_list_at_const(&true_values, 0), 2);
        check_equal(*(const int *)turbo_list_at_const(&false_values, 0), 1);
        turbo_list_destroy(&true_values);
        turbo_list_destroy(&false_values);
    }

    it("rejects turbo partition collection when destination types mismatch") {
        turbo_list_t true_values;
        turbo_list_t false_values;
        stream_t stream;

        check_equal(turbo_list_init(&true_values, sizeof(int)), TURBO_OK);
        check_equal(turbo_list_init(&false_values, sizeof(double)), TURBO_OK);
        check_equal(STREAM_OF(&stream, int, 1, 2, 3), STREAM_OK);
        check_equal(
            stream_collect_turbo_partition(
                &stream,
                &true_values,
                3,
                &false_values,
                3,
                int_is_even),
            STREAM_ERROR);
        check_equal(stream.error, STREAM_ERR_BAD_ARGUMENT);
        turbo_list_destroy(&true_values);
        turbo_list_destroy(&false_values);
    }

    it("returns full immediately when both partition limits are already reached") {
        stream_array_source_state_t source_state;
        turbo_list_t true_values;
        turbo_list_t false_values;
        stream_t stream;
        stream_result_t init_result;
        int source_values[] = {9, 8, 7};

        check_equal(
            turbo_list_init(&true_values, sizeof(int)),
            TURBO_OK);
        check_equal(
            turbo_list_init(&false_values, sizeof(int)),
            TURBO_OK);
        init_result = STREAM_ARRAY_INIT(&stream, &source_state, source_values);
        check_equal(init_result, STREAM_OK);

        check_equal(
            stream_collect_turbo_partition(
                &stream,
                &true_values,
                0,
                &false_values,
                0,
                int_is_even),
            STREAM_FULL);
        check_equal(source_state.pos, 0);
        check_equal(turbo_list_size(&true_values), 0);
        check_equal(turbo_list_size(&false_values), 0);
        turbo_list_destroy(&true_values);
        turbo_list_destroy(&false_values);
    }

    it("counts stream values in true/false partition buckets") {
        turbo_map_t output;
        stream_t stream;
        const size_t *false_count;
        const size_t *true_count;
        uint8_t false_key = 0;
        uint8_t true_key = 1;

        check_equal(
            turbo_map_init(&output, sizeof(uint8_t), sizeof(size_t), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(STREAM_OF(&stream, int, 1, 2, 3, 4, 5, 6), STREAM_OK);
        check_equal(
            stream_collect_turbo_partition_count(
                &stream, &output, 2, int_is_even),
            STREAM_END);

        false_count = (const size_t *)turbo_map_get_const(&output, &false_key);
        true_count = (const size_t *)turbo_map_get_const(&output, &true_key);
        check_not_null((const void *)false_count);
        check_not_null((const void *)true_count);
        check_equal(*false_count, 3);
        check_equal(*true_count, 3);
        turbo_map_destroy(&output);
    }

    it("reduces mapped values in true/false partition buckets") {
        turbo_map_t output;
        stream_t stream;
        const int *false_sum;
        const int *true_sum;

        check_equal(
            turbo_map_init(&output, sizeof(uint8_t), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(
            STREAM_OF(&stream, int, 1, 2, 3, 4, 5, 6),
            STREAM_OK);
        check_equal(
            stream_collect_turbo_partition_reduce(
                &stream,
                &output,
                2,
                sizeof(int),
                int_is_even,
                value_as_one,
                sum_reducer),
            STREAM_END);
        false_sum = (const int *)turbo_map_get_const(&output, &(uint8_t){0});
        true_sum = (const int *)turbo_map_get_const(&output, &(uint8_t){1});
        check_not_null((const void *)false_sum);
        check_not_null((const void *)true_sum);
        check_equal(*false_sum, 3);
        check_equal(*true_sum, 3);
        turbo_map_destroy(&output);
    }

    it("replaces partition-reduce bucket values when reducer is NULL") {
        turbo_map_t output;
        stream_t stream;
        const int *odd_value;

        check_equal(
            turbo_map_init(&output, sizeof(uint8_t), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(STREAM_OF(&stream, int, 1, 3, 5), STREAM_OK);
        check_equal(
            stream_collect_turbo_partition_reduce(
                &stream,
                &output,
                2,
                sizeof(int),
                int_is_even,
                identity_mapper,
                NULL),
            STREAM_END);
        odd_value = (const int *)turbo_map_get_const(&output, &(uint8_t){0});
        check_not_null((const void *)odd_value);
        check_equal(*odd_value, 5);
        turbo_map_destroy(&output);
    }

    it("returns full for partition-count collection at distinct-key budget") {
        stream_array_source_state_t source_state;
        turbo_map_t output;
        stream_t stream;
        stream_result_t init_result;
        int source_values[] = {1, 2, 3};

        check_equal(
            turbo_map_init(&output, sizeof(uint8_t), sizeof(size_t), NULL, NULL, NULL),
            TURBO_OK);
        init_result = STREAM_ARRAY_INIT(&stream, &source_state, source_values);
        check_equal(init_result, STREAM_OK);
        check_equal(
            stream_collect_turbo_partition_count(
                &stream, &output, 1, int_is_even),
            STREAM_FULL);
        check_equal(turbo_map_size(&output), 1);
        check_equal(source_state.pos, 1);
        turbo_map_destroy(&output);
    }

    it("returns full for partition-reduce collection at distinct-key budget") {
        stream_array_source_state_t source_state;
        turbo_map_t output;
        stream_t stream;
        stream_result_t init_result;
        int source_values[] = {1, 3, 2};

        check_equal(
            turbo_map_init(&output, sizeof(uint8_t), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        init_result = STREAM_ARRAY_INIT(&stream, &source_state, source_values);
        check_equal(init_result, STREAM_OK);
        check_equal(
            stream_collect_turbo_partition_reduce(
                &stream,
                &output,
                1,
                sizeof(int),
                int_is_even,
                identity_mapper,
                NULL),
            STREAM_FULL);
        check_equal(turbo_map_size(&output), 1);
        check_equal(source_state.pos, 1);
        turbo_map_destroy(&output);
    }

    it("records reducer errors for partition-reduce buckets") {
        turbo_map_t output;
        stream_t stream;
        stream_array_source_state_t source_state;
        int values[] = {1, 3, 5};

        check_equal(
            turbo_map_init(&output, sizeof(uint8_t), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
        check_equal(
            stream_collect_turbo_partition_reduce(
                &stream,
                &output,
                4,
                sizeof(int),
                int_is_even,
                value_as_one,
                fail_reducer),
            STREAM_ERROR);
        check_equal(stream.error, STREAM_ERR_REDUCE_FAILED);
        check_equal(source_state.pos, 2);
        turbo_map_destroy(&output);
    }

    it("records value mapping errors for partition-reduce buckets") {
        turbo_map_t output;
        stream_t stream;
        stream_array_source_state_t source_state;
        stream_result_t init_result;
        int source_value[] = {1};

        check_equal(
            turbo_map_init(&output, sizeof(uint8_t), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        init_result = STREAM_ARRAY_INIT(&stream, &source_state, source_value);
        check_equal(init_result, STREAM_OK);
        check_equal(
            stream_collect_turbo_partition_reduce(
                &stream,
                &output,
                4,
                sizeof(int),
                int_is_even,
                fail_value_mapper,
                sum_reducer),
            STREAM_ERROR);
        check_equal(stream.error, STREAM_ERR_COLLECT_FAILED);
        check_equal(source_state.pos, 1);
        turbo_map_destroy(&output);
    }

    it("rejects partition-reduce collection when the output map shape is invalid") {
        turbo_map_t output;
        stream_t stream;

        check_equal(
            turbo_map_init(&output, sizeof(int), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(STREAM_OF(&stream, int, 1, 2, 3), STREAM_OK);
        check_equal(
            stream_collect_turbo_partition_reduce(
                &stream,
                &output,
                2,
                sizeof(int),
                int_is_even,
                value_as_one,
                sum_reducer),
            STREAM_ERROR);
        check_equal(stream.error, STREAM_ERR_BAD_ARGUMENT);
        turbo_map_destroy(&output);
    }

    it("rejects turbo partition-count collection when the output map shape is invalid") {
        turbo_map_t output;
        stream_t stream;

        check_equal(
            turbo_map_init(&output, sizeof(int), sizeof(size_t), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(STREAM_OF(&stream, int, 1, 2, 3), STREAM_OK);
        check_equal(
            stream_collect_turbo_partition_count(
                &stream, &output, 2, int_is_even),
            STREAM_ERROR);
        check_equal(stream.error, STREAM_ERR_BAD_ARGUMENT);
        turbo_map_destroy(&output);
    }

    it("collects a stream into a TurboUtils map by mapped key and value with reduce") {
        turbo_map_t output;
        stream_t stream;
        const int *odd_sum;
        const int *even_sum;

        check_equal(
            turbo_map_init(&output, sizeof(int), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(STREAM_OF(&stream, int, 1, 2, 3, 4, 5, 6), STREAM_OK);
        check_equal(
            stream_collect_turbo_map(
                &stream,
                &output,
                4,
                sizeof(int),
                sizeof(int),
                key_by_remainder_two,
                value_as_one,
                sum_reducer),
            STREAM_END);
        check_equal(turbo_map_size(&output), 2);
        odd_sum = (const int *)turbo_map_get_const(&output, &(int){1});
        even_sum = (const int *)turbo_map_get_const(&output, &(int){0});
        check_not_null((const void *)odd_sum);
        check_not_null((const void *)even_sum);
        check_equal(*odd_sum, 3);
        check_equal(*even_sum, 3);
        turbo_map_destroy(&output);
    }

    it("collects a stream into a TurboUtils multimap by mapped key and value") {
        turbo_multimap_t output;
        stream_t stream;
        const turbo_vec_t *even_values;
        const turbo_vec_t *odd_values;
        const int *value;

        check_equal(
            turbo_multimap_init(&output, sizeof(int), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(STREAM_OF(&stream, int, 10, 11, 20, 31, 40), STREAM_OK);
        check_equal(
            stream_collect_turbo_multimap(
                &stream,
                &output,
                10,
                sizeof(int),
                sizeof(int),
                key_by_remainder_two,
                identity_mapper),
            STREAM_END);

        even_values = turbo_multimap_get_values_const(&output, &(int){0});
        odd_values = turbo_multimap_get_values_const(&output, &(int){1});
        check_not_null((const void *)even_values);
        check_not_null((const void *)odd_values);
        check_equal(turbo_vec_size(even_values), 3);
        check_equal(turbo_vec_size(odd_values), 2);

        value = (const int *)turbo_vec_at_const(even_values, 0);
        check_not_null((const void *)value);
        check_equal(value[0], 10);
        value = (const int *)turbo_vec_at_const(even_values, 1);
        check_not_null((const void *)value);
        check_equal(value[0], 20);
        value = (const int *)turbo_vec_at_const(even_values, 2);
        check_not_null((const void *)value);
        check_equal(value[0], 40);

        value = (const int *)turbo_vec_at_const(odd_values, 0);
        check_not_null((const void *)value);
        check_equal(value[0], 11);
        value = (const int *)turbo_vec_at_const(odd_values, 1);
        check_not_null((const void *)value);
        check_equal(value[0], 31);

        check_equal(turbo_multimap_size(&output), 5);
        turbo_multimap_destroy(&output);
    }

    it("replaces values for duplicate keys when no reducer is provided") {
        turbo_map_t output;
        stream_t stream;
        const int *odd_value;

        check_equal(
            turbo_map_init(&output, sizeof(int), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(STREAM_OF(&stream, int, 1, 3, 5), STREAM_OK);
        check_equal(
            stream_collect_turbo_map(
                &stream,
                &output,
                4,
                sizeof(int),
                sizeof(int),
                key_by_remainder_two,
                identity_mapper,
                NULL),
            STREAM_END);
        odd_value = (const int *)turbo_map_get_const(&output, &(int){1});
        check_not_null((const void *)odd_value);
        check_equal(*odd_value, 5);
        turbo_map_destroy(&output);
    }

    it("keeps first value for duplicate keys when KEEP_FIRST mode is set") {
        turbo_map_t output;
        stream_t stream;
        const int *odd_value;

        check_equal(
            turbo_map_init(&output, sizeof(int), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(STREAM_OF(&stream, int, 1, 3, 5), STREAM_OK);
        check_equal(
            stream_collect_turbo_map_with_conflict(
                &stream,
                &output,
                4,
                sizeof(int),
                sizeof(int),
                STREAM_TURBO_MAP_KEEP_FIRST,
                key_by_remainder_two,
                identity_mapper,
                NULL),
            STREAM_END);
        odd_value = (const int *)turbo_map_get_const(&output, &(int){1});
        check_not_null((const void *)odd_value);
        check_equal(*odd_value, 1);
        turbo_map_destroy(&output);
    }

    it("keeps last value for duplicate keys when KEEP_LAST mode is explicitly set") {
        turbo_map_t output;
        stream_t stream;
        const int *odd_value;

        check_equal(
            turbo_map_init(&output, sizeof(int), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(STREAM_OF(&stream, int, 1, 3, 5), STREAM_OK);
        check_equal(
            stream_collect_turbo_map_with_conflict(
                &stream,
                &output,
                4,
                sizeof(int),
                sizeof(int),
                STREAM_TURBO_MAP_KEEP_LAST,
                key_by_remainder_two,
                identity_mapper,
                sum_reducer),
            STREAM_END);
        odd_value = (const int *)turbo_map_get_const(&output, &(int){1});
        check_not_null((const void *)odd_value);
        check_equal(*odd_value, 5);
        turbo_map_destroy(&output);
    }

    it("rejects duplicate keys when REJECT mode is set") {
        turbo_map_t output;
        stream_t stream;
        stream_array_source_state_t source_state;
        int values[] = {1, 3, 5, 2};

        check_equal(
            turbo_map_init(&output, sizeof(int), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(
            STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
        check_equal(
            stream_collect_turbo_map_with_conflict(
                &stream,
                &output,
                4,
                sizeof(int),
                sizeof(int),
                STREAM_TURBO_MAP_REJECT,
                key_by_remainder_two,
                identity_mapper,
                NULL),
            STREAM_ERROR);
        check_equal(stream.error, STREAM_ERR_BAD_ARGUMENT);
        check_equal(turbo_map_size(&output), 1);
        check_equal(source_state.pos, 2);
        turbo_map_destroy(&output);
    }

    it("requires a reducer when MERGE mode is selected") {
        turbo_map_t output;
        stream_t stream;
        stream_array_source_state_t source_state;
        int values[] = {1, 3};

        check_equal(
            turbo_map_init(&output, sizeof(int), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
        check_equal(
            stream_collect_turbo_map_with_conflict(
                &stream,
                &output,
                4,
                sizeof(int),
                sizeof(int),
                STREAM_TURBO_MAP_MERGE,
                key_by_remainder_two,
                identity_mapper,
                NULL),
            STREAM_ERROR);
        check_equal(stream.error, STREAM_ERR_BAD_ARGUMENT);
        check_equal(source_state.pos, 0);
        turbo_map_destroy(&output);
    }

    it("respects hard item limits while collecting into a TurboUtils multimap") {
        stream_array_source_state_t source_state;
        turbo_multimap_t output;
        stream_t stream;
        int values[] = {1, 3, 5};

        check_equal(
            turbo_multimap_init(&output, sizeof(int), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
        check_equal(
            stream_collect_turbo_multimap(
                &stream,
                &output,
                2,
                sizeof(int),
                sizeof(int),
                key_by_remainder_two,
                value_as_one),
            STREAM_FULL);
        check_equal(turbo_multimap_size(&output), 2);
        check_equal(source_state.pos, 2);
        turbo_multimap_destroy(&output);
    }

    it("records multimap collector callback errors") {
        turbo_multimap_t output;
        stream_t stream;
        stream_array_source_state_t source_state;
        stream_result_t init_result;
        int source_value[] = {1};

        check_equal(
            turbo_multimap_init(&output, sizeof(int), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        init_result = STREAM_ARRAY_INIT(&stream, &source_state, source_value);
        check_equal(init_result, STREAM_OK);
        check_equal(
            stream_collect_turbo_multimap(
                &stream,
                &output,
                4,
                sizeof(int),
                sizeof(int),
                fail_key_selector,
                identity_mapper),
            STREAM_ERROR);
        check_equal(stream.error, STREAM_ERR_COLLECT_FAILED);
        check_equal(source_state.pos, 1);
        check_equal(
            turbo_multimap_init(&output, sizeof(int), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);

        init_result = STREAM_ARRAY_INIT(&stream, &source_state, source_value);
        check_equal(init_result, STREAM_OK);
        check_equal(
            stream_collect_turbo_multimap(
                &stream,
                &output,
                4,
                sizeof(int),
                sizeof(int),
                key_by_identity,
                fail_value_mapper),
            STREAM_ERROR);
        check_equal(stream.error, STREAM_ERR_COLLECT_FAILED);
        check_equal(source_state.pos, 1);
        turbo_multimap_destroy(&output);
    }

    it("records reduce callback errors for turbo map collector") {
        turbo_map_t output;
        stream_t stream;
        stream_array_source_state_t source_state;
        int values[] = {1, 2, 3};

        check_equal(
            turbo_map_init(&output, sizeof(int), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
        check_equal(
            stream_collect_turbo_map(
                &stream,
                &output,
                4,
                sizeof(int),
                sizeof(int),
                key_by_remainder_two,
                value_as_one,
                fail_reducer),
            STREAM_ERROR);
        check_equal(stream.error, STREAM_ERR_REDUCE_FAILED);
        check_equal(source_state.pos, 3);
        turbo_map_destroy(&output);
    }

    it("records turbo map collector callback errors for key mapping and value mapping") {
        turbo_map_t output;
        stream_t stream;
        stream_array_source_state_t source_state;
        stream_result_t init_result;
        int source_value[] = {1};

        check_equal(
            turbo_map_init(&output, sizeof(int), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        init_result = STREAM_ARRAY_INIT(&stream, &source_state, source_value);
        check_equal(init_result, STREAM_OK);
        check_equal(
            stream_collect_turbo_map(
                &stream,
                &output,
                4,
                sizeof(int),
                sizeof(int),
                fail_key_selector,
                identity_mapper,
                NULL),
            STREAM_ERROR);
        check_equal(stream.error, STREAM_ERR_COLLECT_FAILED);
        check_equal(source_state.pos, 1);

        init_result = STREAM_ARRAY_INIT(&stream, &source_state, source_value);
        check_equal(init_result, STREAM_OK);
        check_equal(
            stream_collect_turbo_map(
                &stream,
                &output,
                4,
                sizeof(int),
                sizeof(int),
                key_by_identity,
                fail_value_mapper,
                NULL),
            STREAM_ERROR);
        check_equal(stream.error, STREAM_ERR_COLLECT_FAILED);
        check_equal(source_state.pos, 1);
        turbo_map_destroy(&output);
    }

    it("returns full for turbo map collector at hard distinct-key limit") {
        turbo_map_t output;
        stream_t stream;
        stream_array_source_state_t source_state;
        int values[] = {1, 2, 3};

        check_equal(
            turbo_map_init(&output, sizeof(int), sizeof(int), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
        check_equal(
            stream_collect_turbo_map(
                &stream,
                &output,
                2,
                sizeof(int),
                sizeof(int),
                key_by_identity,
                identity_mapper,
                NULL),
            STREAM_FULL);
        check_equal(turbo_map_size(&output), 2);
        check_equal(source_state.pos, 2);
        turbo_map_destroy(&output);
    }

    it("records full when map-count collector reaches the hard distinct-key limit") {
        turbo_map_t output;
        stream_array_source_state_t source_state;
        stream_t stream;
        int values[] = {1, 3, 2};

        check_equal(
            turbo_map_init(&output, sizeof(int), sizeof(size_t), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(
            STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
        check_equal(
            stream_collect_turbo_map_count(
                &stream, &output, 1, sizeof(int), key_by_remainder_two),
            STREAM_FULL);
        check_equal(turbo_map_size(&output), 1);
        check_equal(source_state.pos, 1);
        turbo_map_destroy(&output);
    }

    it("records map count collector callback errors") {
        turbo_map_t output;
        stream_t stream;

        check_equal(
            turbo_map_init(&output, sizeof(int), sizeof(size_t), NULL, NULL, NULL),
            TURBO_OK);
        check_equal(STREAM_OF(&stream, int, 1, 2), STREAM_OK);
        check_equal(
            stream_collect_turbo_map_count(
                &stream, &output, 4, sizeof(int), fail_key_selector),
            STREAM_ERROR);
        check_equal(stream.error, STREAM_ERR_COLLECT_FAILED);
        turbo_map_destroy(&output);
    }

    it("returns full for turbo set collection at hard limit without over-consuming") {
        int values[] = {1, 2, 3};
        stream_array_source_state_t source_state;
        turbo_set_t output;
        stream_t stream;

        check_equal(turbo_set_init(&output, sizeof(int), NULL, NULL, NULL), TURBO_OK);
        check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
        check_equal(stream_collect_turbo_set(&stream, &output, 2), STREAM_FULL);
        check_equal(turbo_set_size(&output), 2);
        check_equal(source_state.pos, 2);
        turbo_set_destroy(&output);
    }

    it("returns full for turbo list collection at hard limit without over-consuming") {
        int values[] = {1, 2, 3};
        stream_array_source_state_t source_state;
        turbo_list_t output;
        stream_t stream;
        const int *item;

        check_equal(turbo_list_init(&output, sizeof(int)), TURBO_OK);
        check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
        check_equal(
            stream_collect_turbo_list(&stream, &output, 2),
            STREAM_FULL);
        check_equal(turbo_list_size(&output), 2);
        item = (const int *)turbo_list_front_const(&output);
        check_not_null((const void *)item);
        check_equal(item[0], 1);
        item = (const int *)turbo_list_at_const(&output, 1);
        check_not_null((const void *)item);
        check_equal(item[0], 2);
        check_equal(source_state.pos, 2);
        turbo_list_destroy(&output);
    }

    it("rejects list collection when the destination already exceeds max_items") {
        int seed[] = {1, 2};
        int values[] = {3, 4};
        stream_array_source_state_t source_state;
        turbo_list_t output;
        stream_t stream;

        check_equal(turbo_list_from_array(&output, seed, 2, sizeof(seed[0])), TURBO_OK);
        check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
        check_equal(
            stream_collect_turbo_list(&stream, &output, 1),
            STREAM_ERROR);
        check_equal(stream.error, STREAM_ERR_BAD_ARGUMENT);
        check_equal(turbo_list_size(&output), 2);
        check_equal(source_state.pos, 0);
        turbo_list_destroy(&output);
    }

    it("stops collection at its hard limit without over-consuming") {
        int values[] = {1, 2, 3};
        stream_array_source_state_t source_state;
        turbo_vec_t output;
        stream_t stream;

        check_equal(turbo_vec_init(&output, sizeof(int)), TURBO_OK);
        check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
        check_equal(
            stream_collect_turbo_vec(&stream, &output, 2),
            STREAM_FULL);
        check_equal(turbo_vec_size(&output), 2);
        check_equal(source_state.pos, 2);
        turbo_vec_destroy(&output);
    }

    it("rejects collect reserve overflow before consuming the source") {
        int values[] = {1, 2};
        stream_array_source_state_t source_state;
        turbo_vec_t output;
        stream_t stream;

        check_equal(turbo_vec_init(&output, sizeof(int)), TURBO_OK);
        check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
        check_equal(
            stream_collect_turbo_vec(&stream, &output, SIZE_MAX),
            STREAM_ERROR);
        check_equal(stream.error, STREAM_ERR_COLLECT_FAILED);
        check_equal(source_state.pos, 0);
        check_equal(turbo_vec_size(&output), 0);
        turbo_vec_destroy(&output);
    }
}
