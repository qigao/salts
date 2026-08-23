#include <cmeta/data.h>
#include <turbostl/typed.h>
#include "tinytest.h"

suite("TurboSTL semantic container data") {
    it("projects sequence semantics from proven type applications") {
        Vec(int, vec);
        Deque(int, deque);
        List(int, list);
        Stack(int, stack);
        Queue(int, queue);

        check_true(cmeta_container_data_descriptor(&vec) ==
                   &cmeta_data_sequence);
        check_true(cmeta_container_data_descriptor(&deque) ==
                   &cmeta_data_sequence);
        check_true(cmeta_container_data_descriptor(&list) ==
                   &cmeta_data_sequence);
        check_true(cmeta_container_data_descriptor(&stack) ==
                   &cmeta_data_sequence);
        check_true(cmeta_container_data_descriptor(&queue) ==
                   &cmeta_data_sequence);
        check_true(cmeta_type_equal(
            cmeta_container_type_argument(&vec, 0u), &cmeta_type_int));
    }

    it("projects set semantics from proven type applications") {
        Set(int, set);
        HashSet(int, hash_set);

        check_true(cmeta_container_data_descriptor(&set) == &cmeta_data_set);
        check_true(cmeta_container_data_descriptor(&hash_set) ==
                   &cmeta_data_set);
        check_true(cmeta_type_equal(
            cmeta_container_type_argument(&set, 0u), &cmeta_type_int));
    }

    it("projects map semantics while generic type owns K and V") {
        HashMap(int, long, hash_map);
        Map(int, long, map);
        BTree(int, long, btree);
        BPlusTree(int, long, bplus_tree);

        check_true(cmeta_container_data_descriptor(&hash_map) ==
                   &cmeta_data_map);
        check_true(cmeta_container_data_descriptor(&map) == &cmeta_data_map);
        check_true(cmeta_container_data_descriptor(&btree) == &cmeta_data_map);
        check_true(cmeta_container_data_descriptor(&bplus_tree) ==
                   &cmeta_data_map);
        check_true(cmeta_type_equal(
            cmeta_container_type_argument(&map, 0u), &cmeta_type_int));
        check_true(cmeta_type_equal(
            cmeta_container_type_argument(&map, 1u), &cmeta_type_long));
    }

    it("leaves heap and multimap semantically unresolved in v1") {
        Heap(int, heap);
        MultiMap(int, long, multimap);

        check_true(cmeta_container_type_application_valid(&heap));
        check_true(cmeta_container_type_application_valid(&multimap));
        check_null(cmeta_container_data_descriptor(&heap));
        check_null(cmeta_container_data_descriptor(&multimap));
    }

    it("does not project a raw byte vector without a generic application") {
        vec_t raw = {0};

        check_equal(vec_init_bytes(&raw, sizeof(int), _Alignof(int), 2u),
                    STL_OK);
        check_false(cmeta_container_type_application_valid(&raw));
        check_null(cmeta_container_data_descriptor(&raw));
        vec_raw_destroy_storage(&raw);
    }
}
