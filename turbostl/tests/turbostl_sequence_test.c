#include <turbostl.h>
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
        vec_t vec = {0};
        const int value = 7;

        check_equal(vec_init_bytes(&vec, sizeof(value), _Alignof(int), 3u),
                    STL_OK);
        check_equal(vec_push(&vec, &value), STL_OK);
        check_equal(vec_resize(&vec, 3u), STL_OK);
        check_equal(*(const int *)vec_at_const(&vec, 0u), 7);
        check_equal(*(const int *)vec_at_const(&vec, 1u), 0);
        check_equal(*(const int *)vec_at_const(&vec, 2u), 0);
        check_equal(vec_push(&vec, &value), STL_CAPACITY_EXCEEDED);
        vec_raw_destroy_storage(&vec);
    }

    it("rejects capacity byte overflow without changing the vector") {
        vec_t vec = {0};
        uint64_t generation_before;

        check_equal(vec_init_bytes(&vec, sizeof(uint64_t), _Alignof(uint64_t), SIZE_MAX),
                    STL_OK);
        generation_before = vec.generation;
        check_equal(vec_reserve(&vec, SIZE_MAX), STL_CAPACITY_EXCEEDED);
        check_equal(vec_size(&vec), (size_t)0u);
        check_equal(vec.generation, generation_before);
        vec_raw_destroy_storage(&vec);
    }

    it("preserves deque order and rejects a value past its limit") {
        deque_t deque = {0};
        const int one = 1;
        const int two = 2;
        const int three = 3;
        int out = 0;

        check_equal(deque_init_bytes(&deque, sizeof(one), _Alignof(int), 3u),
                    STL_OK);
        check_equal(deque_push_back(&deque, &two), STL_OK);
        check_equal(deque_push_front(&deque, &one), STL_OK);
        check_equal(deque_push_back(&deque, &three), STL_OK);
        check_equal(deque_pop_front(&deque, &out), STL_OK);
        check_equal(out, 1);
        check_equal(deque_push_back(&deque, &one), STL_OK);
        check_equal(deque_push_back(&deque, &one), STL_CAPACITY_EXCEEDED);
        check_equal(*(const int *)deque_front_const(&deque), 2);
        check_equal(*(const int *)deque_at_const(&deque, 1u), 3);
        deque_raw_destroy_storage(&deque);
    }

    it("uses raw heap comparator while preserving the heap invariant") {
        heap_t heap = {0};
        const int values[] = {5, 2, 9, 1};
        int previous = INT32_MIN;
        int out = 0;
        size_t index;

        check_equal(heap_init_bytes(&heap, sizeof(int), _Alignof(int), 4u,
                                          int_compare, NULL), STL_OK);
        for (index = 0u; index < sizeof(values) / sizeof(values[0]); ++index)
            check_equal(heap_push(&heap, &values[index]), STL_OK);
        while (heap_pop(&heap, &out) == STL_OK) {
            check_true(previous <= out);
            previous = out;
        }
        heap_raw_destroy_storage(&heap);
    }

    it("orders records larger than the former inline swap buffer") {
        heap_t heap = {0};
        const int priorities[] = {9, 1, 7, 3, 5};
        const int expected[] = {1, 3, 5, 7, 9};
        large_heap_item item;
        large_heap_item out;
        size_t index;

        check_equal(heap_init_bytes(&heap, sizeof(item), _Alignof(large_heap_item),
                                          5u, large_heap_compare, NULL),
                    STL_OK);
        for (index = 0u; index < 5u; ++index) {
            memset(&item, priorities[index], sizeof(item));
            item.priority = priorities[index];
            check_equal(heap_push(&heap, &item), STL_OK);
        }
        for (index = 0u; index < 5u; ++index) {
            check_equal(heap_pop(&heap, &out), STL_OK);
            check_equal(out.priority, expected[index]);
        }
        heap_raw_destroy_storage(&heap);
    }

    it("increments generation only for successful mutations") {
        vec_t vec = {0};
        const int value = 3;
        uint64_t generation;

        check_equal(vec_init_bytes(&vec, sizeof(value), _Alignof(int), 1u),
                    STL_OK);
        generation = vec.generation;
        check_equal(vec_push(&vec, &value), STL_OK);
        check_true(vec.generation > generation);
        generation = vec.generation;
        check_equal(vec_push(&vec, &value), STL_CAPACITY_EXCEEDED);
        check_equal(vec.generation, generation);
        vec_raw_destroy_storage(&vec);
    }

    it("honors explicit over-alignment") {
        vec_t vec = {0};
        const unsigned char byte = 1u;

        check_equal(vec_init_bytes(&vec, sizeof(byte), 64u, 1u), STL_OK);
        check_equal(vec_push(&vec, &byte), STL_OK);
        check_equal((uintptr_t)vec_data(&vec) % 64u, 0u);
        vec_raw_destroy_storage(&vec);
    }

    it("leaves destroyed byte handles unchanged when from-array exceeds the limit") {
        const int values[] = {1, 2};
        vec_t vec = {0};
        deque_t deque = {0};
        heap_t heap = {0};
        vec_t vec_before;
        deque_t deque_before;
        heap_t heap_before;

        check_equal(vec_init_bytes(&vec, sizeof(int), _Alignof(int), 1u), STL_OK);
        vec_raw_destroy_storage(&vec);
        vec_before = vec;
        check_equal(vec_from_array_bytes(&vec, values, 2u, sizeof(int), _Alignof(int), 1u),
                    STL_CAPACITY_EXCEEDED);
        check_equal(memcmp(&vec, &vec_before, sizeof(vec)), 0);
        check_equal(vec_from_array_bytes(&vec, values, 1u, sizeof(int), _Alignof(int), 1u),
                    STL_OK);
        check_equal(vec.generation, vec_before.generation + UINT64_C(1));
        vec_raw_destroy_storage(&vec);

        check_equal(deque_init_bytes(&deque, sizeof(int), _Alignof(int), 1u), STL_OK);
        deque_raw_destroy_storage(&deque);
        deque_before = deque;
        check_equal(deque_from_array_bytes(&deque, values, 2u, sizeof(int), _Alignof(int), 1u),
                    STL_CAPACITY_EXCEEDED);
        check_equal(memcmp(&deque, &deque_before, sizeof(deque)), 0);
        check_equal(deque_from_array_bytes(&deque, values, 1u, sizeof(int), _Alignof(int), 1u),
                    STL_OK);
        check_equal(deque.generation, deque_before.generation + UINT64_C(1));
        deque_raw_destroy_storage(&deque);

        check_equal(heap_init_bytes(&heap, sizeof(int), _Alignof(int), 1u, int_compare, NULL),
                    STL_OK);
        heap_raw_destroy_storage(&heap);
        heap_before = heap;
        check_equal(heap_from_array_bytes(&heap, values, 2u, sizeof(int), _Alignof(int), 1u,
                                                 int_compare, NULL), STL_CAPACITY_EXCEEDED);
        check_equal(memcmp(&heap, &heap_before, sizeof(heap)), 0);
        check_equal(heap_from_array_bytes(&heap, values, 1u, sizeof(int), _Alignof(int), 1u,
                                                 int_compare, NULL), STL_OK);
        check_equal(heap.generation, heap_before.generation + UINT64_C(1));
        heap_raw_destroy_storage(&heap);
    }

    it("rejects repeated initialization without changing live handles") {
        vec_t vec = {0};
        deque_t deque = {0};
        heap_t heap = {0};
        vec_t vec_before;
        deque_t deque_before;
        heap_t heap_before;
        const int value = 1;

        check_equal(vec_init_bytes(&vec, sizeof(int), _Alignof(int), 1u), STL_OK);
        vec_before = vec;
        check_equal(vec_init_bytes(&vec, sizeof(int), _Alignof(int), 1u),
                    STL_INVALID_ARGUMENT);
        check_equal(memcmp(&vec, &vec_before, sizeof(vec)), 0);
        check_equal(vec_from_array_bytes(&vec, &value, 1u, sizeof(int), _Alignof(int), 1u),
                    STL_INVALID_ARGUMENT);
        check_equal(memcmp(&vec, &vec_before, sizeof(vec)), 0);
        vec_raw_destroy_storage(&vec);

        check_equal(deque_init_bytes(&deque, sizeof(int), _Alignof(int), 1u), STL_OK);
        deque_before = deque;
        check_equal(deque_init_bytes(&deque, sizeof(int), _Alignof(int), 1u),
                    STL_INVALID_ARGUMENT);
        check_equal(memcmp(&deque, &deque_before, sizeof(deque)), 0);
        check_equal(deque_from_array_bytes(&deque, &value, 1u, sizeof(int), _Alignof(int), 1u),
                    STL_INVALID_ARGUMENT);
        check_equal(memcmp(&deque, &deque_before, sizeof(deque)), 0);
        deque_raw_destroy_storage(&deque);

        check_equal(heap_init_bytes(&heap, sizeof(int), _Alignof(int), 1u, int_compare, NULL),
                    STL_OK);
        heap_before = heap;
        check_equal(heap_init_bytes(&heap, sizeof(int), _Alignof(int), 1u, int_compare, NULL),
                    STL_INVALID_ARGUMENT);
        check_equal(memcmp(&heap, &heap_before, sizeof(heap)), 0);
        check_equal(heap_from_array_bytes(&heap, &value, 1u, sizeof(int), _Alignof(int), 1u,
                                                 int_compare, NULL), STL_INVALID_ARGUMENT);
        check_equal(memcmp(&heap, &heap_before, sizeof(heap)), 0);
        heap_raw_destroy_storage(&heap);
    }

    it("does not write removal output on failure") {
        vec_t vec = {0};
        deque_t deque = {0};
        heap_t heap = {0};
        int output = 91;

        check_equal(vec_init_bytes(&vec, sizeof(int), _Alignof(int), 1u), STL_OK);
        check_equal(vec_pop(&vec, &output), STL_EMPTY);
        check_equal(output, 91);
        check_equal(vec_erase(&vec, 0u, &output), STL_INVALID_ARGUMENT);
        check_equal(output, 91);
        check_equal(vec_swap_remove(&vec, 0u, &output), STL_INVALID_ARGUMENT);
        check_equal(output, 91);
        vec_raw_destroy_storage(&vec);

        check_equal(deque_init_bytes(&deque, sizeof(int), _Alignof(int), 1u), STL_OK);
        check_equal(deque_pop_back(&deque, &output), STL_EMPTY);
        check_equal(output, 91);
        check_equal(deque_pop_front(&deque, &output), STL_EMPTY);
        check_equal(output, 91);
        deque_raw_destroy_storage(&deque);

        check_equal(heap_init_bytes(&heap, sizeof(int), _Alignof(int), 1u, int_compare, NULL),
                    STL_OK);
        check_equal(heap_pop(&heap, &output), STL_EMPTY);
        check_equal(output, 91);
        heap_raw_destroy_storage(&heap);
    }
}
