#include <turbo/container.h>
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

typedef struct counted_value {
    int *value;
} counted_value;

static size_t counted_copies;
static size_t counted_moves;
static size_t counted_destroys;
static bool counted_copy_allowed;
static size_t counted_copy_fail_on;

static bool counted_copy(void *destination_, const void *source_) {
    counted_value *destination = (counted_value *)destination_;
    const counted_value *source = (const counted_value *)source_;

    if (!counted_copy_allowed || (counted_copy_fail_on != 0u &&
        counted_copies + 1u == counted_copy_fail_on) || destination == NULL || source == NULL ||
        source->value == NULL)
        return false;
    destination->value = (int *)malloc(sizeof(*destination->value));
    if (destination->value == NULL)
        return false;
    *destination->value = *source->value;
    ++counted_copies;
    return true;
}

static void counted_move(void *destination_, void *source_) {
    counted_value *destination = (counted_value *)destination_;
    counted_value *source = (counted_value *)source_;

    destination->value = source->value;
    source->value = NULL;
    ++counted_moves;
}

static void counted_destroy(void *value_) {
    counted_value *value = (counted_value *)value_;

    free(value->value);
    value->value = NULL;
    ++counted_destroys;
}

static int counted_compare(const void *left_, const void *right_) {
    const counted_value *left = (const counted_value *)left_;
    const counted_value *right = (const counted_value *)right_;
    return (*left->value > *right->value) - (*left->value < *right->value);
}

static const cmeta_type_traits counted_traits = {
    CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY | CMETA_TRAIT_COMPARE,
    NULL, NULL, counted_compare, counted_copy, counted_move, counted_destroy
};

static const cmeta_type_desc counted_type = {
    "counted_value", sizeof(counted_value), _Alignof(counted_value), CMETA_T_OBJECT,
    NULL, &counted_traits
};

static counted_value counted_source(int value) {
    counted_value source;
    source.value = (int *)malloc(sizeof(*source.value));
    *source.value = value;
    return source;
}

static void counted_reset(void) {
    counted_copies = 0u;
    counted_moves = 0u;
    counted_destroys = 0u;
    counted_copy_allowed = true;
    counted_copy_fail_on = 0u;
}

suite("Container ownership") {
    it("transfers typed vector removals and balances constructed values") {
        turbo_vec_t vec = {0};
        counted_value first;
        counted_value second;
        counted_value out = {0};
        uint64_t generation;

        counted_reset();
        first = counted_source(1);
        second = counted_source(2);
        check_equal(turbo_vec_init(&vec, &counted_type, 3u), CONTAINER_OK);
        check_equal(turbo_vec_push(&vec, &first), CONTAINER_OK);
        check_equal(turbo_vec_push(&vec, &second), CONTAINER_OK);
        generation = vec.generation;
        check_equal(turbo_vec_reserve(&vec, 4u), CONTAINER_CAPACITY_EXCEEDED);
        check_equal(vec.generation, generation);
        check_equal(turbo_vec_set(&vec, 0u, &second), CONTAINER_OK);
        check_equal(turbo_vec_pop(&vec, &out), CONTAINER_OK);
        check_equal(*out.value, 2);
        counted_destroy(&out);
        check_equal(turbo_vec_clear(&vec), CONTAINER_OK);
        turbo_vec_destroy(&vec);
        counted_destroy(&first);
        counted_destroy(&second);
        check_equal(counted_copies, (size_t)3u);
        check_equal(counted_destroys, (size_t)9u);
    }

    it("leaves a typed vector unchanged when copying fails") {
        turbo_vec_t vec = {0};
        counted_value source;
        uint64_t generation;

        counted_reset();
        source = counted_source(7);
        check_equal(turbo_vec_init(&vec, &counted_type, 2u), CONTAINER_OK);
        check_equal(turbo_vec_push(&vec, &source), CONTAINER_OK);
        generation = vec.generation;
        counted_copy_allowed = false;
        check_equal(turbo_vec_set(&vec, 0u, &source), CONTAINER_OUT_OF_MEMORY);
        check_equal(*((const counted_value *)turbo_vec_at_const(&vec, 0u))->value, 7);
        check_equal(vec.generation, generation);
        turbo_vec_destroy(&vec);
        counted_destroy(&source);
    }

    it("uses typed deque destruction and heap comparison traits") {
        turbo_deque_t deque = {0};
        turbo_heap_t heap = {0};
        counted_value low;
        counted_value high;
        counted_value out = {0};

        counted_reset();
        low = counted_source(2);
        high = counted_source(9);
        check_equal(turbo_deque_init(&deque, &counted_type, 2u), CONTAINER_OK);
        check_equal(turbo_deque_push_back(&deque, &low), CONTAINER_OK);
        check_equal(turbo_deque_pop_back(&deque, NULL), CONTAINER_OK);
        check_equal(counted_destroys, (size_t)2u);
        turbo_deque_destroy(&deque);
        check_equal(turbo_heap_init(&heap, &counted_type, 2u), CONTAINER_OK);
        check_equal(turbo_heap_push(&heap, &high), CONTAINER_OK);
        check_equal(turbo_heap_push(&heap, &low), CONTAINER_OK);
        check_equal(turbo_heap_pop(&heap, &out), CONTAINER_OK);
        check_equal(*out.value, 2);
        counted_destroy(&out);
        turbo_heap_destroy(&heap);
        counted_destroy(&low);
        counted_destroy(&high);
    }

    it("does not mutate typed deque or heap when copying fails") {
        turbo_deque_t deque = {0};
        turbo_heap_t heap = {0};
        counted_value source;
        uint64_t deque_generation;
        uint64_t heap_generation;

        counted_reset();
        source = counted_source(4);
        check_equal(turbo_deque_init(&deque, &counted_type, 2u), CONTAINER_OK);
        check_equal(turbo_heap_init(&heap, &counted_type, 2u), CONTAINER_OK);
        deque_generation = deque.generation;
        heap_generation = heap.generation;
        counted_copy_allowed = false;
        check_equal(turbo_deque_push_back(&deque, &source), CONTAINER_OUT_OF_MEMORY);
        check_equal(turbo_heap_push(&heap, &source), CONTAINER_OUT_OF_MEMORY);
        check_equal(turbo_deque_size(&deque), (size_t)0u);
        check_equal(turbo_heap_size(&heap), (size_t)0u);
        check_equal(deque.generation, deque_generation);
        check_equal(heap.generation, heap_generation);
        turbo_deque_destroy(&deque);
        turbo_heap_destroy(&heap);
        counted_destroy(&source);
    }

    it("rejects typed vector growth because no default constructor exists") {
        turbo_vec_t vec = {0};
        counted_value source;
        uint64_t generation;

        counted_reset();
        source = counted_source(5);
        check_equal(turbo_vec_init(&vec, &counted_type, 2u), CONTAINER_OK);
        check_equal(turbo_vec_push(&vec, &source), CONTAINER_OK);
        generation = vec.generation;
        check_equal(turbo_vec_resize(&vec, 2u), CONTAINER_TRAIT_MISSING);
        check_equal(turbo_vec_size(&vec), (size_t)1u);
        check_equal(vec.generation, generation);
        turbo_vec_destroy(&vec);
        counted_destroy(&source);
    }

    it("leaves destroyed vector handle unchanged when typed from-array copy fails") {
        turbo_vec_t vec = {0};
        turbo_vec_t before;
        counted_value values[3];

        counted_reset();
        values[0] = counted_source(1);
        values[1] = counted_source(2);
        values[2] = counted_source(3);
        check_equal(turbo_vec_init(&vec, &counted_type, 3u), CONTAINER_OK);
        turbo_vec_destroy(&vec);
        before = vec;
        counted_copy_fail_on = 2u;
        check_equal(turbo_vec_from_array(&vec, values, 3u, &counted_type, 3u),
                    CONTAINER_OUT_OF_MEMORY);
        check_equal(memcmp(&vec, &before, sizeof(vec)), 0);
        counted_destroy(&values[0]);
        counted_destroy(&values[1]);
        counted_destroy(&values[2]);
        check_equal(counted_copies, (size_t)1u);
        check_equal(counted_destroys, (size_t)5u);
    }

    it("leaves destroyed deque handle unchanged when typed from-array copy fails") {
        turbo_deque_t deque = {0};
        turbo_deque_t before;
        counted_value values[3];

        counted_reset();
        values[0] = counted_source(1);
        values[1] = counted_source(2);
        values[2] = counted_source(3);
        check_equal(turbo_deque_init(&deque, &counted_type, 3u), CONTAINER_OK);
        turbo_deque_destroy(&deque);
        before = deque;
        counted_copy_fail_on = 2u;
        check_equal(turbo_deque_from_array(&deque, values, 3u, &counted_type, 3u),
                    CONTAINER_OUT_OF_MEMORY);
        check_equal(memcmp(&deque, &before, sizeof(deque)), 0);
        counted_destroy(&values[0]);
        counted_destroy(&values[1]);
        counted_destroy(&values[2]);
        check_equal(counted_copies, (size_t)1u);
        check_equal(counted_destroys, (size_t)5u);
    }

    it("leaves destroyed heap handle unchanged when typed from-array copy fails") {
        turbo_heap_t heap = {0};
        turbo_heap_t before;
        counted_value values[3];

        counted_reset();
        values[0] = counted_source(1);
        values[1] = counted_source(2);
        values[2] = counted_source(3);
        check_equal(turbo_heap_init(&heap, &counted_type, 3u), CONTAINER_OK);
        turbo_heap_destroy(&heap);
        before = heap;
        counted_copy_fail_on = 2u;
        check_equal(turbo_heap_from_array(&heap, values, 3u, &counted_type, 3u),
                    CONTAINER_OUT_OF_MEMORY);
        check_equal(memcmp(&heap, &before, sizeof(heap)), 0);
        counted_destroy(&values[0]);
        counted_destroy(&values[1]);
        counted_destroy(&values[2]);
        check_equal(counted_copies, (size_t)1u);
        check_equal(counted_destroys, (size_t)5u);
    }
}
