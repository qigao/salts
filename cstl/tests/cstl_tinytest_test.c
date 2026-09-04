#include <cstl/typed.h>

typed(Vec, TinyTestIntVec, int);
typed(Deque, TinyTestIntDeque, int);
typed(List, TinyTestIntList, int);
typed(Map, TinyTestIntMap, int, int);
typed(MultiMap, TinyTestIntMultiMap, int, int);
typed(BTree, TinyTestIntBTree, int, int);
typed(BPlusTree, TinyTestIntBPlusTree, int, int);
typed(Vec, TinyTestIntVecWrongType, int);
typed(Map, TinyTestIntMapWrongKeyType, int, int);

#include <cstl/tinytest.h>

CSTL_TINYTEST_DEFINE_SEQUENCE_EQUAL(TinyTestIntVec, int)
CSTL_TINYTEST_DEFINE_SEQUENCE_EQUAL(TinyTestIntDeque, int)
CSTL_TINYTEST_DEFINE_SEQUENCE_EQUAL(TinyTestIntList, int)
CSTL_TINYTEST_DEFINE_ORDERED_MAP_EQUAL(TinyTestIntMap, int, int)
CSTL_TINYTEST_DEFINE_ORDERED_MAP_EQUAL(TinyTestIntMultiMap, int, int)
CSTL_TINYTEST_DEFINE_ORDERED_MAP_EQUAL(TinyTestIntBTree, int, int)
CSTL_TINYTEST_DEFINE_ORDERED_MAP_EQUAL(TinyTestIntBPlusTree, int, int)
CSTL_TINYTEST_DEFINE_SEQUENCE_EQUAL(TinyTestIntVecWrongType, long long)
CSTL_TINYTEST_DEFINE_ORDERED_MAP_EQUAL(TinyTestIntMapWrongKeyType, float, int)

static void cstl_tinytest_fill_vec(TinyTestIntVec *values, int first, int second) {
  check_equal(TinyTestIntVec_init(values, 2u), STL_OK);
  check_equal(TinyTestIntVec_push(values, first), STL_OK);
  check_equal(TinyTestIntVec_push(values, second), STL_OK);
}

static void cstl_tinytest_fill_deque(TinyTestIntDeque *values, int first, int second) {
  check_equal(TinyTestIntDeque_init(values, 2u), STL_OK);
  check_equal(TinyTestIntDeque_push_back(values, first), STL_OK);
  check_equal(TinyTestIntDeque_push_back(values, second), STL_OK);
}

static void cstl_tinytest_fill_list(TinyTestIntList *values, int first, int second) {
  check_equal(TinyTestIntList_init(values, 2u), STL_OK);
  check_equal(TinyTestIntList_push_back(values, first), STL_OK);
  check_equal(TinyTestIntList_push_back(values, second), STL_OK);
}

static void cstl_tinytest_fill_map(TinyTestIntMap *values, int first_key, int first_value,
                                   int second_key, int second_value) {
  check_equal(TinyTestIntMap_init(values, 2u), STL_OK);
  check_equal(TinyTestIntMap_put(values, first_key, first_value), STL_OK);
  check_equal(TinyTestIntMap_put(values, second_key, second_value), STL_OK);
}

static void cstl_tinytest_fill_multimap(TinyTestIntMultiMap *values, int first_key,
                                        int first_value, int second_key, int second_value) {
  check_equal(TinyTestIntMultiMap_init(values, 2u), STL_OK);
  check_equal(TinyTestIntMultiMap_put(values, first_key, first_value), STL_OK);
  check_equal(TinyTestIntMultiMap_put(values, second_key, second_value), STL_OK);
}

suite("CSTL TinyTest equality bridge") {
  it("compares explicitly declared ordered typed containers without copying handles") {
    TinyTestIntVec actual_vec = {0};
    TinyTestIntVec expected_vec = {0};
    TinyTestIntDeque actual_deque = {0};
    TinyTestIntDeque expected_deque = {0};
    TinyTestIntList actual_list = {0};
    TinyTestIntList expected_list = {0};
    TinyTestIntMap actual_map = {0};
    TinyTestIntMap expected_map = {0};

    cstl_tinytest_fill_vec(&actual_vec, 3, 5);
    cstl_tinytest_fill_vec(&expected_vec, 3, 5);
    cstl_tinytest_fill_deque(&actual_deque, 7, 11);
    cstl_tinytest_fill_deque(&expected_deque, 7, 11);
    cstl_tinytest_fill_list(&actual_list, 13, 17);
    cstl_tinytest_fill_list(&expected_list, 13, 17);
    cstl_tinytest_fill_map(&actual_map, 7, 70, 3, 30);
    cstl_tinytest_fill_map(&expected_map, 3, 30, 7, 70);

    check_cstl_equal(TinyTestIntVec, actual_vec, expected_vec);
    check_cstl_equal(TinyTestIntDeque, actual_deque, expected_deque);
    check_cstl_equal(TinyTestIntList, actual_list, expected_list);
    check_cstl_equal(TinyTestIntMap, actual_map, expected_map);

    TinyTestIntMap_destroy(&expected_map);
    TinyTestIntMap_destroy(&actual_map);
    TinyTestIntList_destroy(&expected_list);
    TinyTestIntList_destroy(&actual_list);
    TinyTestIntDeque_destroy(&expected_deque);
    TinyTestIntDeque_destroy(&actual_deque);
    TinyTestIntVec_destroy(&expected_vec);
    TinyTestIntVec_destroy(&actual_vec);
  }

  it("rejects a sequence whose element differs") {
    TinyTestIntVec actual = {0};
    TinyTestIntVec expected = {0};

    cstl_tinytest_fill_vec(&actual, 3, 5);
    cstl_tinytest_fill_vec(&expected, 3, 7);

    check_false(TinyTestIntVec_cstl_tinytest_equal(&actual, &expected));

    TinyTestIntVec_destroy(&expected);
    TinyTestIntVec_destroy(&actual);
  }

  it("rejects an ordered map whose value differs") {
    TinyTestIntMap actual = {0};
    TinyTestIntMap expected = {0};

    cstl_tinytest_fill_map(&actual, 3, 30, 7, 70);
    cstl_tinytest_fill_map(&expected, 3, 30, 7, 71);

    check_false(TinyTestIntMap_cstl_tinytest_equal(&actual, &expected));

    TinyTestIntMap_destroy(&expected);
    cstl_tinytest_fill_map(&expected, 3, 30, 8, 70);

    check_false(TinyTestIntMap_cstl_tinytest_equal(&actual, &expected));

    TinyTestIntMap_destroy(&expected);
    TinyTestIntMap_destroy(&actual);
  }

  it("compares ordered associative containers with duplicate keys as ordered pairs") {
    TinyTestIntMultiMap actual = {0};
    TinyTestIntMultiMap expected = {0};

    cstl_tinytest_fill_multimap(&actual, 3, 30, 3, 31);
    cstl_tinytest_fill_multimap(&expected, 3, 30, 3, 31);

    check_cstl_equal(TinyTestIntMultiMap, actual, expected);

    TinyTestIntMultiMap_destroy(&expected);
    TinyTestIntMultiMap_destroy(&actual);
  }

  it("compares BTree and BPlusTree by their ordered keys and values") {
    TinyTestIntBTree actual_btree = {0};
    TinyTestIntBTree expected_btree = {0};
    TinyTestIntBPlusTree actual_bplus = {0};
    TinyTestIntBPlusTree expected_bplus = {0};
    TinyTestIntBTree_entry btree_actual_entries[] = {{7, 70}, {3, 30}};
    TinyTestIntBTree_entry btree_expected_entries[] = {{3, 30}, {7, 70}};
    TinyTestIntBPlusTree_entry bplus_actual_entries[] = {{7, 70}, {3, 30}};
    TinyTestIntBPlusTree_entry bplus_expected_entries[] = {{3, 30}, {7, 70}};

    check_equal(TinyTestIntBTree_from(&actual_btree, btree_actual_entries, 2u, 2u), STL_OK);
    check_equal(TinyTestIntBTree_from(&expected_btree, btree_expected_entries, 2u, 2u), STL_OK);
    check_equal(TinyTestIntBPlusTree_from(&actual_bplus, bplus_actual_entries, 2u, 2u), STL_OK);
    check_equal(TinyTestIntBPlusTree_from(&expected_bplus, bplus_expected_entries, 2u, 2u), STL_OK);

    check_cstl_equal(TinyTestIntBTree, actual_btree, expected_btree);
    check_cstl_equal(TinyTestIntBPlusTree, actual_bplus, expected_bplus);

    TinyTestIntBPlusTree_destroy(&expected_bplus);
    TinyTestIntBPlusTree_destroy(&actual_bplus);
    TinyTestIntBTree_destroy(&expected_btree);
    TinyTestIntBTree_destroy(&actual_btree);
  }

  it("rejects comparators whose declared value storage differs from the typed range") {
    TinyTestIntVecWrongType actual_vec = {0};
    TinyTestIntVecWrongType expected_vec = {0};
    TinyTestIntMapWrongKeyType actual_map = {0};
    TinyTestIntMapWrongKeyType expected_map = {0};

    check_equal(TinyTestIntVecWrongType_init(&actual_vec, 1u), STL_OK);
    check_equal(TinyTestIntVecWrongType_init(&expected_vec, 1u), STL_OK);
    check_equal(TinyTestIntVecWrongType_push(&actual_vec, 3), STL_OK);
    check_equal(TinyTestIntVecWrongType_push(&expected_vec, 3), STL_OK);
    check_false(TinyTestIntVecWrongType_cstl_tinytest_equal(&actual_vec, &expected_vec));

    check_equal(TinyTestIntMapWrongKeyType_init(&actual_map, 1u), STL_OK);
    check_equal(TinyTestIntMapWrongKeyType_init(&expected_map, 1u), STL_OK);
    check_equal(TinyTestIntMapWrongKeyType_put(&actual_map, 3, 30), STL_OK);
    check_equal(TinyTestIntMapWrongKeyType_put(&expected_map, 3, 30), STL_OK);
    check_false(TinyTestIntMapWrongKeyType_cstl_tinytest_equal(&actual_map, &expected_map));

    TinyTestIntMapWrongKeyType_destroy(&expected_map);
    TinyTestIntMapWrongKeyType_destroy(&actual_map);
    TinyTestIntVecWrongType_destroy(&expected_vec);
    TinyTestIntVecWrongType_destroy(&actual_vec);
  }
}
