#include <turbostl.h>
#include "tinytest.h"

suite("TurboSTL public header") {
    it("infers a vector type without exposing a generated container type") {
        Vec(int, vec);
        int input = 7;
        int output = 0;

        check_equal(vec_init(&vec, 1u), STL_OK);
        check_equal(vec_push(&vec, &input), STL_OK);
        check_equal(vec_pop(&vec, &output), STL_OK);
        check_equal(output, 7);
        vec_destroy(&vec);

        check_equal(vec_init(&vec, 2u), STL_OK);
        vec_destroy(&vec);
    }

    it("infers deque stack queue and heap types from declarations") {
        Deque(int, deque);
        Stack(int, stack);
        Queue(int, queue);
        Heap(int, heap);
        int input = 5;
        int output = 0;

        check_equal(deque_init(&deque, 2u), STL_OK);
        check_equal(deque_push_back(&deque, &input), STL_OK);
        check_equal(deque_pop_front(&deque, &output), STL_OK);
        check_equal(output, 5);
        deque_destroy(&deque);
        check_equal(deque_init(&deque, 3u), STL_OK);
        deque_destroy(&deque);

        check_equal(stack_init(&stack, 2u), STL_OK);
        check_equal(stack_push(&stack, &input), STL_OK);
        check_equal(stack_pop(&stack, &output), STL_OK);
        check_equal(output, 5);
        stack_destroy(&stack);
        check_equal(stack_init(&stack, 3u), STL_OK);
        stack_destroy(&stack);

        check_equal(queue_init(&queue, 2u), STL_OK);
        check_equal(queue_push(&queue, &input), STL_OK);
        check_equal(queue_pop(&queue, &output), STL_OK);
        check_equal(output, 5);
        queue_destroy(&queue);
        check_equal(queue_init(&queue, 3u), STL_OK);
        queue_destroy(&queue);

        check_equal(heap_init(&heap, 2u), STL_OK);
        check_equal(heap_push(&heap, &input), STL_OK);
        check_equal(heap_pop(&heap, &output), STL_OK);
        check_equal(output, 5);
        heap_destroy(&heap);
        check_equal(heap_init(&heap, 3u), STL_OK);
        heap_destroy(&heap);
    }
}
