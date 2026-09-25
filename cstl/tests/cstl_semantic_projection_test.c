#include <cstl/typed.h>
#include <cmeta/data.h>
#include "tinytest.h"

typedef struct cstl_semantic_collect_ints {
  int *values;
  size_t count;
} cstl_semantic_collect_ints;

static cmeta_status cstl_semantic_collect_int(
    void *context, const void *element) {
  cstl_semantic_collect_ints *out = (cstl_semantic_collect_ints *)context;
  if (out == NULL || element == NULL) return CMETA_INVALID_ARGUMENT;
  out->values[out->count++] = *(const int *)element;
  return CMETA_OK;
}

typedef struct cstl_semantic_map_capture {
  int keys[2];
  long values[2];
  size_t count;
} cstl_semantic_map_capture;

static cmeta_status cstl_semantic_collect_map(
    void *context, const void *key, const void *value) {
  cstl_semantic_map_capture *out = (cstl_semantic_map_capture *)context;
  if (out == NULL || key == NULL || value == NULL)
    return CMETA_INVALID_ARGUMENT;
  out->keys[out->count] = *(const int *)key;
  out->values[out->count] = *(const long *)value;
  ++out->count;
  return CMETA_OK;
}

spec("CSTL semantic projection") {
  it("projects sequence-like containers without duplicating element type") {
    Vec(int, vec);
    Deque(int, deque);
    List(int, list);
    Stack(int, stack);
    Queue(int, queue);

    check_true(cmeta_container_data(&vec) == &cmeta_data_sequence);
    check_true(cmeta_container_data(&deque) == &cmeta_data_sequence);
    check_true(cmeta_container_data(&list) == &cmeta_data_sequence);
    check_true(cmeta_container_data(&stack) == &cmeta_data_sequence);
    check_true(cmeta_container_data(&queue) == &cmeta_data_sequence);
    check_true(cmeta_type_equal(cmeta_container_type_argument(&vec, 0u),
                                &cmeta_type_int));
  }

  it("projects set containers") {
    Set(int, set);
    HashSet(int, hash_set);

    check_true(cmeta_container_data(&set) == &cmeta_data_set);
    check_true(cmeta_container_data(&hash_set) == &cmeta_data_set);
  }

  it("projects map containers while type arguments stay in generic metadata") {
    HashMap(int, long, hash_map);
    Map(int, long, map);
    BTree(int, long, btree);
    BPlusTree(int, long, bplus_tree);

    check_true(cmeta_container_data(&hash_map) == &cmeta_data_map);
    check_true(cmeta_container_data(&map) == &cmeta_data_map);
    check_true(cmeta_container_data(&btree) == &cmeta_data_map);
    check_true(cmeta_container_data(&bplus_tree) == &cmeta_data_map);
    check_true(cmeta_type_equal(cmeta_container_type_argument(&map, 0u),
                                &cmeta_type_int));
    check_true(cmeta_type_equal(cmeta_container_type_argument(&map, 1u),
                                &cmeta_type_long));
  }

  it("leaves Heap and MultiMap semantically unresolved") {
    Heap(int, heap);
    MultiMap(int, long, multimap);

    check_null(cmeta_container_data(&heap));
    check_null(cmeta_container_data(&multimap));
    check_true(cmeta_container_type_application_valid(&heap));
    check_true(cmeta_container_type_application_valid(&multimap));
  }

  it("projects typed Vec through canonical CMeta collection reflection") {
    typed(Vec, reflected_ints, int);
    reflected_ints values = {0};
    cmeta_data_collection_view view = {0};
    check_equal(reflected_ints_init(&values, 8u), STL_OK);
    check_equal(reflected_ints_push(&values, 3), STL_OK);
    check_equal(reflected_ints_push(&values, 5), STL_OK);
    check_true(reflected_ints_collection_data.collection_ops ==
               &reflected_ints_collection_ops);
    check_equal(cmeta_data_collection_read(
                    &reflected_ints_collection_data, &values, &view),
                CMETA_OK);
    check_true(view.element == &cmeta_data_int);
    check_equal(view.count, 2u);
    check_equal(view.stride, sizeof(int));
    check_equal(*(const int *)view.data, 3);
    reflected_ints_destroy(&values);
  }


  it("projects typed Deque without claiming contiguous storage") {
    typed(Deque, reflected_deque, int);
    reflected_deque values = {0};
    int seen[2] = {0};
    cstl_semantic_collect_ints collected = {seen, 0u};
    check_equal(reflected_deque_init(&values, 8u), STL_OK);
    check_equal(reflected_deque_push_back(&values, 3), STL_OK);
    check_equal(reflected_deque_push_back(&values, 5), STL_OK);
    check_null(reflected_deque_collection_ops.read);
    check_true(reflected_deque_collection_ops.foreach != NULL);
    check_equal(cmeta_data_collection_foreach(
                    &reflected_deque_collection_data, &values,
                    cstl_semantic_collect_int, &collected, 2u),
                CMETA_OK);
    check_equal(collected.count, 2u);
    check_equal(seen[0], 3);
    check_equal(seen[1], 5);
    reflected_deque_destroy(&values);
  }


  it("projects typed List through linked iteration only") {
    typed(List, reflected_list, int);
    reflected_list values = {0};
    int seen[2] = {0};
    cstl_semantic_collect_ints collected = {seen, 0u};
    check_equal(reflected_list_init(&values, 8u), STL_OK);
    check_equal(reflected_list_push_back(&values, 7, NULL), STL_OK);
    check_equal(reflected_list_push_back(&values, 11, NULL), STL_OK);
    check_null(reflected_list_collection_ops.read);
    check_true(reflected_list_collection_ops.foreach != NULL);
    check_equal(cmeta_data_collection_foreach(
                    &reflected_list_collection_data, &values,
                    cstl_semantic_collect_int, &collected, 2u),
                CMETA_OK);
    check_equal(collected.count, 2u);
    check_equal(seen[0], 7);
    check_equal(seen[1], 11);
    reflected_list_destroy(&values);
  }


  it("projects typed Set and HashSet with SET semantics") {
    typed(Set, reflected_set, int);
    typed(HashSet, reflected_hash_set, int);
    reflected_set ordered = {0};
    reflected_hash_set hashed = {0};
    int ordered_seen[2] = {0};
    int hashed_seen[2] = {0};
    cstl_semantic_collect_ints ordered_out = {ordered_seen, 0u};
    cstl_semantic_collect_ints hashed_out = {hashed_seen, 0u};

    check_equal(reflected_set_init(&ordered, 8u), STL_OK);
    check_equal(reflected_set_add(&ordered, 5), STL_OK);
    check_equal(reflected_set_add(&ordered, 3), STL_OK);
    check_equal(reflected_hash_set_init(&hashed, 8u), STL_OK);
    check_equal(reflected_hash_set_add(&hashed, 5), STL_OK);
    check_equal(reflected_hash_set_add(&hashed, 3), STL_OK);

    check_equal(reflected_set_collection_data.kind, CMETA_DATA_SET);
    check_equal(reflected_hash_set_collection_data.kind, CMETA_DATA_SET);
    check_true(reflected_set_collection_element(&ordered) == &cmeta_data_int);
    check_true(reflected_hash_set_collection_element(&hashed) == &cmeta_data_int);

    check_equal(cmeta_data_collection_foreach(
                    &reflected_set_collection_data, &ordered,
                    cstl_semantic_collect_int, &ordered_out, 2u),
                CMETA_OK);
    check_equal(cmeta_data_collection_foreach(
                    &reflected_hash_set_collection_data, &hashed,
                    cstl_semantic_collect_int, &hashed_out, 2u),
                CMETA_OK);
    check_equal(ordered_out.count, 2u);
    check_equal(hashed_out.count, 2u);

    reflected_set_destroy(&ordered);
    reflected_hash_set_destroy(&hashed);
  }


  it("projects typed Map as key-value reflection, never pair sequence") {
    typed(Map, reflected_map, int, long);
    reflected_map values = {0};
    cstl_semantic_map_capture captured = {{0}, {0}, 0u};

    check_equal(reflected_map_init(&values, 8u), STL_OK);
    check_equal(reflected_map_put(&values, 5, 50L), STL_OK);
    check_equal(reflected_map_put(&values, 3, 30L), STL_OK);

    check_equal(reflected_map_map_data.kind, CMETA_DATA_MAP);
    check_true(reflected_map_map_data.collection_ops == NULL);
    check_true(reflected_map_map_data.map_ops == &reflected_map_map_ops);
    check_true(reflected_map_map_key(&values) == &cmeta_data_int);
    check_true(reflected_map_map_value(&values) == &cmeta_data_long);

    check_equal(cmeta_data_map_foreach(
                    &reflected_map_map_data, &values,
                    cstl_semantic_collect_map, &captured, 2u),
                CMETA_OK);
    check_equal(captured.count, 2u);
    check_equal(captured.keys[0], 3);
    check_equal(captured.values[0], 30L);
    check_equal(captured.keys[1], 5);
    check_equal(captured.values[1], 50L);

    reflected_map_destroy(&values);
  }


  it("projects HashMap and tree maps through one CMeta map contract") {
    typed(HashMap, reflected_hash_map, int, long);
    typed(BTree, reflected_btree, int, long);
    typed(BPlusTree, reflected_bplus, int, long);
    reflected_hash_map hashed = {0};
    reflected_btree btree = {0};
    reflected_bplus bplus = {0};
    cstl_semantic_map_capture hc = {{0}, {0}, 0u};
    cstl_semantic_map_capture tc = {{0}, {0}, 0u};
    cstl_semantic_map_capture pc = {{0}, {0}, 0u};

    check_equal(reflected_hash_map_init(&hashed, 8u), STL_OK);
    check_equal(reflected_btree_init(&btree, 8u), STL_OK);
    check_equal(reflected_bplus_init(&bplus, 8u), STL_OK);
    check_equal(reflected_hash_map_put(&hashed, 5, 50L), STL_OK);
    check_equal(reflected_hash_map_put(&hashed, 3, 30L), STL_OK);
    check_equal(reflected_btree_put(&btree, 5, 50L), STL_OK);
    check_equal(reflected_btree_put(&btree, 3, 30L), STL_OK);
    check_equal(reflected_bplus_put(&bplus, 5, 50L), STL_OK);
    check_equal(reflected_bplus_put(&bplus, 3, 30L), STL_OK);

    check_equal(cmeta_data_map_foreach(&reflected_hash_map_map_data, &hashed,
                    cstl_semantic_collect_map, &hc, 2u), CMETA_OK);
    check_equal(cmeta_data_map_foreach(&reflected_btree_map_data, &btree,
                    cstl_semantic_collect_map, &tc, 2u), CMETA_OK);
    check_equal(cmeta_data_map_foreach(&reflected_bplus_map_data, &bplus,
                    cstl_semantic_collect_map, &pc, 2u), CMETA_OK);
    check_equal(hc.count, 2u);
    check_equal(tc.count, 2u);
    check_equal(pc.count, 2u);
    check_equal(tc.keys[0], 3);
    check_equal(tc.keys[1], 5);
    check_equal(pc.keys[0], 3);
    check_equal(pc.keys[1], 5);

    reflected_hash_map_destroy(&hashed);
    reflected_btree_destroy(&btree);
    reflected_bplus_destroy(&bplus);
  }

}
