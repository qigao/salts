#include <turbo/stl.h>
#include "tinytest.h"

#include <stdint.h>
#include <string.h>

static int int_compare(const void *left, const void *right, void *context) {
    const int lhs = *(const int *)left;
    const int rhs = *(const int *)right;
    (void)context;
    return (lhs > rhs) - (lhs < rhs);
}

typedef struct large_heap_item {
    int priority;
    unsigned char payload[512];
} large_heap_item;

static int large_heap_compare(const void *left, const void *right, void *context) {
    const large_heap_item *lhs = (const large_heap_item *)left;
    const large_heap_item *rhs = (const large_heap_item *)right;
    (void)context;
    return (lhs->priority > rhs->priority) - (lhs->priority < rhs->priority);
}

suite("TurboSTL sequences") {
    it("keeps raw vector values bounded and zero fills growth") {
        turbo_vec_t vec = {0};
        const int value = 7;

        check_equal(turbo_vec_init_bytes(&vec, sizeof(value), _Alignof(int), 3u),
                    TURBO_STL_OK);
        check_equal(turbo_vec_push(&vec, &value), TURBO_STL_OK);
        check_equal(turbo_vec_resize(&vec, 3u), TURBO_STL_OK);
        check_equal(*(const int *)turbo_vec_at_const(&vec, 0u), 7);
        check_equal(*(const int *)turbo_vec_at_const(&vec, 1u), 0);
        check_equal(*(const int *)turbo_vec_at_const(&vec, 2u), 0);
        check_equal(turbo_vec_push(&vec, &value), TURBO_STL_CAPACITY_EXCEEDED);
        turbo_vec_destroy(&vec);
    }

    it("rejects capacity byte overflow without changing the vector") {
        turbo_vec_t vec = {0};
        uint64_t generation_before;

        check_equal(turbo_vec_init_bytes(&vec, sizeof(uint64_t), _Alignof(uint64_t), SIZE_MAX),
                    TURBO_STL_OK);
        generation_before = vec.generation;
        check_equal(turbo_vec_reserve(&vec, SIZE_MAX), TURBO_STL_CAPACITY_EXCEEDED);
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
                    TURBO_STL_OK);
        check_equal(turbo_deque_push_back(&deque, &two), TURBO_STL_OK);
        check_equal(turbo_deque_push_front(&deque, &one), TURBO_STL_OK);
        check_equal(turbo_deque_push_back(&deque, &three), TURBO_STL_OK);
        check_equal(turbo_deque_pop_front(&deque, &out), TURBO_STL_OK);
        check_equal(out, 1);
        check_equal(turbo_deque_push_back(&deque, &one), TURBO_STL_OK);
        check_equal(turbo_deque_push_back(&deque, &one), TURBO_STL_CAPACITY_EXCEEDED);
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
                                          int_compare, NULL), TURBO_STL_OK);
        for (index = 0u; index < sizeof(values) / sizeof(values[0]); ++index)
            check_equal(turbo_heap_push(&heap, &values[index]), TURBO_STL_OK);
        while (turbo_heap_pop(&heap, &out) == TURBO_STL_OK) {
            check_true(previous <= out);
            previous = out;
        }
        turbo_heap_destroy(&heap);
    }

    it("orders records larger than the former inline swap buffer") {
        turbo_heap_t heap = {0};
        const int priorities[] = {9, 1, 7, 3, 5};
        const int expected[] = {1, 3, 5, 7, 9};
        large_heap_item item;
        large_heap_item out;
        size_t index;

        check_equal(turbo_heap_init_bytes(&heap, sizeof(item), _Alignof(large_heap_item),
                                          5u, large_heap_compare, NULL),
                    TURBO_STL_OK);
        for (index = 0u; index < 5u; ++index) {
            memset(&item, priorities[index], sizeof(item));
            item.priority = priorities[index];
            check_equal(turbo_heap_push(&heap, &item), TURBO_STL_OK);
        }
        for (index = 0u; index < 5u; ++index) {
            check_equal(turbo_heap_pop(&heap, &out), TURBO_STL_OK);
            check_equal(out.priority, expected[index]);
        }
        turbo_heap_destroy(&heap);
    }

    it("increments generation only for successful mutations") {
        turbo_vec_t vec = {0};
        const int value = 3;
        uint64_t generation;

        check_equal(turbo_vec_init_bytes(&vec, sizeof(value), _Alignof(int), 1u),
                    TURBO_STL_OK);
        generation = vec.generation;
        check_equal(turbo_vec_push(&vec, &value), TURBO_STL_OK);
        check_true(vec.generation > generation);
        generation = vec.generation;
        check_equal(turbo_vec_push(&vec, &value), TURBO_STL_CAPACITY_EXCEEDED);
        check_equal(vec.generation, generation);
        turbo_vec_destroy(&vec);
    }

    it("honors explicit over-alignment") {
        turbo_vec_t vec = {0};
        const unsigned char byte = 1u;

        check_equal(turbo_vec_init_bytes(&vec, sizeof(byte), 64u, 1u), TURBO_STL_OK);
        check_equal(turbo_vec_push(&vec, &byte), TURBO_STL_OK);
        check_equal((uintptr_t)turbo_vec_data(&vec) % 64u, 0u);
        turbo_vec_destroy(&vec);
    }

    it("leaves destroyed byte handles unchanged when from-array exceeds the limit") {
        const int values[] = {1, 2};
        turbo_vec_t vec = {0};
        turbo_deque_t deque = {0};
        turbo_heap_t heap = {0};
        turbo_vec_t vec_before;
        turbo_deque_t deque_before;
        turbo_heap_t heap_before;

        check_equal(turbo_vec_init_bytes(&vec, sizeof(int), _Alignof(int), 1u), TURBO_STL_OK);
        turbo_vec_destroy(&vec);
        vec_before = vec;
        check_equal(turbo_vec_from_array_bytes(&vec, values, 2u, sizeof(int), _Alignof(int), 1u),
                    TURBO_STL_CAPACITY_EXCEEDED);
        check_equal(memcmp(&vec, &vec_before, sizeof(vec)), 0);
        check_equal(turbo_vec_from_array_bytes(&vec, values, 1u, sizeof(int), _Alignof(int), 1u),
                    TURBO_STL_OK);
        check_equal(vec.generation, vec_before.generation + UINT64_C(1));
        turbo_vec_destroy(&vec);

        check_equal(turbo_deque_init_bytes(&deque, sizeof(int), _Alignof(int), 1u), TURBO_STL_OK);
        turbo_deque_destroy(&deque);
        deque_before = deque;
        check_equal(turbo_deque_from_array_bytes(&deque, values, 2u, sizeof(int), _Alignof(int), 1u),
                    TURBO_STL_CAPACITY_EXCEEDED);
        check_equal(memcmp(&deque, &deque_before, sizeof(deque)), 0);
        check_equal(turbo_deque_from_array_bytes(&deque, values, 1u, sizeof(int), _Alignof(int), 1u),
                    TURBO_STL_OK);
        check_equal(deque.generation, deque_before.generation + UINT64_C(1));
        turbo_deque_destroy(&deque);

        check_equal(turbo_heap_init_bytes(&heap, sizeof(int), _Alignof(int), 1u, int_compare, NULL),
                    TURBO_STL_OK);
        turbo_heap_destroy(&heap);
        heap_before = heap;
        check_equal(turbo_heap_from_array_bytes(&heap, values, 2u, sizeof(int), _Alignof(int), 1u,
                                                 int_compare, NULL), TURBO_STL_CAPACITY_EXCEEDED);
        check_equal(memcmp(&heap, &heap_before, sizeof(heap)), 0);
        check_equal(turbo_heap_from_array_bytes(&heap, values, 1u, sizeof(int), _Alignof(int), 1u,
                                                 int_compare, NULL), TURBO_STL_OK);
        check_equal(heap.generation, heap_before.generation + UINT64_C(1));
        turbo_heap_destroy(&heap);
    }

    it("rejects repeated initialization without changing live handles") {
        turbo_vec_t vec = {0};
        turbo_deque_t deque = {0};
        turbo_heap_t heap = {0};
        turbo_vec_t vec_before;
        turbo_deque_t deque_before;
        turbo_heap_t heap_before;
        const int value = 1;

        check_equal(turbo_vec_init_bytes(&vec, sizeof(int), _Alignof(int), 1u), TURBO_STL_OK);
        vec_before = vec;
        check_equal(turbo_vec_init_bytes(&vec, sizeof(int), _Alignof(int), 1u),
                    TURBO_STL_INVALID_ARGUMENT);
        check_equal(memcmp(&vec, &vec_before, sizeof(vec)), 0);
        check_equal(turbo_vec_from_array_bytes(&vec, &value, 1u, sizeof(int), _Alignof(int), 1u),
                    TURBO_STL_INVALID_ARGUMENT);
        check_equal(memcmp(&vec, &vec_before, sizeof(vec)), 0);
        turbo_vec_destroy(&vec);

        check_equal(turbo_deque_init_bytes(&deque, sizeof(int), _Alignof(int), 1u), TURBO_STL_OK);
        deque_before = deque;
        check_equal(turbo_deque_init_bytes(&deque, sizeof(int), _Alignof(int), 1u),
                    TURBO_STL_INVALID_ARGUMENT);
        check_equal(memcmp(&deque, &deque_before, sizeof(deque)), 0);
        check_equal(turbo_deque_from_array_bytes(&deque, &value, 1u, sizeof(int), _Alignof(int), 1u),
                    TURBO_STL_INVALID_ARGUMENT);
        check_equal(memcmp(&deque, &deque_before, sizeof(deque)), 0);
        turbo_deque_destroy(&deque);

        check_equal(turbo_heap_init_bytes(&heap, sizeof(int), _Alignof(int), 1u, int_compare, NULL),
                    TURBO_STL_OK);
        heap_before = heap;
        check_equal(turbo_heap_init_bytes(&heap, sizeof(int), _Alignof(int), 1u, int_compare, NULL),
                    TURBO_STL_INVALID_ARGUMENT);
        check_equal(memcmp(&heap, &heap_before, sizeof(heap)), 0);
        check_equal(turbo_heap_from_array_bytes(&heap, &value, 1u, sizeof(int), _Alignof(int), 1u,
                                                 int_compare, NULL), TURBO_STL_INVALID_ARGUMENT);
        check_equal(memcmp(&heap, &heap_before, sizeof(heap)), 0);
        turbo_heap_destroy(&heap);
    }

    it("does not write removal output on failure") {
        turbo_vec_t vec = {0};
        turbo_deque_t deque = {0};
        turbo_heap_t heap = {0};
        int output = 91;

        check_equal(turbo_vec_init_bytes(&vec, sizeof(int), _Alignof(int), 1u), TURBO_STL_OK);
        check_equal(turbo_vec_pop(&vec, &output), TURBO_STL_EMPTY);
        check_equal(output, 91);
        check_equal(turbo_vec_erase(&vec, 0u, &output), TURBO_STL_INVALID_ARGUMENT);
        check_equal(output, 91);
        check_equal(turbo_vec_swap_remove(&vec, 0u, &output), TURBO_STL_INVALID_ARGUMENT);
        check_equal(output, 91);
        turbo_vec_destroy(&vec);

        check_equal(turbo_deque_init_bytes(&deque, sizeof(int), _Alignof(int), 1u), TURBO_STL_OK);
        check_equal(turbo_deque_pop_back(&deque, &output), TURBO_STL_EMPTY);
        check_equal(output, 91);
        check_equal(turbo_deque_pop_front(&deque, &output), TURBO_STL_EMPTY);
        check_equal(output, 91);
        turbo_deque_destroy(&deque);

        check_equal(turbo_heap_init_bytes(&heap, sizeof(int), _Alignof(int), 1u, int_compare, NULL),
                    TURBO_STL_OK);
        check_equal(turbo_heap_pop(&heap, &output), TURBO_STL_EMPTY);
        check_equal(output, 91);
        turbo_heap_destroy(&heap);
    }
}
