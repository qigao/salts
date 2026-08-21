#include <turbo/container.h>
#include "tinytest.h"

#include <stdint.h>

static int int_compare(const void *left, const void *right, void *context) {
    const int lhs = *(const int *)left;
    const int rhs = *(const int *)right;
    (void)context;
    return (lhs > rhs) - (lhs < rhs);
}

suite("Container sequences") {
    it("keeps raw vector values bounded and zero fills growth") {
        turbo_vec_t vec = {0};
        const int value = 7;

        check_equal(turbo_vec_init_bytes(&vec, sizeof(value), _Alignof(int), 3u),
                    CONTAINER_OK);
        check_equal(turbo_vec_push(&vec, &value), CONTAINER_OK);
        check_equal(turbo_vec_resize(&vec, 3u), CONTAINER_OK);
        check_equal(*(const int *)turbo_vec_at_const(&vec, 0u), 7);
        check_equal(*(const int *)turbo_vec_at_const(&vec, 1u), 0);
        check_equal(*(const int *)turbo_vec_at_const(&vec, 2u), 0);
        check_equal(turbo_vec_push(&vec, &value), CONTAINER_CAPACITY_EXCEEDED);
        turbo_vec_destroy(&vec);
    }

    it("rejects capacity byte overflow without changing the vector") {
        turbo_vec_t vec = {0};
        uint64_t generation_before;

        check_equal(turbo_vec_init_bytes(&vec, sizeof(uint64_t), _Alignof(uint64_t), SIZE_MAX),
                    CONTAINER_OK);
        generation_before = vec.generation;
        check_equal(turbo_vec_reserve(&vec, SIZE_MAX), CONTAINER_CAPACITY_EXCEEDED);
        check_equal(turbo_vec_size(&vec), (size_t)0u);
        check_equal(vec.generation, generation_before);
        turbo_vec_destroy(&vec);
    }

    it("preserves deque order and rejects a value past its limit") {
        turbo_deque_t deque = {0};
        const int one = 1;
        const int two = 2;
        const int three = 3;
        int out = 0;

        check_equal(turbo_deque_init_bytes(&deque, sizeof(one), _Alignof(int), 3u),
                    CONTAINER_OK);
        check_equal(turbo_deque_push_back(&deque, &two), CONTAINER_OK);
        check_equal(turbo_deque_push_front(&deque, &one), CONTAINER_OK);
        check_equal(turbo_deque_push_back(&deque, &three), CONTAINER_OK);
        check_equal(turbo_deque_pop_front(&deque, &out), CONTAINER_OK);
        check_equal(out, 1);
        check_equal(turbo_deque_push_back(&deque, &one), CONTAINER_OK);
        check_equal(turbo_deque_push_back(&deque, &one), CONTAINER_CAPACITY_EXCEEDED);
        check_equal(*(const int *)turbo_deque_front_const(&deque), 2);
        check_equal(*(const int *)turbo_deque_at_const(&deque, 1u), 3);
        turbo_deque_destroy(&deque);
    }

    it("uses raw heap comparator while preserving the heap invariant") {
        turbo_heap_t heap = {0};
        const int values[] = {5, 2, 9, 1};
        int previous = INT32_MIN;
        int out = 0;
        size_t index;

        check_equal(turbo_heap_init_bytes(&heap, sizeof(int), _Alignof(int), 4u,
                                          int_compare, NULL), CONTAINER_OK);
        for (index = 0u; index < sizeof(values) / sizeof(values[0]); ++index)
            check_equal(turbo_heap_push(&heap, &values[index]), CONTAINER_OK);
        while (turbo_heap_pop(&heap, &out) == CONTAINER_OK) {
            check_true(previous <= out);
            previous = out;
        }
        turbo_heap_destroy(&heap);
    }

    it("increments generation only for successful mutations") {
        turbo_vec_t vec = {0};
        const int value = 3;
        uint64_t generation;

        check_equal(turbo_vec_init_bytes(&vec, sizeof(value), _Alignof(int), 1u),
                    CONTAINER_OK);
        generation = vec.generation;
        check_equal(turbo_vec_push(&vec, &value), CONTAINER_OK);
        check_true(vec.generation > generation);
        generation = vec.generation;
        check_equal(turbo_vec_push(&vec, &value), CONTAINER_CAPACITY_EXCEEDED);
        check_equal(vec.generation, generation);
        turbo_vec_destroy(&vec);
    }

    it("honors explicit over-alignment") {
        turbo_vec_t vec = {0};
        const unsigned char byte = 1u;

        check_equal(turbo_vec_init_bytes(&vec, sizeof(byte), 64u, 1u), CONTAINER_OK);
        check_equal(turbo_vec_push(&vec, &byte), CONTAINER_OK);
        check_equal((uintptr_t)turbo_vec_data(&vec) % 64u, 0u);
        turbo_vec_destroy(&vec);
    }
}
