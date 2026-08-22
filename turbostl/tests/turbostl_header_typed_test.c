#include <turbostl/typed.h>
#include "tinytest.h"

suite("TurboSTL typed public header") {
    it("exposes self-describing declarations without generated type names") {
        Vec(int, vec);
        List(int, list);
        Map(int, int, map);

        check_equal(vec_init(&vec, 1u), STL_OK);
        check_equal(list_init(&list, 1u), STL_OK);
        check_equal(map_init(&map, 1u), STL_OK);

        map_destroy(&map);
        list_destroy(&list);
        vec_destroy(&vec);
    }

    it("binds unary kind descriptors and preserves them across reinitialization") {
        Vec(int, vec);
        Deque(int, deque);
        List(int, list);
        Stack(int, stack);
        Queue(int, queue);
        Heap(int, heap);
        Set(int, set);
        HashSet(int, hash_set);
        const cmeta_container_desc *vec_kind = cmeta_container_descriptor(&vec);
        const cmeta_container_desc *deque_kind = cmeta_container_descriptor(&deque);
        const cmeta_container_desc *list_kind = cmeta_container_descriptor(&list);
        const cmeta_container_desc *stack_kind = cmeta_container_descriptor(&stack);
        const cmeta_container_desc *queue_kind = cmeta_container_descriptor(&queue);
        const cmeta_container_desc *heap_kind = cmeta_container_descriptor(&heap);
        const cmeta_container_desc *set_kind = cmeta_container_descriptor(&set);
        const cmeta_container_desc *hash_set_kind = cmeta_container_descriptor(&hash_set);

        check_not_null(vec_kind);
        check_not_null(deque_kind);
        check_not_null(list_kind);
        check_not_null(stack_kind);
        check_not_null(queue_kind);
        check_not_null(heap_kind);
        check_not_null(set_kind);
        check_not_null(hash_set_kind);

        check_equal(vec_init(&vec, 2u), STL_OK);
        check_equal(deque_init(&deque, 2u), STL_OK);
        check_equal(list_init(&list, 2u), STL_OK);
        check_equal(stack_init(&stack, 2u), STL_OK);
        check_equal(queue_init(&queue, 2u), STL_OK);
        check_equal(heap_init(&heap, 2u), STL_OK);
        check_equal(set_init(&set, 2u), STL_OK);
        check_equal(hash_set_init(&hash_set, 2u), STL_OK);

        vec_destroy(&vec);
        deque_destroy(&deque);
        list_destroy(&list);
        stack_destroy(&stack);
        queue_destroy(&queue);
        heap_destroy(&heap);
        set_destroy(&set);
        hash_set_destroy(&hash_set);

        check_true(cmeta_container_descriptor(&vec) == vec_kind);
        check_true(cmeta_container_descriptor(&deque) == deque_kind);
        check_true(cmeta_container_descriptor(&list) == list_kind);
        check_true(cmeta_container_descriptor(&stack) == stack_kind);
        check_true(cmeta_container_descriptor(&queue) == queue_kind);
        check_true(cmeta_container_descriptor(&heap) == heap_kind);
        check_true(cmeta_container_descriptor(&set) == set_kind);
        check_true(cmeta_container_descriptor(&hash_set) == hash_set_kind);

        check_equal(vec_init(&vec, 1u), STL_OK);
        check_equal(deque_init(&deque, 1u), STL_OK);
        check_equal(list_init(&list, 1u), STL_OK);
        check_equal(stack_init(&stack, 1u), STL_OK);
        check_equal(queue_init(&queue, 1u), STL_OK);
        check_equal(heap_init(&heap, 1u), STL_OK);
        check_equal(set_init(&set, 1u), STL_OK);
        check_equal(hash_set_init(&hash_set, 1u), STL_OK);

        vec_destroy(&vec);
        deque_destroy(&deque);
        list_destroy(&list);
        stack_destroy(&stack);
        queue_destroy(&queue);
        heap_destroy(&heap);
        set_destroy(&set);
        hash_set_destroy(&hash_set);
    }
}
