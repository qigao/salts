#include "legacy_generated_typed.h"
#include "tinytest.h"

#include <string.h>

#define COUNT_CONTAINER_KIND(kind,arity,family,raw,prefix,methods,accept,key_at,value_at,range_flags,key_flags,value_flags,entry_flags) + 1
enum { TURBO_TEST_CONTAINER_KIND_COUNT = 0 Replay(TURBO_STL_KIND_SCHEMA, COUNT_CONTAINER_KIND) };
#undef COUNT_CONTAINER_KIND

#define DECLARE_CONTAINER_KIND_ARITY(kind,arity,family,raw,prefix,methods,accept,key_at,value_at,range_flags,key_flags,value_flags,entry_flags) TURBO_TEST_ARITY_##kind = arity,
enum { Replay(TURBO_STL_KIND_SCHEMA, DECLARE_CONTAINER_KIND_ARITY) };
#undef DECLARE_CONTAINER_KIND_ARITY

_Static_assert(TURBO_TEST_CONTAINER_KIND_COUNT == 13,
               "TURBO_STL_KIND_SCHEMA_COUNT_MISMATCH");
_Static_assert(TURBO_TEST_ARITY_Vec == 1 && TURBO_TEST_ARITY_HashMap == 2 &&
                   TURBO_TEST_ARITY_BPlusTree == 2,
               "TURBO_STL_KIND_SCHEMA_ARITY_MISMATCH");

typed(Vec, IntVec, int);
typed(Deque, IntDeque, int);
typed(List, IntList, int);
typed(Stack, IntStack, int);
typed(Queue, IntQueue, int);
typed(Heap, IntHeap, int);
typed(Set, IntSet, int);
typed(HashSet, IntHashSet, int);
typed(HashMap, IntLongHashMap, int, long);
typed(Map, IntLongMap, int, long);
typed(MultiMap, IntLongMultiMap, int, long);
typed(BTree, IntLongBTree, int, long);
typed(BPlusTree, IntLongBPlusTree, int, long);

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

#define VERIFY_C1_COLLECTOR(Name, Value) do {                               \
    Name committed = {0};                                                   \
    Name aborted = {0};                                                     \
    int first = (Value);                                                    \
    int second = (Value) + 1;                                               \
    cmeta_collector collector = Name##_collector(&committed, 1u);           \
    check_equal(cmeta_collector_begin(&collector), CMETA_OK);                \
    check_equal(cmeta_collector_accept(&collector, collector.input_type,     \
                                       &first), CMETA_OK);                   \
    check_equal(cmeta_collector_finish(&collector), CMETA_OK);               \
    check_equal(Name##_size(&committed), (size_t)1u);                        \
    Name##_destroy(&committed);                                             \
    collector = Name##_collector(&aborted, 1u);                              \
    check_equal(cmeta_collector_begin(&collector), CMETA_OK);                \
    check_equal(cmeta_collector_accept(&collector, collector.input_type,     \
                                       &first), CMETA_OK);                   \
    check_equal(cmeta_collector_accept(&collector, collector.input_type,     \
                                       &second), CMETA_CAPACITY_EXCEEDED);   \
    check_equal(memcmp(&aborted, &(Name){0}, sizeof(aborted)), 0);           \
    cmeta_collector_abort(&collector);                                       \
    check_equal(memcmp(&aborted, &(Name){0}, sizeof(aborted)), 0);           \
} while (0)

#define VERIFY_C2_COLLECTOR(Name, Key, Value) do {                           \
    Name committed = {0};                                                   \
    Name aborted = {0};                                                     \
    Name##_entry first = {(Key), (Value)};                                  \
    Name##_entry second = {(Key) + 1, (Value) + 1};                         \
    cmeta_collector collector = Name##_collector(&committed, 1u);           \
    check_equal(cmeta_collector_begin(&collector), CMETA_OK);                \
    check_equal(cmeta_collector_accept(&collector, collector.input_type,     \
                                       &first), CMETA_OK);                   \
    check_equal(cmeta_collector_finish(&collector), CMETA_OK);               \
    check_equal(Name##_size(&committed), (size_t)1u);                        \
    Name##_destroy(&committed);                                             \
    collector = Name##_collector(&aborted, 1u);                              \
    check_equal(cmeta_collector_begin(&collector), CMETA_OK);                \
    check_equal(cmeta_collector_accept(&collector, collector.input_type,     \
                                       &first), CMETA_OK);                   \
    check_equal(cmeta_collector_accept(&collector, collector.input_type,     \
                                       &second), CMETA_CAPACITY_EXCEEDED);   \
    check_equal(memcmp(&aborted, &(Name){0}, sizeof(aborted)), 0);           \
    cmeta_collector_abort(&collector);                                       \
    check_equal(memcmp(&aborted, &(Name){0}, sizeof(aborted)), 0);           \
} while (0)

spec("TurboSTL typed schema") {
    it("generates a bounded sequence Range and transactional collector") {
        IntVec source = {0};
        IntVec output = {0};
        cmeta_range range;
        cmeta_collector collector;
        cmeta_range_cursor cursor = {0};
        int value = 4;
        int ranged = 0;

        check_equal(IntVec_init(&source, 2u), TURBO_STL_OK);
        check_equal(IntVec_push(&source, value), TURBO_STL_OK);
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
        cmeta_range_cursor cursor = {0};
        cmeta_range_cursor before_cursor;
        int one = 1;
        int two = 2;
        int output = 91;

        check_equal(IntDeque_init(&values, 2u), TURBO_STL_OK);
        check_equal(IntDeque_push_back(&values, one), TURBO_STL_OK);
        check_true(cmeta_container_range_view(&values,
                                              CMETA_CONTAINER_VIEW_DEFAULT,
                                              &range));
        check_equal(IntDeque_push_back(&values, two), TURBO_STL_OK);
        before_cursor = cursor;
        check_equal(cmeta_range_next(&range, &cursor, &output),
                    CMETA_GEN_MUTATED);
        check_equal(memcmp(&cursor, &before_cursor, sizeof(cursor)), 0);
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
        cmeta_range_cursor cursor = {0};

        check_equal(IntLongHashMap_init(&map, 1u), TURBO_STL_OK);
        check_equal(IntLongHashMap_put(&map, 7, 70L), TURBO_STL_OK);
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

        check_equal(IntHeap_init(&heap, 0u), TURBO_STL_OK);
        check_equal(IntHeap_push(&heap, one), TURBO_STL_CAPACITY_EXCEEDED);
        check_equal(IntSet_init(&set, 0u), TURBO_STL_OK);
        check_equal(IntSet_add(&set, one), TURBO_STL_CAPACITY_EXCEEDED);
        check_equal(IntLongMap_init(&map, 0u), TURBO_STL_OK);
        check_equal(IntLongMap_put(&map, one, 1L), TURBO_STL_CAPACITY_EXCEEDED);
        check_equal(IntLongMultiMap_init(&multimap, 0u), TURBO_STL_OK);
        check_equal(IntLongMultiMap_put(&multimap, one, 1L),
                    TURBO_STL_CAPACITY_EXCEEDED);
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
                    TURBO_STL_CAPACITY_EXCEEDED);
        check_equal(memcmp(&vector, &(IntVec){0}, sizeof(vector)), 0);
        check_equal(IntVec_from(&vector, values, 2u, 2u), TURBO_STL_OK);
        check_equal(IntVec_size(&vector), (size_t)2u);

        check_equal(IntLongHashMap_from(&map, map_entries, 2u, 2u),
                    TURBO_STL_OK);
        check_equal(*IntLongHashMap_get_const(&map, 2), 20L);
        generation = turbo_hash_map_generation(&map.raw);
        check_equal(IntLongHashMap_from(&map, map_entries, 2u, 1u),
                    TURBO_STL_CAPACITY_EXCEEDED);
        check_equal(turbo_hash_map_generation(&map.raw), generation);
        check_equal(*IntLongHashMap_get_const(&map, 2), 20L);

        check_equal(IntLongMultiMap_from(&multimap, multi_entries, 2u, 1u),
                    TURBO_STL_CAPACITY_EXCEEDED);
        check_true(IntLongMultiMap_empty(&multimap));
        check_equal(IntLongMultiMap_from(&multimap, multi_entries, 2u, 2u),
                    TURBO_STL_OK);
        check_equal(IntLongMultiMap_count(&multimap, 1), (size_t)2u);

        check_equal(IntLongBPlusTree_from(&tree, tree_entries, 2u, 2u),
                    TURBO_STL_OK);
        check_equal(*IntLongBPlusTree_get_const(&tree, 1), 10L);

        IntLongBPlusTree_destroy(&tree);
        IntLongMultiMap_destroy(&multimap);
        IntLongHashMap_destroy(&map);
        IntVec_destroy(&vector);
    }

    it("counts live keys rather than input rows when building associative containers") {
        IntLongHashMap hash_map = {0};
        IntLongMap map = {0};
        IntLongBTree btree = {0};
        IntLongBPlusTree bplus = {0};
        IntLongHashMap_entry hash_entries[] = {{1, 10L}, {1, 11L}};
        IntLongMap_entry map_entries[] = {{1, 20L}, {1, 21L}};
        IntLongBTree_entry btree_entries[] = {{1, 30L}, {1, 31L}};
        IntLongBPlusTree_entry bplus_entries[] = {{1, 40L}, {1, 41L}};
        IntLongHashMap_entry hash_distinct[] = {{2, 12L}, {3, 13L}};
        IntLongMap_entry map_distinct[] = {{2, 22L}, {3, 23L}};
        IntLongBTree_entry btree_distinct[] = {{2, 32L}, {3, 33L}};
        IntLongBPlusTree_entry bplus_distinct[] = {{2, 42L}, {3, 43L}};

        check_equal(IntLongHashMap_from(&hash_map, hash_entries, 2u, 1u),
                    TURBO_STL_OK);
        check_equal(IntLongHashMap_size(&hash_map), (size_t)1u);
        check_equal(*IntLongHashMap_get_const(&hash_map, 1), 11L);
        IntLongHashMap_destroy(&hash_map);

        check_equal(IntLongMap_from(&map, map_entries, 2u, 1u), TURBO_STL_OK);
        check_equal(IntLongMap_size(&map), (size_t)1u);
        check_equal(*IntLongMap_get_const(&map, 1), 21L);
        IntLongMap_destroy(&map);

        check_equal(IntLongBTree_from(&btree, btree_entries, 2u, 1u),
                    TURBO_STL_OK);
        check_equal(IntLongBTree_size(&btree), (size_t)1u);
        check_equal(*IntLongBTree_get_const(&btree, 1), 31L);
        IntLongBTree_destroy(&btree);

        check_equal(IntLongBPlusTree_from(&bplus, bplus_entries, 2u, 1u),
                    TURBO_STL_OK);
        check_equal(IntLongBPlusTree_size(&bplus), (size_t)1u);
        check_equal(*IntLongBPlusTree_get_const(&bplus, 1), 41L);
        IntLongBPlusTree_destroy(&bplus);

        check_equal(IntLongHashMap_from(&hash_map, hash_distinct, 2u, 1u),
                    TURBO_STL_CAPACITY_EXCEEDED);
        check_equal(memcmp(&hash_map, &(IntLongHashMap){0}, sizeof(hash_map)), 0);
        check_equal(IntLongMap_from(&map, map_distinct, 2u, 1u),
                    TURBO_STL_CAPACITY_EXCEEDED);
        check_equal(memcmp(&map, &(IntLongMap){0}, sizeof(map)), 0);
        check_equal(IntLongBTree_from(&btree, btree_distinct, 2u, 1u),
                    TURBO_STL_CAPACITY_EXCEEDED);
        check_equal(memcmp(&btree, &(IntLongBTree){0}, sizeof(btree)), 0);
        check_equal(IntLongBPlusTree_from(&bplus, bplus_distinct, 2u, 1u),
                    TURBO_STL_CAPACITY_EXCEEDED);
        check_equal(memcmp(&bplus, &(IntLongBPlusTree){0}, sizeof(bplus)), 0);
    }

    it("uses container storage traits for generated wrappers") {
        VERIFY_C1_COLLECTOR(IntVec, 3);
        VERIFY_C1_COLLECTOR(IntDeque, 4);
        VERIFY_C1_COLLECTOR(IntList, 5);
        VERIFY_C1_COLLECTOR(IntStack, 6);
        VERIFY_C1_COLLECTOR(IntQueue, 7);
        VERIFY_C1_COLLECTOR(IntHeap, 8);
        VERIFY_C1_COLLECTOR(IntSet, 9);
        VERIFY_C1_COLLECTOR(IntHashSet, 10);
        VERIFY_C2_COLLECTOR(IntLongHashMap, 11, 110L);
        VERIFY_C2_COLLECTOR(IntLongMap, 12, 120L);
        VERIFY_C2_COLLECTOR(IntLongMultiMap, 13, 130L);
        VERIFY_C2_COLLECTOR(IntLongBTree, 14, 140L);
        VERIFY_C2_COLLECTOR(IntLongBPlusTree, 15, 150L);
    }
}
