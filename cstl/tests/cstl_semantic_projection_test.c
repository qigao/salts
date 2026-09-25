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

}
