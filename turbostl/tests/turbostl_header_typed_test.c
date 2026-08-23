#include <turbostl/typed.h>

#include "tinytest.h"

typed(Vec, HeaderVec, int);
typed(Deque, HeaderDeque, int);
typed(List, HeaderList, int);
typed(Stack, HeaderStack, int);
typed(Queue, HeaderQueue, int);
typed(Heap, HeaderHeap, int);
typed(Set, HeaderSet, int);
typed(HashSet, HeaderHashSet, int);
typed(HashMap, HeaderHashMap, int, long);
typed(Map, HeaderMap, int, long);
typed(MultiMap, HeaderMultiMap, int, long);
typed(BTree, HeaderBTree, int, long);
typed(BPlusTree, HeaderBPlusTree, int, long);

#ifndef CMETA_GENERIC_KIND_Vec
#error "typed.h must register TurboSTL finite Generic kinds"
#endif

suite("TurboSTL typed public header") {
    it("instantiates all thirteen finite Generic kinds") {
        HeaderVec vec = {0};
        HeaderDeque deque = {0};
        HeaderList list = {0};
        HeaderStack stack = {0};
        HeaderQueue queue = {0};
        HeaderHeap heap = {0};
        HeaderSet set = {0};
        HeaderHashSet hash_set = {0};
        HeaderHashMap hash_map = {0};
        HeaderMap map = {0};
        HeaderMultiMap multimap = {0};
        HeaderBTree btree = {0};
        HeaderBPlusTree bplus_tree = {0};

        check_equal(HeaderVec_init(&vec, 2u), STL_OK);
        check_equal(HeaderDeque_init(&deque, 2u), STL_OK);
        check_equal(HeaderList_init(&list, 2u), STL_OK);
        check_equal(HeaderStack_init(&stack, 2u), STL_OK);
        check_equal(HeaderQueue_init(&queue, 2u), STL_OK);
        check_equal(HeaderHeap_init(&heap, 2u), STL_OK);
        check_equal(HeaderSet_init(&set, 2u), STL_OK);
        check_equal(HeaderHashSet_init(&hash_set, 2u), STL_OK);
        check_equal(HeaderHashMap_init(&hash_map, 2u), STL_OK);
        check_equal(HeaderMap_init(&map, 2u), STL_OK);
        check_equal(HeaderMultiMap_init(&multimap, 2u), STL_OK);
        check_equal(HeaderBTree_init(&btree, 2u), STL_OK);
        check_equal(HeaderBPlusTree_init(&bplus_tree, 2u), STL_OK);

        check_not_null(cmeta_container_descriptor(&vec));
        check_not_null(cmeta_container_descriptor(&list));
        check_not_null(cmeta_container_descriptor(&map));

        HeaderBPlusTree_destroy(&bplus_tree);
        HeaderBTree_destroy(&btree);
        HeaderMultiMap_destroy(&multimap);
        HeaderMap_destroy(&map);
        HeaderHashMap_destroy(&hash_map);
        HeaderHashSet_destroy(&hash_set);
        HeaderSet_destroy(&set);
        HeaderHeap_destroy(&heap);
        HeaderQueue_destroy(&queue);
        HeaderStack_destroy(&stack);
        HeaderList_destroy(&list);
        HeaderDeque_destroy(&deque);
        HeaderVec_destroy(&vec);
    }

    it("dispatches semantic List and Map operations by type token") {
        HeaderList list = {0};
        HeaderMap map = {0};
        int list_value = 7;
        int list_output = 0;
        const int key = 3;
        const long value = 30L;

        check_equal(list_init(HeaderList, &list, 2u), STL_OK);
        check_equal(list_add(HeaderList, &list, list_value), STL_OK);
        check_equal(list_pop_front(HeaderList, &list, &list_output), STL_OK);
        check_equal(list_output, list_value);

        check_equal(map_init(HeaderMap, &map, 2u), STL_OK);
        check_equal(map_put(HeaderMap, &map, key, value), STL_OK);
        check_equal(map_size(HeaderMap, &map), (size_t)1u);
        check_equal(*HeaderMap_get_const(&map, key), value);

        map_destroy(HeaderMap, &map);
        list_destroy(HeaderList, &list);
    }
}
