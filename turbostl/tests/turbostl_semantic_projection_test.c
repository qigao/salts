#include <turbostl/typed.h>
#include <cmeta/data.h>
#include "tinytest.h"

static const cmeta_data_desc *semantic_of(const void *object) {
    const cmeta_container_ext *ext = cmeta_container_extension(object);
    return ext != NULL ? ext->data : NULL;
}

spec("TurboSTL semantic projection") {
  it("projects sequence-like containers without duplicating element type") {
    Vec(int, vec);
    Deque(int, deque);
    List(int, list);
    Stack(int, stack);
    Queue(int, queue);

    check_true(semantic_of(&vec) == &cmeta_data_sequence);
    check_true(semantic_of(&deque) == &cmeta_data_sequence);
    check_true(semantic_of(&list) == &cmeta_data_sequence);
    check_true(semantic_of(&stack) == &cmeta_data_sequence);
    check_true(semantic_of(&queue) == &cmeta_data_sequence);
    check_true(cmeta_type_equal(cmeta_container_type_argument(&vec, 0u),
                                &cmeta_type_int));
  }

  it("projects set containers") {
    Set(int, set);
    HashSet(int, hash_set);

    check_true(semantic_of(&set) == &cmeta_data_set);
    check_true(semantic_of(&hash_set) == &cmeta_data_set);
  }

  it("projects map containers while type arguments stay in generic metadata") {
    HashMap(int, long, hash_map);
    Map(int, long, map);
    BTree(int, long, btree);
    BPlusTree(int, long, bplus_tree);

    check_true(semantic_of(&hash_map) == &cmeta_data_map);
    check_true(semantic_of(&map) == &cmeta_data_map);
    check_true(semantic_of(&btree) == &cmeta_data_map);
    check_true(semantic_of(&bplus_tree) == &cmeta_data_map);
    check_true(cmeta_type_equal(cmeta_container_type_argument(&map, 0u),
                                &cmeta_type_int));
    check_true(cmeta_type_equal(cmeta_container_type_argument(&map, 1u),
                                &cmeta_type_long));
  }

  it("leaves Heap and MultiMap semantically unresolved") {
    Heap(int, heap);
    MultiMap(int, long, multimap);

    check_null(semantic_of(&heap));
    check_null(semantic_of(&multimap));
    check_true(cmeta_container_type_application_valid(&heap));
    check_true(cmeta_container_type_application_valid(&multimap));
  }
}
