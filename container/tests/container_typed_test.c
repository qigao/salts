#include <turbo/container/typed.h>
#include "tinytest.h"

#include <string.h>

Containers(
    (Vec, IntVec, int),
    (Deque, IntDeque, int),
    (List, IntList, int),
    (Stack, IntStack, int),
    (Queue, IntQueue, int),
    (Heap, IntHeap, int),
    (Set, IntSet, int),
    (HashSet, IntHashSet, int),
    (HashMap, IntLongHashMap, int, long),
    (Map, IntLongMap, int, long),
    (MultiMap, IntLongMultiMap, int, long),
    (BTree, IntLongBTree, int, long),
    (BPlusTree, IntLongBPlusTree, int, long)
);

#define VERIFY_EMPTY_COLLECTOR(Name) do {                                  \
    Name output = {0};                                                      \
    cmeta_collector collector;                                              \
    check_true(Name##_cmeta_container_desc.collector != NULL);              \
    collector = Name##_cmeta_container_desc.collector(&output, 2u);         \
    check_equal(cmeta_collector_begin(&collector), CMETA_OK);                \
    check_equal(cmeta_collector_finish(&collector), CMETA_OK);               \
    check_true(cmeta_container_descriptor(&output) ==                        \
               &Name##_cmeta_container_desc);                               \
    Name##_destroy(&output);                                                \
} while (0)

spec("Container typed schema") {
    it("generates a bounded sequence Range and transactional collector") {
        IntVec source = {0};
        IntVec output = {0};
        cmeta_range range;
        cmeta_collector collector;
        size_t cursor = 0u;
        int value = 4;
        int ranged = 0;

        check_equal(IntVec_init(&source, 2u), CONTAINER_OK);
        check_equal(IntVec_push(&source, value), CONTAINER_OK);
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

        collector = IntVec_collector(&output, 1u);
        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &value),
                    CMETA_OK);
        check_equal(cmeta_collector_finish(&collector), CMETA_OK);
        check_equal(IntVec_size(&output), (size_t)1u);
        check_equal(*IntVec_at_const(&output, 0u), 4);
        IntVec_destroy(&output);
        IntVec_destroy(&source);
    }

    it("invalidates an existing Range without changing cursor or output") {
        IntDeque values = {0};
        cmeta_range range;
        size_t cursor = 0u;
        int one = 1;
        int two = 2;
        int output = 91;

        check_equal(IntDeque_init(&values, 2u), CONTAINER_OK);
        check_equal(IntDeque_push_back(&values, one), CONTAINER_OK);
        check_true(cmeta_container_range_view(&values,
                                              CMETA_CONTAINER_VIEW_DEFAULT,
                                              &range));
        check_equal(IntDeque_push_back(&values, two), CONTAINER_OK);
        check_equal(cmeta_range_next(&range, &cursor, &output),
                    CMETA_GEN_MUTATED);
        check_equal(cursor, (size_t)0u);
        check_equal(output, 91);
        IntDeque_destroy(&values);
    }

    it("exposes default entries plus key value and entry views for maps") {
        IntLongHashMap map = {0};
        cmeta_range default_range;
        cmeta_range keys;
        cmeta_range values;
        cmeta_range entries;
        IntLongHashMap_entry entry = {0};
        size_t cursor = 0u;

        check_equal(IntLongHashMap_init(&map, 1u), CONTAINER_OK);
        check_equal(IntLongHashMap_put(&map, 7, 70L), CONTAINER_OK);
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
        check_equal(entry.key, 7);
        check_equal(entry.value, 70L);
        IntLongHashMap_destroy(&map);
    }

    it("propagates explicit limits through every generated initializer") {
        IntHeap heap = {0};
        IntSet set = {0};
        IntLongMap map = {0};
        IntLongMultiMap multimap = {0};
        int one = 1;

        check_equal(IntHeap_init(&heap, 0u), CONTAINER_OK);
        check_equal(IntHeap_push(&heap, one), CONTAINER_CAPACITY_EXCEEDED);
        check_equal(IntSet_init(&set, 0u), CONTAINER_OK);
        check_equal(IntSet_add(&set, one), CONTAINER_CAPACITY_EXCEEDED);
        check_equal(IntLongMap_init(&map, 0u), CONTAINER_OK);
        check_equal(IntLongMap_put(&map, one, 1L), CONTAINER_CAPACITY_EXCEEDED);
        check_equal(IntLongMultiMap_init(&multimap, 0u, 0u), CONTAINER_OK);
        check_equal(IntLongMultiMap_put(&multimap, one, 1L),
                    CONTAINER_CAPACITY_EXCEEDED);
        IntLongMultiMap_destroy(&multimap);
        IntLongMap_destroy(&map);
        IntSet_destroy(&set);
        IntHeap_destroy(&heap);
    }

    it("requires explicit from limits and commits associative inputs once") {
        IntVec vector = {0};
        IntLongHashMap map = {0};
        IntLongMultiMap multimap = {0};
        IntLongBPlusTree tree = {0};
        int values[] = {1, 2};
        IntLongHashMap_entry map_entries[] = {{1, 10L}, {2, 20L}};
        IntLongMultiMap_entry multi_entries[] = {{1, 10L}, {1, 11L}};
        IntLongBPlusTree_entry tree_entries[] = {{2, 20L}, {1, 10L}};
        uint64_t generation;

        check_equal(IntVec_from(&vector, values, 2u, 1u),
                    CONTAINER_CAPACITY_EXCEEDED);
        check_equal(memcmp(&vector, &(IntVec){0}, sizeof(vector)), 0);
        check_equal(IntVec_from(&vector, values, 2u, 2u), CONTAINER_OK);
        check_equal(IntVec_size(&vector), (size_t)2u);

        check_equal(IntLongHashMap_from(&map, map_entries, 2u, 2u),
                    CONTAINER_OK);
        check_equal(*IntLongHashMap_get_const(&map, 2), 20L);
        generation = turbo_hash_map_generation(&map.raw);
        check_equal(IntLongHashMap_from(&map, map_entries, 2u, 1u),
                    CONTAINER_CAPACITY_EXCEEDED);
        check_equal(turbo_hash_map_generation(&map.raw), generation);
        check_equal(*IntLongHashMap_get_const(&map, 2), 20L);

        check_equal(IntLongMultiMap_from(&multimap, multi_entries, 2u, 1u, 1u),
                    CONTAINER_CAPACITY_EXCEEDED);
        check_true(IntLongMultiMap_empty(&multimap));
        check_equal(IntLongMultiMap_from(&multimap, multi_entries, 2u, 1u, 2u),
                    CONTAINER_OK);
        check_equal(IntLongMultiMap_count(&multimap, 1), (size_t)2u);

        check_equal(IntLongBPlusTree_from(&tree, tree_entries, 2u, 2u),
                    CONTAINER_OK);
        check_equal(*IntLongBPlusTree_get_const(&tree, 1), 10L);

        IntLongBPlusTree_destroy(&tree);
        IntLongMultiMap_destroy(&multimap);
        IntLongHashMap_destroy(&map);
        IntVec_destroy(&vector);
    }

    it("aborts a collector exactly once and restores zero output") {
        IntVec output = {0};
        cmeta_collector collector = IntVec_collector(&output, 1u);
        int one = 1;
        int two = 2;

        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &one),
                    CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &two),
                    CMETA_CAPACITY_EXCEEDED);
        check_true(collector.state == CMETA_COLLECTOR_ABORTED);
        check_equal(memcmp(&output, &(IntVec){0}, sizeof(output)), 0);
        cmeta_collector_abort(&collector);
        check_equal(memcmp(&output, &(IntVec){0}, sizeof(output)), 0);
    }

    it("exposes an empty committing collector for all standard kinds") {
        VERIFY_EMPTY_COLLECTOR(IntVec);
        VERIFY_EMPTY_COLLECTOR(IntDeque);
        VERIFY_EMPTY_COLLECTOR(IntList);
        VERIFY_EMPTY_COLLECTOR(IntStack);
        VERIFY_EMPTY_COLLECTOR(IntQueue);
        VERIFY_EMPTY_COLLECTOR(IntHeap);
        VERIFY_EMPTY_COLLECTOR(IntSet);
        VERIFY_EMPTY_COLLECTOR(IntHashSet);
        VERIFY_EMPTY_COLLECTOR(IntLongHashMap);
        VERIFY_EMPTY_COLLECTOR(IntLongMap);
        VERIFY_EMPTY_COLLECTOR(IntLongMultiMap);
        VERIFY_EMPTY_COLLECTOR(IntLongBTree);
        VERIFY_EMPTY_COLLECTOR(IntLongBPlusTree);
    }

    it("collects associative tree and multimap entries transactionally") {
        IntLongHashMap hash_map = {0};
        IntLongBTree tree = {0};
        IntLongMultiMap multimap = {0};
        IntLongHashMap_entry hash_entry = {3, 30L};
        IntLongBTree_entry tree_entry = {2, 20L};
        IntLongMultiMap_entry multi_entries[] = {{1, 10L}, {1, 11L}};
        cmeta_collector hash_collector = IntLongHashMap_collector(&hash_map, 1u);
        cmeta_collector tree_collector = IntLongBTree_collector(&tree, 1u);
        cmeta_collector multi_collector = IntLongMultiMap_collector(&multimap, 2u);

        check_equal(cmeta_collector_begin(&hash_collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&hash_collector,
                                           hash_collector.input_type,
                                           &hash_entry), CMETA_OK);
        check_equal(cmeta_collector_finish(&hash_collector), CMETA_OK);
        check_equal(*IntLongHashMap_get_const(&hash_map, 3), 30L);

        check_equal(cmeta_collector_begin(&tree_collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&tree_collector,
                                           tree_collector.input_type,
                                           &tree_entry), CMETA_OK);
        check_equal(cmeta_collector_finish(&tree_collector), CMETA_OK);
        check_equal(*IntLongBTree_get_const(&tree, 2), 20L);

        check_equal(cmeta_collector_begin(&multi_collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&multi_collector,
                                           multi_collector.input_type,
                                           &multi_entries[0]), CMETA_OK);
        check_equal(cmeta_collector_accept(&multi_collector,
                                           multi_collector.input_type,
                                           &multi_entries[1]), CMETA_OK);
        check_equal(cmeta_collector_finish(&multi_collector), CMETA_OK);
        check_equal(IntLongMultiMap_count(&multimap, 1), (size_t)2u);

        IntLongMultiMap_destroy(&multimap);
        IntLongBTree_destroy(&tree);
        IntLongHashMap_destroy(&hash_map);
    }

    it("handles collector type mismatch limit zero reuse and terminal states") {
        IntVec output = {0};
        cmeta_collector collector = IntVec_collector(&output, 0u);
        int value = 7;

        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &value),
                    CMETA_CAPACITY_EXCEEDED);
        check_equal(memcmp(&output, &(IntVec){0}, sizeof(output)), 0);
        check_equal(cmeta_collector_finish(&collector), CMETA_INVALID_ARGUMENT);

        collector = IntVec_collector(&output, 1u);
        collector.input_type = &cmeta_type_long;
        check_equal(cmeta_collector_begin(&collector), CMETA_TYPE_MISMATCH);
        check_equal(memcmp(&output, &(IntVec){0}, sizeof(output)), 0);

        collector = IntVec_collector(&output, 1u);
        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &value),
                    CMETA_OK);
        check_equal(cmeta_collector_finish(&collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &value),
                    CMETA_INVALID_ARGUMENT);
        check_equal(cmeta_collector_finish(&collector), CMETA_INVALID_ARGUMENT);
        IntVec_destroy(&output);

        collector = IntVec_collector(&output, 1u);
        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        check_equal(cmeta_collector_finish(&collector), CMETA_OK);
        IntVec_destroy(&output);
    }
}

#undef VERIFY_EMPTY_COLLECTOR
