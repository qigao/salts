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

    it("exposes unary default ranges and instance collectors") {
        Vec(int, vec);
        Deque(int, deque);
        List(int, list);
        Stack(int, stack);
        Queue(int, queue);
        Heap(int, heap);
        Set(int, set);
        HashSet(int, hash_set);
        Vec(int, collected);
        Vec(int, aborted);
        cmeta_range range = {0};
        cmeta_collector collector;
        const cmeta_container_desc *desc;
        int value = 7;
        int out = 0;

        check_equal(vec_init(&vec, 2u), STL_OK);
        check_equal(deque_init(&deque, 2u), STL_OK);
        check_equal(list_init(&list, 2u), STL_OK);
        check_equal(stack_init(&stack, 2u), STL_OK);
        check_equal(queue_init(&queue, 2u), STL_OK);
        check_equal(heap_init(&heap, 2u), STL_OK);
        check_equal(set_init(&set, 2u), STL_OK);
        check_equal(hash_set_init(&hash_set, 2u), STL_OK);

        check_equal(vec_push(&vec, &value), STL_OK);
        check_equal(deque_push_back(&deque, &value), STL_OK);
        check_equal(list_push_back(&list, &value, NULL), STL_OK);
        check_equal(stack_push(&stack, &value), STL_OK);
        check_equal(queue_push(&queue, &value), STL_OK);
        check_equal(heap_push(&heap, &value), STL_OK);
        check_equal(set_add(&set, &value), STL_OK);
        check_equal(hash_set_add(&hash_set, &value), STL_OK);

#define CHECK_DEFAULT_RANGE(handle, required_flags, forbidden_flags) do {       \
    cmeta_range_cursor cursor = {0};                                           \
    range = (cmeta_range){0};                                                  \
    check_true(cmeta_container_range_view(&(handle),                           \
                                           CMETA_CONTAINER_VIEW_DEFAULT,        \
                                           &range));                            \
    check_true(cmeta_type_equal(range.element_type, &cmeta_type_int));         \
    check_true((range.flags & (required_flags)) == (required_flags));          \
    check_equal(range.flags & (forbidden_flags), (cmeta_range_flags)0u);       \
    out = 0;                                                                   \
    {                                                                          \
        cmeta_gen_status status = cmeta_range_next(&range, &cursor, &out);      \
        check_true(status == CMETA_GEN_VALUE ||                                \
                   status == CMETA_GEN_VALUE_AND_DONE);                        \
    }                                                                          \
    check_equal(out, value);                                                   \
} while (0)

        CHECK_DEFAULT_RANGE(vec,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_CONTIGUOUS |
                CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SORTED | CMETA_RANGE_UNIQUE);
        CHECK_DEFAULT_RANGE(deque,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED |
                CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SORTED);
        CHECK_DEFAULT_RANGE(list,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SORTED);
        CHECK_DEFAULT_RANGE(stack,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED |
                CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SORTED);
        CHECK_DEFAULT_RANGE(queue,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED |
                CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SORTED);
        CHECK_DEFAULT_RANGE(heap,
            CMETA_RANGE_SIZED | CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED);
        CHECK_DEFAULT_RANGE(set,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED |
                CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE,
            0u);
        CHECK_DEFAULT_RANGE(hash_set,
            CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED);
#undef CHECK_DEFAULT_RANGE

        desc = cmeta_container_descriptor(&collected);
        check_not_null(desc);
        check_not_null(desc->collector);
        if (desc->collector != NULL) {
            collector = desc->collector(&collected, 2u);
            check_equal(cmeta_collector_begin(&collector), CMETA_OK);
            check_equal(cmeta_collector_accept(&collector, &cmeta_type_int,
                                               &value), CMETA_OK);
            check_equal(cmeta_collector_finish(&collector), CMETA_OK);
            check_equal(vec_size(&collected), (size_t)1u);
            check_equal(*(const int *)vec_at_const(&collected, 0u), value);
            vec_destroy(&collected);
        }

        desc = cmeta_container_descriptor(&aborted);
        check_not_null(desc);
        check_not_null(desc->collector);
        if (desc->collector != NULL) {
            const cmeta_container_desc *kind = desc;
            collector = desc->collector(&aborted, 2u);
            check_equal(cmeta_collector_begin(&collector), CMETA_OK);
            check_equal(cmeta_collector_accept(&collector, &cmeta_type_int,
                                               &value), CMETA_OK);
            cmeta_collector_abort(&collector);
            check_true(cmeta_container_descriptor(&aborted) == kind);
            check_equal(vec_init(&aborted, 1u), STL_OK);
            vec_destroy(&aborted);
        }

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
