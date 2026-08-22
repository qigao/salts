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

    it("infers set hash-set and hash-map types from declarations") {
        Set(int, ordered);
        HashSet(int, hashed);
        HashMap(int, int, table);
        int key = 4;
        int value = 16;

        check_equal(set_init(&ordered, 4u), STL_OK);
        check_equal(set_add(&ordered, &key), STL_OK);
        check_true(set_contains(&ordered, &key));
        set_destroy(&ordered);
        check_equal(set_init(&ordered, 8u), STL_OK);
        set_destroy(&ordered);

        check_equal(hash_set_init(&hashed, 4u), STL_OK);
        check_equal(hash_set_add(&hashed, &key), STL_OK);
        check_true(hash_set_contains(&hashed, &key));
        hash_set_destroy(&hashed);
        check_equal(hash_set_init(&hashed, 8u), STL_OK);
        hash_set_destroy(&hashed);

        check_equal(hash_map_init(&table, 4u), STL_OK);
        check_equal(hash_map_put(&table, &key, &value), STL_OK);
        check_equal(*(const int *)hash_map_get_const(&table, &key), 16);
        hash_map_destroy(&table);
        check_equal(hash_map_init(&table, 8u), STL_OK);
        hash_map_destroy(&table);
    }
}
