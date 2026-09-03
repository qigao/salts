#include <cstl.h>
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

static const cmeta_type_desc missing_traits_type = {
    "missing_traits", sizeof(counted_value), _Alignof(counted_value), CMETA_T_OBJECT,
    NULL, NULL
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

suite("CSTL ownership") {
    it("transfers typed vector removals and balances constructed values") {
        vec_t vec = {0};
        counted_value first;
        counted_value second;
        counted_value out = {0};
        uint64_t generation;

        counted_reset();
        first = counted_source(1);
        second = counted_source(2);
        check_equal(vec_raw_init(&vec, &counted_type, 3u), STL_OK);
        check_equal(vec_push(&vec, &first), STL_OK);
        check_equal(vec_push(&vec, &second), STL_OK);
        generation = vec.generation;
        check_equal(vec_reserve(&vec, 4u), STL_CAPACITY_EXCEEDED);
        check_equal(vec.generation, generation);
        check_equal(vec_set(&vec, 0u, &second), STL_OK);
        check_equal(vec_pop(&vec, &out), STL_OK);
        check_equal(*out.value, 2);
        counted_destroy(&out);
        check_equal(vec_clear(&vec), STL_OK);
        vec_raw_destroy_storage(&vec);
        counted_destroy(&first);
        counted_destroy(&second);
        check_equal(counted_copies, (size_t)3u);
        check_equal(counted_destroys, (size_t)9u);
    }

    it("leaves a typed vector unchanged when copying fails") {
        vec_t vec = {0};
        counted_value source;
        uint64_t generation;

        counted_reset();
        source = counted_source(7);
        check_equal(vec_raw_init(&vec, &counted_type, 2u), STL_OK);
        check_equal(vec_push(&vec, &source), STL_OK);
        generation = vec.generation;
        counted_copy_allowed = false;
        check_equal(vec_set(&vec, 0u, &source), STL_OUT_OF_MEMORY);
        check_equal(*((const counted_value *)vec_at_const(&vec, 0u))->value, 7);
        check_equal(vec.generation, generation);
        vec_raw_destroy_storage(&vec);
        counted_destroy(&source);
    }

    it("uses typed deque destruction and heap comparison traits") {
        deque_t deque = {0};
        heap_t heap = {0};
        counted_value low;
        counted_value high;
        counted_value out = {0};

        counted_reset();
        low = counted_source(2);
        high = counted_source(9);
        check_equal(deque_raw_init(&deque, &counted_type, 2u), STL_OK);
        check_equal(deque_push_back(&deque, &low), STL_OK);
        check_equal(deque_pop_back(&deque, NULL), STL_OK);
        check_equal(counted_destroys, (size_t)2u);
        deque_raw_destroy_storage(&deque);
        check_equal(heap_raw_init(&heap, &counted_type, 2u), STL_OK);
        check_equal(heap_push(&heap, &high), STL_OK);
        check_equal(heap_push(&heap, &low), STL_OK);
        check_equal(heap_pop(&heap, &out), STL_OK);
        check_equal(*out.value, 2);
        counted_destroy(&out);
        heap_raw_destroy_storage(&heap);
        counted_destroy(&low);
        counted_destroy(&high);
    }

    it("does not mutate typed deque or heap when copying fails") {
        deque_t deque = {0};
        heap_t heap = {0};
        counted_value source;
        uint64_t deque_generation;
        uint64_t heap_generation;

        counted_reset();
        source = counted_source(4);
        check_equal(deque_raw_init(&deque, &counted_type, 2u), STL_OK);
        check_equal(heap_raw_init(&heap, &counted_type, 2u), STL_OK);
        deque_generation = deque.generation;
        heap_generation = heap.generation;
        counted_copy_allowed = false;
        check_equal(deque_push_back(&deque, &source), STL_OUT_OF_MEMORY);
        check_equal(heap_push(&heap, &source), STL_OUT_OF_MEMORY);
        check_equal(deque_size(&deque), (size_t)0u);
        check_equal(heap_size(&heap), (size_t)0u);
        check_equal(deque.generation, deque_generation);
        check_equal(heap.generation, heap_generation);
        deque_raw_destroy_storage(&deque);
        heap_raw_destroy_storage(&heap);
        counted_destroy(&source);
    }

    it("rejects typed vector growth because no default constructor exists") {
        vec_t vec = {0};
        counted_value source;
        uint64_t generation;

        counted_reset();
        source = counted_source(5);
        check_equal(vec_raw_init(&vec, &counted_type, 2u), STL_OK);
        check_equal(vec_push(&vec, &source), STL_OK);
        generation = vec.generation;
        check_equal(vec_resize(&vec, 2u), STL_TRAIT_MISSING);
        check_equal(vec_size(&vec), (size_t)1u);
        check_equal(vec.generation, generation);
        vec_raw_destroy_storage(&vec);
        counted_destroy(&source);
    }

    it("leaves destroyed vector handle unchanged when typed from-array copy fails") {
        vec_t vec = {0};
        vec_t before;
        counted_value values[3];

        counted_reset();
        values[0] = counted_source(1);
        values[1] = counted_source(2);
        values[2] = counted_source(3);
        check_equal(vec_raw_init(&vec, &counted_type, 3u), STL_OK);
        vec_raw_destroy_storage(&vec);
        before = vec;
        counted_copy_fail_on = 2u;
        check_equal(vec_raw_from_array(&vec, values, 3u, &counted_type, 3u),
                    STL_OUT_OF_MEMORY);
        check_equal(memcmp(&vec, &before, sizeof(vec)), 0);
        counted_destroy(&values[0]);
        counted_destroy(&values[1]);
        counted_destroy(&values[2]);
        check_equal(counted_copies, (size_t)1u);
        check_equal(counted_destroys, (size_t)5u);
    }

    it("leaves destroyed deque handle unchanged when typed from-array copy fails") {
        deque_t deque = {0};
        deque_t before;
        counted_value values[3];

        counted_reset();
        values[0] = counted_source(1);
        values[1] = counted_source(2);
        values[2] = counted_source(3);
        check_equal(deque_raw_init(&deque, &counted_type, 3u), STL_OK);
        deque_raw_destroy_storage(&deque);
        before = deque;
        counted_copy_fail_on = 2u;
        check_equal(deque_raw_from_array(&deque, values, 3u, &counted_type, 3u),
                    STL_OUT_OF_MEMORY);
        check_equal(memcmp(&deque, &before, sizeof(deque)), 0);
        counted_destroy(&values[0]);
        counted_destroy(&values[1]);
        counted_destroy(&values[2]);
        check_equal(counted_copies, (size_t)1u);
        check_equal(counted_destroys, (size_t)5u);
    }

    it("leaves destroyed heap handle unchanged when typed from-array copy fails") {
        heap_t heap = {0};
        heap_t before;
        counted_value values[3];

        counted_reset();
        values[0] = counted_source(1);
        values[1] = counted_source(2);
        values[2] = counted_source(3);
        check_equal(heap_raw_init(&heap, &counted_type, 3u), STL_OK);
        heap_raw_destroy_storage(&heap);
        before = heap;
        counted_copy_fail_on = 2u;
        check_equal(heap_raw_from_array(&heap, values, 3u, &counted_type, 3u),
                    STL_OUT_OF_MEMORY);
        check_equal(memcmp(&heap, &before, sizeof(heap)), 0);
        counted_destroy(&values[0]);
        counted_destroy(&values[1]);
        counted_destroy(&values[2]);
        check_equal(counted_copies, (size_t)1u);
        check_equal(counted_destroys, (size_t)5u);
    }

    it("rejects live typed initialization before validating traits") {
        vec_t vec = {0};
        deque_t deque = {0};
        heap_t heap = {0};
        vec_t vec_before;
        deque_t deque_before;
        heap_t heap_before;

        check_equal(vec_raw_init(&vec, &counted_type, 1u), STL_OK);
        vec_before = vec;
        check_equal(vec_raw_init(&vec, &missing_traits_type, 1u), STL_INVALID_ARGUMENT);
        check_equal(memcmp(&vec, &vec_before, sizeof(vec)), 0);
        vec_raw_destroy_storage(&vec);

        check_equal(deque_raw_init(&deque, &counted_type, 1u), STL_OK);
        deque_before = deque;
        check_equal(deque_raw_init(&deque, &missing_traits_type, 1u), STL_INVALID_ARGUMENT);
        check_equal(memcmp(&deque, &deque_before, sizeof(deque)), 0);
        deque_raw_destroy_storage(&deque);

        check_equal(heap_raw_init(&heap, &counted_type, 1u), STL_OK);
        heap_before = heap;
        check_equal(heap_raw_init(&heap, &missing_traits_type, 1u), STL_INVALID_ARGUMENT);
        check_equal(memcmp(&heap, &heap_before, sizeof(heap)), 0);
        heap_raw_destroy_storage(&heap);
    }

    it("transfers vector erase and swap-remove ownership exactly once") {
        vec_t vec = {0};
        counted_value values[3];
        counted_value erased = {0};
        counted_value swapped = {0};

        counted_reset();
        values[0] = counted_source(1);
        values[1] = counted_source(2);
        values[2] = counted_source(3);
        check_equal(vec_raw_init(&vec, &counted_type, 3u), STL_OK);
        check_equal(vec_push(&vec, &values[0]), STL_OK);
        check_equal(vec_push(&vec, &values[1]), STL_OK);
        check_equal(vec_push(&vec, &values[2]), STL_OK);
        check_equal(vec_erase(&vec, 1u, &erased), STL_OK);
        check_equal(*erased.value, 2);
        check_equal(vec_swap_remove(&vec, 0u, &swapped), STL_OK);
        check_equal(*swapped.value, 1);
        check_equal(vec_size(&vec), (size_t)1u);
        counted_destroy(&erased);
        counted_destroy(&swapped);
        vec_raw_destroy_storage(&vec);
        counted_destroy(&values[0]);
        counted_destroy(&values[1]);
        counted_destroy(&values[2]);
        check_equal(counted_copies, (size_t)3u);
        check_equal(counted_destroys, (size_t)13u);
    }

    it("transfers deque front and back ownership exactly once") {
        deque_t deque = {0};
        counted_value values[3];
        counted_value front = {0};
        counted_value back = {0};

        counted_reset();
        values[0] = counted_source(1);
        values[1] = counted_source(2);
        values[2] = counted_source(3);
        check_equal(deque_raw_init(&deque, &counted_type, 3u), STL_OK);
        check_equal(deque_push_back(&deque, &values[0]), STL_OK);
        check_equal(deque_push_back(&deque, &values[1]), STL_OK);
        check_equal(deque_push_back(&deque, &values[2]), STL_OK);
        check_equal(deque_pop_front(&deque, &front), STL_OK);
        check_equal(*front.value, 1);
        check_equal(deque_pop_back(&deque, &back), STL_OK);
        check_equal(*back.value, 3);
        check_equal(deque_size(&deque), (size_t)1u);
        counted_destroy(&front);
        counted_destroy(&back);
        deque_raw_destroy_storage(&deque);
        counted_destroy(&values[0]);
        counted_destroy(&values[1]);
        counted_destroy(&values[2]);
        check_equal(counted_copies, (size_t)3u);
        check_equal(counted_destroys, (size_t)11u);
    }
}
