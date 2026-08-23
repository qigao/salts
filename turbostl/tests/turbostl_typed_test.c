#include <turbostl/typed.h>
#include <cmeta/entry.h>
#include "tinytest.h"

#include <stdint.h>
#include <string.h>

#define VERIFY_EMPTY_COLLECTOR(instance, size_fn, destroy_fn) do {            \
    const cmeta_container_desc *desc__ = cmeta_container_descriptor(&(instance)); \
    cmeta_collector collector__;                                              \
    check_not_null(desc__);                                                   \
    check_not_null(desc__->collector);                                        \
    collector__ = desc__->collector(&(instance), 2u);                         \
    check_equal(cmeta_collector_begin(&collector__), CMETA_OK);               \
    check_equal(cmeta_collector_finish(&collector__), CMETA_OK);              \
    check_equal(size_fn(&(instance)), (size_t)0u);                            \
    check_true(cmeta_container_descriptor(&(instance)) == desc__);            \
    destroy_fn(&(instance));                                                  \
    check_equal(size_fn(&(instance)), (size_t)0u);                            \
    check_true(cmeta_container_descriptor(&(instance)) == desc__);            \
} while (0)

#define VERIFY_C1_COLLECTOR(instance, size_fn, destroy_fn, Value) do {        \
    const cmeta_container_desc *desc__ = cmeta_container_descriptor(&(instance)); \
    int first__ = (Value);                                                    \
    int second__ = (Value) + 1;                                               \
    cmeta_collector collector__;                                              \
    check_not_null(desc__);                                                   \
    check_not_null(desc__->collector);                                        \
    collector__ = desc__->collector(&(instance), 1u);                         \
    check_equal(cmeta_collector_begin(&collector__), CMETA_OK);               \
    check_equal(cmeta_collector_accept(&collector__, collector__.input_type,  \
                                       &first__), CMETA_OK);                   \
    check_equal(cmeta_collector_finish(&collector__), CMETA_OK);              \
    check_equal(size_fn(&(instance)), (size_t)1u);                            \
    destroy_fn(&(instance));                                                  \
    check_equal(size_fn(&(instance)), (size_t)0u);                            \
    check_true(cmeta_container_descriptor(&(instance)) == desc__);            \
    collector__ = desc__->collector(&(instance), 1u);                         \
    check_equal(cmeta_collector_begin(&collector__), CMETA_OK);               \
    check_equal(cmeta_collector_accept(&collector__, collector__.input_type,  \
                                       &first__), CMETA_OK);                   \
    check_equal(cmeta_collector_accept(&collector__, collector__.input_type,  \
                                       &second__), CMETA_CAPACITY_EXCEEDED);  \
    check_true(collector__.state == CMETA_COLLECTOR_ABORTED);                 \
    check_equal(size_fn(&(instance)), (size_t)0u);                            \
    check_true(cmeta_container_descriptor(&(instance)) == desc__);            \
    cmeta_collector_abort(&collector__);                                      \
    check_true(collector__.state == CMETA_COLLECTOR_ABORTED);                 \
    check_equal(size_fn(&(instance)), (size_t)0u);                            \
} while (0)

#define VERIFY_C2_COLLECTOR(instance, size_fn, destroy_fn, Key, Value) do {   \
    const cmeta_container_desc *desc__ = cmeta_container_descriptor(&(instance)); \
    int first_key__ = (Key);                                                  \
    int second_key__ = (Key) + 1;                                            \
    long first_value__ = (Value);                                             \
    long second_value__ = (Value) + 1L;                                      \
    cmeta_entry first__ = {                                                   \
        .key_type = (instance).key_type,                                      \
        .value_type = (instance).value_type,                                  \
        .key = &first_key__,                                                  \
        .value = &first_value__,                                              \
        .key_storage = NULL,                                                  \
        .value_storage = NULL};                                               \
    cmeta_entry second__ = {                                                  \
        .key_type = (instance).key_type,                                      \
        .value_type = (instance).value_type,                                  \
        .key = &second_key__,                                                 \
        .value = &second_value__,                                             \
        .key_storage = NULL,                                                  \
        .value_storage = NULL};                                               \
    cmeta_collector collector__;                                              \
    check_not_null(desc__);                                                   \
    check_not_null(desc__->collector);                                        \
    collector__ = desc__->collector(&(instance), 1u);                         \
    check_equal(cmeta_collector_begin(&collector__), CMETA_OK);               \
    check_equal(cmeta_collector_accept(&collector__, collector__.input_type,  \
                                       &first__), CMETA_OK);                   \
    check_equal(cmeta_collector_finish(&collector__), CMETA_OK);              \
    check_equal(size_fn(&(instance)), (size_t)1u);                            \
    destroy_fn(&(instance));                                                  \
    check_equal(size_fn(&(instance)), (size_t)0u);                            \
    check_true(cmeta_container_descriptor(&(instance)) == desc__);            \
    collector__ = desc__->collector(&(instance), 1u);                         \
    check_equal(cmeta_collector_begin(&collector__), CMETA_OK);               \
    check_equal(cmeta_collector_accept(&collector__, collector__.input_type,  \
                                       &first__), CMETA_OK);                   \
    check_equal(cmeta_collector_accept(&collector__, collector__.input_type,  \
                                       &second__), CMETA_CAPACITY_EXCEEDED);  \
    check_true(collector__.state == CMETA_COLLECTOR_ABORTED);                 \
    check_equal(size_fn(&(instance)), (size_t)0u);                            \
    check_true(cmeta_container_descriptor(&(instance)) == desc__);            \
    cmeta_collector_abort(&collector__);                                      \
    check_true(collector__.state == CMETA_COLLECTOR_ABORTED);                 \
    check_equal(size_fn(&(instance)), (size_t)0u);                            \
} while (0)

spec("TurboSTL instance metadata") {
    it("binds all thirteen standard kinds without generated facade types") {
        Vec(int, vector);
        Deque(int, deque);
        List(int, list);
        Stack(int, stack);
        Queue(int, queue);
        Heap(int, heap);
        Set(int, set);
        HashSet(int, hash_set);
        HashMap(int, long, hash_map);
        Map(int, long, map);
        MultiMap(int, long, multimap);
        BTree(int, long, btree);
        BPlusTree(int, long, bplus);
        const void *instances[] = {
            &vector, &deque, &list, &stack, &queue, &heap, &set,
            &hash_set, &hash_map, &map, &multimap, &btree, &bplus};
        size_t index;

        _Static_assert(sizeof(instances) / sizeof(instances[0]) == 13u,
                       "TURBOSTL_STANDARD_INSTANCE_KIND_COUNT_MISMATCH");
        for (index = 0u; index < sizeof(instances) / sizeof(instances[0]); ++index) {
            const cmeta_container_desc *desc =
                cmeta_container_descriptor(instances[index]);
            check_not_null(desc);
            check_not_null(desc->range);
            check_not_null(desc->collector);
        }
        check_not_null(cmeta_container_descriptor(&hash_map)->keys_range);
        check_not_null(cmeta_container_descriptor(&hash_map)->values_range);
        check_not_null(cmeta_container_descriptor(&hash_map)->entries_range);
        check_not_null(cmeta_container_descriptor(&map)->keys_range);
        check_not_null(cmeta_container_descriptor(&multimap)->entries_range);
        check_not_null(cmeta_container_descriptor(&btree)->entries_range);
        check_not_null(cmeta_container_descriptor(&bplus)->entries_range);
    }

    it("exposes a bounded sequence Range and transactional collector") {
        Vec(int, source);
        Vec(int, output);
        cmeta_range range = {0};
        cmeta_collector collector;
        cmeta_range_cursor cursor = {0};
        int value = 4;
        int ranged = 0;

        check_equal(vec_init(&source, 2u), STL_OK);
        check_equal(vec_push(&source, &value), STL_OK);
        check_true(cmeta_container_range_view(&source, CMETA_CONTAINER_VIEW_DEFAULT,
                                              &range));
        check_true(cmeta_type_equal(range.element_type, &cmeta_type_int));
        check_true((range.flags & (CMETA_RANGE_ORDERED | CMETA_RANGE_SIZED |
                                  CMETA_RANGE_CONTIGUOUS)) ==
                   (CMETA_RANGE_ORDERED | CMETA_RANGE_SIZED |
                    CMETA_RANGE_CONTIGUOUS));
        check_equal(cmeta_range_next(&range, &cursor, &ranged),
                    CMETA_GEN_VALUE_AND_DONE);
        check_equal(ranged, 4);

        collector = cmeta_container_descriptor(&output)->collector(&output, 1u);
        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &value),
                    CMETA_OK);
        check_equal(cmeta_collector_finish(&collector), CMETA_OK);
        check_equal(vec_size(&output), (size_t)1u);
        check_equal(*(const int *)vec_at_const(&output, 0u), 4);
        vec_destroy(&output);
        vec_destroy(&source);
    }

    it("invalidates an existing Range without changing cursor or output") {
        Deque(int, values);
        cmeta_range range = {0};
        cmeta_range_cursor cursor = {0};
        cmeta_range_cursor before_cursor;
        int one = 1;
        int two = 2;
        int output = 91;

        check_equal(deque_init(&values, 2u), STL_OK);
        check_equal(deque_push_back(&values, &one), STL_OK);
        check_true(cmeta_container_range_view(&values,
                                              CMETA_CONTAINER_VIEW_DEFAULT,
                                              &range));
        check_equal(deque_push_back(&values, &two), STL_OK);
        before_cursor = cursor;
        check_equal(cmeta_range_next(&range, &cursor, &output),
                    CMETA_GEN_MUTATED);
        check_equal(memcmp(&cursor, &before_cursor, sizeof(cursor)), 0);
        check_equal(output, 91);
        deque_destroy(&values);
    }

    it("exposes default entries plus key value and entry views for maps") {
        HashMap(int, long, map);
        cmeta_range default_range = {0};
        cmeta_range keys = {0};
        cmeta_range values = {0};
        cmeta_range entries = {0};
        cmeta_entry entry = {0};
        cmeta_range_cursor cursor = {0};
        int key = 7;
        long value = 70L;

        check_equal(hash_map_init(&map, 1u), STL_OK);
        check_equal(hash_map_put(&map, &key, &value), STL_OK);
        check_true(cmeta_container_range_view(&map, CMETA_CONTAINER_VIEW_DEFAULT,
                                              &default_range));
        check_true(cmeta_container_range_view(&map, CMETA_CONTAINER_VIEW_KEYS,
                                              &keys));
        check_true(cmeta_container_range_view(&map, CMETA_CONTAINER_VIEW_VALUES,
                                              &values));
        check_true(cmeta_container_range_view(&map, CMETA_CONTAINER_VIEW_ENTRIES,
                                              &entries));
        check_true(cmeta_type_equal(default_range.element_type,
                                    entries.element_type));
        check_true((keys.flags & CMETA_RANGE_UNIQUE) != 0u);
        check_equal(cmeta_range_next(&default_range, &cursor, &entry),
                    CMETA_GEN_VALUE);
        check_true(cmeta_type_equal(entry.key_type, &cmeta_type_int));
        check_true(cmeta_type_equal(entry.value_type, &cmeta_type_long));
        check_equal(*(const int *)entry.key, 7);
        check_equal(*(const long *)entry.value, 70L);
        check_equal(entry.key_storage, NULL);
        check_equal(entry.value_storage, NULL);
        hash_map_destroy(&map);
    }

    it("propagates explicit limits through every instance initializer") {
        Heap(int, heap);
        Set(int, set);
        Map(int, long, map);
        MultiMap(int, long, multimap);
        int one = 1;
        long value = 1L;

        check_equal(heap_init(&heap, 0u), STL_OK);
        check_equal(heap_push(&heap, &one), STL_CAPACITY_EXCEEDED);
        check_equal(set_init(&set, 0u), STL_OK);
        check_equal(set_add(&set, &one), STL_CAPACITY_EXCEEDED);
        check_equal(map_init(&map, 0u), STL_OK);
        check_equal(map_put(&map, &one, &value), STL_CAPACITY_EXCEEDED);
        check_equal(multimap_init(&multimap, 0u), STL_OK);
        check_equal(multimap_put(&multimap, &one, &value),
                    STL_CAPACITY_EXCEEDED);
        multimap_destroy(&multimap);
        map_destroy(&map);
        set_destroy(&set);
        heap_destroy(&heap);
    }

    it("requires explicit from limits and commits associative inputs once") {
        Vec(int, vector);
        HashMap(int, long, map);
        MultiMap(int, long, multimap);
        BPlusTree(int, long, tree);
        int values[] = {1, 2};
        int map_keys[] = {1, 2};
        long map_values[] = {10L, 20L};
        int multi_keys[] = {1, 1};
        long multi_values[] = {10L, 11L};
        int tree_keys[] = {2, 1};
        long tree_values[] = {20L, 10L};
        int lookup = 2;
        int multi_key = 1;
        int tree_key = 1;
        uint64_t generation;

        check_equal(vec_from_array(&vector, values, 2u, 1u),
                    STL_CAPACITY_EXCEEDED);
        check_equal(vec_size(&vector), (size_t)0u);
        check_true(cmeta_container_descriptor(&vector) == &stl_vec_container_desc);
        check_equal(vec_from_array(&vector, values, 2u, 2u), STL_OK);
        check_equal(vec_size(&vector), (size_t)2u);

        check_equal(hash_map_from_arrays(&map, map_keys, map_values, 2u, 2u),
                    STL_OK);
        check_equal(*(const long *)hash_map_get_const(&map, &lookup), 20L);
        generation = hash_map_generation(&map);
        check_equal(hash_map_from_arrays(&map, map_keys, map_values, 2u, 1u),
                    STL_CAPACITY_EXCEEDED);
        check_equal(hash_map_generation(&map), generation);
        check_equal(*(const long *)hash_map_get_const(&map, &lookup), 20L);

        check_equal(multimap_from_arrays(&multimap, multi_keys, multi_values,
                                         2u, 1u), STL_CAPACITY_EXCEEDED);
        check_true(multimap_empty(&multimap));
        check_equal(multimap_from_arrays(&multimap, multi_keys, multi_values,
                                         2u, 2u), STL_OK);
        check_equal(multimap_count(&multimap, &multi_key), (size_t)2u);

        check_equal(bplus_tree_from_arrays(&tree, tree_keys, tree_values,
                                           2u, 2u), STL_OK);
        check_equal(*(const long *)bplus_tree_get_const(&tree, &tree_key), 10L);

        bplus_tree_destroy(&tree);
        multimap_destroy(&multimap);
        hash_map_destroy(&map);
        vec_destroy(&vector);
    }

    it("counts live keys rather than input rows when building associative containers") {
        HashMap(int, long, hash_map);
        Map(int, long, map);
        BTree(int, long, btree);
        BPlusTree(int, long, bplus);
        int duplicate_keys[] = {1, 1};
        int distinct_keys[] = {2, 3};
        long hash_values[] = {10L, 11L};
        long map_values[] = {20L, 21L};
        long btree_values[] = {30L, 31L};
        long bplus_values[] = {40L, 41L};
        long hash_distinct[] = {12L, 13L};
        long map_distinct[] = {22L, 23L};
        long btree_distinct[] = {32L, 33L};
        long bplus_distinct[] = {42L, 43L};
        int key = 1;

        check_equal(hash_map_from_arrays(&hash_map, duplicate_keys, hash_values,
                                         2u, 1u), STL_OK);
        check_equal(hash_map_size(&hash_map), (size_t)1u);
        check_equal(*(const long *)hash_map_get_const(&hash_map, &key), 11L);
        check_equal(hash_map_generation(&hash_map), UINT64_C(1));

        check_equal(map_from_arrays(&map, duplicate_keys, map_values, 2u, 1u),
                    STL_OK);
        check_equal(map_size(&map), (size_t)1u);
        check_equal(*(const long *)map_get_const(&map, &key), 21L);
        check_equal(map_generation(&map), UINT64_C(1));

        check_equal(btree_from_arrays(&btree, duplicate_keys, btree_values,
                                      2u, 1u), STL_OK);
        check_equal(btree_size(&btree), (size_t)1u);
        check_equal(*(const long *)btree_get_const(&btree, &key), 31L);
        check_equal(btree_generation(&btree), UINT64_C(1));

        check_equal(bplus_tree_from_arrays(&bplus, duplicate_keys, bplus_values,
                                           2u, 1u), STL_OK);
        check_equal(bplus_tree_size(&bplus), (size_t)1u);
        check_equal(*(const long *)bplus_tree_get_const(&bplus, &key), 41L);
        check_equal(bplus_tree_generation(&bplus), UINT64_C(1));

        check_equal(hash_map_from_arrays(&hash_map, distinct_keys, hash_distinct,
                                         2u, 1u), STL_CAPACITY_EXCEEDED);
        check_equal(hash_map_generation(&hash_map), UINT64_C(1));
        check_equal(*(const long *)hash_map_get_const(&hash_map, &key), 11L);
        check_equal(map_from_arrays(&map, distinct_keys, map_distinct, 2u, 1u),
                    STL_CAPACITY_EXCEEDED);
        check_equal(map_generation(&map), UINT64_C(1));
        check_equal(*(const long *)map_get_const(&map, &key), 21L);
        check_equal(btree_from_arrays(&btree, distinct_keys, btree_distinct,
                                      2u, 1u), STL_CAPACITY_EXCEEDED);
        check_equal(btree_generation(&btree), UINT64_C(1));
        check_equal(*(const long *)btree_get_const(&btree, &key), 31L);
        check_equal(bplus_tree_from_arrays(&bplus, distinct_keys, bplus_distinct,
                                           2u, 1u), STL_CAPACITY_EXCEEDED);
        check_equal(bplus_tree_generation(&bplus), UINT64_C(1));
        check_equal(*(const long *)bplus_tree_get_const(&bplus, &key), 41L);

        bplus_tree_destroy(&bplus);
        btree_destroy(&btree);
        map_destroy(&map);
        hash_map_destroy(&hash_map);
    }

    it("aborts a collector exactly once and restores the bound empty instance") {
        Vec(int, output);
        const cmeta_container_desc *desc = cmeta_container_descriptor(&output);
        cmeta_collector collector = desc->collector(&output, 1u);
        int one = 1;
        int two = 2;

        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &one),
                    CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &two),
                    CMETA_CAPACITY_EXCEEDED);
        check_true(collector.state == CMETA_COLLECTOR_ABORTED);
        check_equal(vec_size(&output), (size_t)0u);
        check_true(cmeta_container_descriptor(&output) == desc);
        check_true(cmeta_type_equal(output.element_type, &cmeta_type_int));
        cmeta_collector_abort(&collector);
        check_true(collector.state == CMETA_COLLECTOR_ABORTED);
        check_equal(vec_size(&output), (size_t)0u);
        check_true(cmeta_container_descriptor(&output) == desc);
    }

    it("exposes an empty committing collector for all standard kinds") {
        Vec(int, vector);
        Deque(int, deque);
        List(int, list);
        Stack(int, stack);
        Queue(int, queue);
        Heap(int, heap);
        Set(int, set);
        HashSet(int, hash_set);
        HashMap(int, long, hash_map);
        Map(int, long, map);
        MultiMap(int, long, multimap);
        BTree(int, long, btree);
        BPlusTree(int, long, bplus);

        VERIFY_EMPTY_COLLECTOR(vector, vec_size, vec_destroy);
        VERIFY_EMPTY_COLLECTOR(deque, deque_size, deque_destroy);
        VERIFY_EMPTY_COLLECTOR(list, list_size, list_destroy);
        VERIFY_EMPTY_COLLECTOR(stack, stack_size, stack_destroy);
        VERIFY_EMPTY_COLLECTOR(queue, queue_size, queue_destroy);
        VERIFY_EMPTY_COLLECTOR(heap, heap_size, heap_destroy);
        VERIFY_EMPTY_COLLECTOR(set, set_size, set_destroy);
        VERIFY_EMPTY_COLLECTOR(hash_set, hash_set_size, hash_set_destroy);
        VERIFY_EMPTY_COLLECTOR(hash_map, hash_map_size, hash_map_destroy);
        VERIFY_EMPTY_COLLECTOR(map, map_size, map_destroy);
        VERIFY_EMPTY_COLLECTOR(multimap, multimap_size, multimap_destroy);
        VERIFY_EMPTY_COLLECTOR(btree, btree_size, btree_destroy);
        VERIFY_EMPTY_COLLECTOR(bplus, bplus_tree_size, bplus_tree_destroy);
    }

    it("executes nonempty commit and overflow abort for all standard kinds") {
        Vec(int, vector);
        Deque(int, deque);
        List(int, list);
        Stack(int, stack);
        Queue(int, queue);
        Heap(int, heap);
        Set(int, set);
        HashSet(int, hash_set);
        HashMap(int, long, hash_map);
        Map(int, long, map);
        MultiMap(int, long, multimap);
        BTree(int, long, btree);
        BPlusTree(int, long, bplus);

        VERIFY_C1_COLLECTOR(vector, vec_size, vec_destroy, 1);
        VERIFY_C1_COLLECTOR(deque, deque_size, deque_destroy, 2);
        VERIFY_C1_COLLECTOR(list, list_size, list_destroy, 3);
        VERIFY_C1_COLLECTOR(stack, stack_size, stack_destroy, 4);
        VERIFY_C1_COLLECTOR(queue, queue_size, queue_destroy, 5);
        VERIFY_C1_COLLECTOR(heap, heap_size, heap_destroy, 6);
        VERIFY_C1_COLLECTOR(set, set_size, set_destroy, 7);
        VERIFY_C1_COLLECTOR(hash_set, hash_set_size, hash_set_destroy, 8);
        VERIFY_C2_COLLECTOR(hash_map, hash_map_size, hash_map_destroy, 1, 10L);
        VERIFY_C2_COLLECTOR(map, map_size, map_destroy, 2, 20L);
        VERIFY_C2_COLLECTOR(multimap, multimap_size, multimap_destroy, 3, 30L);
        VERIFY_C2_COLLECTOR(btree, btree_size, btree_destroy, 4, 40L);
        VERIFY_C2_COLLECTOR(bplus, bplus_tree_size, bplus_tree_destroy, 5, 50L);
    }

    it("collects associative tree and multimap entries transactionally") {
        HashMap(int, long, hash_map);
        BTree(int, long, tree);
        MultiMap(int, long, multimap);
        int hash_key = 3;
        long hash_value = 30L;
        int tree_key = 2;
        long tree_value = 20L;
        int multi_key = 1;
        long multi_value_a = 10L;
        long multi_value_b = 11L;
        cmeta_entry hash_entry = {
            .key_type = hash_map.key_type, .value_type = hash_map.value_type,
            .key = &hash_key, .value = &hash_value,
            .key_storage = NULL, .value_storage = NULL};
        cmeta_entry tree_entry = {
            .key_type = tree.key_type, .value_type = tree.value_type,
            .key = &tree_key, .value = &tree_value,
            .key_storage = NULL, .value_storage = NULL};
        cmeta_entry multi_entries[] = {
            {.key_type = multimap.key_type, .value_type = multimap.value_type,
             .key = &multi_key, .value = &multi_value_a,
             .key_storage = NULL, .value_storage = NULL},
            {.key_type = multimap.key_type, .value_type = multimap.value_type,
             .key = &multi_key, .value = &multi_value_b,
             .key_storage = NULL, .value_storage = NULL}};
        cmeta_collector hash_collector =
            cmeta_container_descriptor(&hash_map)->collector(&hash_map, 1u);
        cmeta_collector tree_collector =
            cmeta_container_descriptor(&tree)->collector(&tree, 1u);
        cmeta_collector multi_collector =
            cmeta_container_descriptor(&multimap)->collector(&multimap, 2u);

        check_equal(cmeta_collector_begin(&hash_collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&hash_collector,
                                           hash_collector.input_type,
                                           &hash_entry), CMETA_OK);
        check_equal(cmeta_collector_finish(&hash_collector), CMETA_OK);
        check_equal(*(const long *)hash_map_get_const(&hash_map, &hash_key), 30L);

        check_equal(cmeta_collector_begin(&tree_collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&tree_collector,
                                           tree_collector.input_type,
                                           &tree_entry), CMETA_OK);
        check_equal(cmeta_collector_finish(&tree_collector), CMETA_OK);
        check_equal(*(const long *)btree_get_const(&tree, &tree_key), 20L);

        check_equal(cmeta_collector_begin(&multi_collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&multi_collector,
                                           multi_collector.input_type,
                                           &multi_entries[0]), CMETA_OK);
        check_equal(cmeta_collector_accept(&multi_collector,
                                           multi_collector.input_type,
                                           &multi_entries[1]), CMETA_OK);
        check_equal(cmeta_collector_finish(&multi_collector), CMETA_OK);
        check_equal(multimap_count(&multimap, &multi_key), (size_t)2u);

        multimap_destroy(&multimap);
        btree_destroy(&tree);
        hash_map_destroy(&hash_map);
    }

    it("handles collector type mismatch limit zero reuse and terminal states") {
        Vec(int, output);
        const cmeta_container_desc *desc = cmeta_container_descriptor(&output);
        cmeta_collector collector = desc->collector(&output, 0u);
        int value = 7;

        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &value),
                    CMETA_CAPACITY_EXCEEDED);
        check_equal(vec_size(&output), (size_t)0u);
        check_true(cmeta_container_descriptor(&output) == desc);
        check_equal(cmeta_collector_finish(&collector), CMETA_INVALID_ARGUMENT);

        collector = desc->collector(&output, 1u);
        collector.input_type = &cmeta_type_long;
        check_equal(cmeta_collector_begin(&collector), CMETA_TYPE_MISMATCH);
        check_equal(vec_size(&output), (size_t)0u);
        check_true(cmeta_container_descriptor(&output) == desc);

        collector = desc->collector(&output, 1u);
        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &value),
                    CMETA_OK);
        check_equal(cmeta_collector_finish(&collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &value),
                    CMETA_INVALID_ARGUMENT);
        check_equal(cmeta_collector_finish(&collector), CMETA_INVALID_ARGUMENT);
        check_equal(vec_size(&output), (size_t)1u);
        vec_destroy(&output);

        collector = desc->collector(&output, 1u);
        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        check_equal(cmeta_collector_finish(&collector), CMETA_OK);
        check_equal(vec_size(&output), (size_t)0u);
        vec_destroy(&output);
    }
}

#undef VERIFY_EMPTY_COLLECTOR
#undef VERIFY_C1_COLLECTOR
#undef VERIFY_C2_COLLECTOR
