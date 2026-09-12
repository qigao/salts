#include <cstl/vec_alloc.h>
#include "tinytest.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Test allocator is the actual storage provider, not a mocked vector. It checks
 * every pointer/byte receipt and makes each allocation boundary fail on demand. */
enum { LEDGER_SLOTS = 64, TEST_LIMIT = 32, TEST_ALIGNMENT = 64 };
typedef struct allocation { void *pointer; size_t bytes; } allocation;
typedef struct ledger {
    allocation live[LEDGER_SLOTS];
    size_t used, peak, calls, releases, count, limit, fail_at;
    stl_status failure;
} ledger;

static stl_status tracked_allocate(void *context, size_t bytes, void **out) {
    ledger *state = (ledger *)context;
    size_t index;
    *out = NULL;
    ++state->calls;
    if (state->calls == state->fail_at) return state->failure;
    if (bytes == 0u || bytes > state->limit - state->used)
        return STL_CAPACITY_EXCEEDED;
    for (index = 0u; index < LEDGER_SLOTS && state->live[index].pointer; ++index) {}
    if (index == LEDGER_SLOTS) return STL_CAPACITY_EXCEEDED;
    *out = malloc(bytes);
    if (*out == NULL) return STL_OUT_OF_MEMORY;
    state->live[index] = (allocation){*out, bytes};
    state->used += bytes;
    if (state->peak < state->used) state->peak = state->used;
    ++state->count;
    return STL_OK;
}

static void tracked_deallocate(void *context, void *pointer, size_t bytes) {
    ledger *state = (ledger *)context;
    size_t index;
    for (index = 0u; index < LEDGER_SLOTS; ++index) {
        if (state->live[index].pointer != pointer) continue;
        check_not_null(pointer);
        check_equal(state->live[index].bytes, bytes);
        check_true(bytes <= state->used);
        state->used -= bytes;
        --state->count;
        ++state->releases;
        state->live[index] = (allocation){0};
        free(pointer);
        return;
    }
    check_true(0); /* Unknown or repeated release is a test failure. */
}

static stl_allocator allocator_for(ledger *state) {
    return (stl_allocator){state, tracked_allocate, tracked_deallocate};
}

static void check_empty_ledger(const ledger *state) {
    check_equal(state->used, (size_t)0u);
    check_equal(state->count, (size_t)0u);
}

typedef struct managed { int *value; } managed;
static size_t managed_live;
static int reject_copy;
static bool managed_copy(void *to, const void *from) {
    const managed *source = (const managed *)from;
    managed *target = (managed *)to;
    if (reject_copy) return false;
    target->value = (int *)malloc(sizeof(int));
    if (!target->value) return false;
    *target->value = *source->value;
    ++managed_live;
    return true;
}
static void managed_move(void *to, void *from) {
    *(managed *)to = *(managed *)from;
    ((managed *)from)->value = NULL;
}
static void managed_destroy(void *value) {
    managed *item = (managed *)value;
    if (item->value) { free(item->value); item->value = NULL; --managed_live; }
}
static const cmeta_type_traits managed_traits = {
    CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    NULL, NULL, NULL, managed_copy, managed_move, managed_destroy
};
/* Like the current Jinja parser type: lifecycle metadata, no registry identity. */
static const cmeta_type_desc managed_type = {
    "test.Managed", sizeof(managed), CMETA_ALIGNOF(managed),
    CMETA_T_OBJECT, NULL, &managed_traits, NULL
};

suite("CSTL allocator-bound Vec") {
    it("accounts for owner backing and scratch and copies the allocator descriptor") {
        ledger state = {.limit = SIZE_MAX};
        stl_allocator allocator = allocator_for(&state);
        vec_alloc_t *owner = NULL;
        int input = 42;
        check_equal(vec_alloc_new_bytes(sizeof(int), CMETA_ALIGNOF(int), TEST_LIMIT,
                                       &allocator, &owner), STL_OK);
        check_not_null(owner);
        check_equal(state.count, (size_t)1u);
        check_true(state.used > 0u);
        allocator = (stl_allocator){0};
        check_equal(vec_alloc_push(owner, &input), STL_OK);
        check_equal(*(int *)vec_alloc_at(owner, 0u), input);
        check_equal(vec_size(vec_alloc_view(owner)), (size_t)1u);
        check_equal(state.count, (size_t)2u);
        check_true(state.peak > state.used); /* scratch overlaps published storage */
        vec_alloc_destroy(owner);
        check_empty_ledger(&state);
        check_equal(state.calls, state.releases);
    }

    it("returns null on invalid construction without calling the provider") {
        ledger state = {.limit = SIZE_MAX};
        stl_allocator allocator = allocator_for(&state);
        stl_allocator incomplete = allocator;
        vec_alloc_t *owner = NULL;
        check_equal(vec_alloc_new_bytes(0u, 1u, TEST_LIMIT, &allocator, &owner), STL_INVALID_ARGUMENT);
        check_equal(vec_alloc_new_bytes(1u, 3u, TEST_LIMIT, &allocator, &owner), STL_INVALID_ARGUMENT);
        check_equal(vec_alloc_new_bytes(1u, 1u, TEST_LIMIT, NULL, &owner), STL_INVALID_ARGUMENT);
        incomplete.deallocate = NULL;
        check_equal(vec_alloc_new_bytes(1u, 1u, TEST_LIMIT, &incomplete, &owner), STL_INVALID_ARGUMENT);
        incomplete = allocator; incomplete.allocate = NULL;
        check_equal(vec_alloc_new_bytes(1u, 1u, TEST_LIMIT, &incomplete, &owner), STL_INVALID_ARGUMENT);
        check_equal(vec_alloc_new(NULL, TEST_LIMIT, &allocator, &owner), STL_INVALID_ARGUMENT);
        check_equal(vec_alloc_new_bytes(1u, 1u, TEST_LIMIT, &allocator, NULL), STL_INVALID_ARGUMENT);
        check_null(owner);
        check_equal(state.calls, (size_t)0u);
        check_empty_ledger(&state);
    }

    it("propagates owner capacity and out-of-memory without a fallback") {
        const stl_status failures[] = {STL_CAPACITY_EXCEEDED, STL_OUT_OF_MEMORY};
        size_t index;
        for (index = 0u; index < sizeof(failures)/sizeof(failures[0]); ++index) {
            ledger state = {.limit = SIZE_MAX, .fail_at = 1u, .failure = failures[index]};
            const stl_allocator allocator = allocator_for(&state);
            vec_alloc_t *owner = NULL;
            check_equal(vec_alloc_new_bytes(1u, 1u, TEST_LIMIT, &allocator, &owner), failures[index]);
            check_null(owner);
            check_equal(state.calls, (size_t)1u);
            check_empty_ledger(&state);
        }
    }

    it("preserves data address generation and charges on scratch or growth failure") {
        const stl_status failures[] = {STL_CAPACITY_EXCEEDED, STL_OUT_OF_MEMORY};
        size_t failure, point;
        for (failure = 0u; failure < sizeof(failures)/sizeof(failures[0]); ++failure) {
            for (point = 1u; point <= 2u; ++point) {
                ledger state = {.limit = SIZE_MAX};
                const stl_allocator allocator = allocator_for(&state);
                vec_alloc_t *owner = NULL;
                int value = 19;
                size_t bytes, size, capacity, calls, index;
                uint64_t generation;
                const void *data;
                check_equal(vec_alloc_new_bytes(sizeof(int), CMETA_ALIGNOF(int), TEST_LIMIT,
                                               &allocator, &owner), STL_OK);
                check_equal(vec_alloc_push(owner, &value), STL_OK);
                capacity = vec_capacity(vec_alloc_view(owner));
                for (index = 1u; index < capacity; ++index)
                    check_equal(vec_alloc_push(owner, &value), STL_OK);
                bytes = state.used; size = vec_size(vec_alloc_view(owner));
                generation = vec_generation(vec_alloc_view(owner));
                data = vec_data_const(vec_alloc_view(owner));
                calls = state.calls;
                state.fail_at = calls + point; state.failure = failures[failure];
                check_equal(vec_alloc_push(owner, vec_alloc_at(owner, 0u)), failures[failure]);
                check_equal(state.calls, calls + point);
                check_equal(state.used, bytes);
                check_equal(vec_size(vec_alloc_view(owner)), size);
                check_equal(vec_capacity(vec_alloc_view(owner)), capacity);
                check_equal(vec_generation(vec_alloc_view(owner)), generation);
                check_true(vec_data_const(vec_alloc_view(owner)) == data);
                for (index = 0u; index < size; ++index)
                    check_equal(*(int *)vec_alloc_at(owner, index), value);
                state.fail_at = 0u;
                check_equal(vec_alloc_push(owner, vec_alloc_at(owner, 0u)), STL_OK);
                check_equal(*(int *)vec_alloc_at(owner, size), value);
                vec_alloc_destroy(owner);
                check_empty_ledger(&state);
            }
        }
    }

    it("enforces live requested-byte capacity including overlap and allows retry") {
        ledger state = {.limit = SIZE_MAX};
        const stl_allocator allocator = allocator_for(&state);
        vec_alloc_t *owner = NULL;
        size_t bytes, capacity;
        const void *data;
        check_equal(vec_alloc_new_bytes(1u, 1u, TEST_LIMIT, &allocator, &owner), STL_OK);
        check_equal(vec_alloc_resize(owner, 1u), STL_OK);
        capacity = vec_capacity(vec_alloc_view(owner));
        check_equal(vec_alloc_resize(owner, capacity), STL_OK);
        bytes = state.used; data = vec_data_const(vec_alloc_view(owner));
        state.limit = bytes;
        check_equal(vec_alloc_resize(owner, capacity + 1u), STL_CAPACITY_EXCEEDED);
        check_equal(state.used, bytes);
        check_true(vec_data_const(vec_alloc_view(owner)) == data);
        check_equal(vec_size(vec_alloc_view(owner)), capacity);
        state.limit = SIZE_MAX;
        check_equal(vec_alloc_resize(owner, capacity + 1u), STL_OK);
        check_true(state.peak > state.used);
        vec_alloc_destroy(owner);
        check_empty_ledger(&state);
    }

    it("zero fills byte growth and respects empty and exact element limits") {
        ledger state = {.limit = SIZE_MAX};
        const stl_allocator allocator = allocator_for(&state);
        vec_alloc_t *owner = NULL;
        unsigned char value = 7;
        size_t calls;
        check_equal(vec_alloc_new_bytes(1u, 1u, 0u, &allocator, &owner), STL_OK);
        calls = state.calls;
        check_equal(vec_alloc_resize(owner, 0u), STL_OK);
        check_equal(vec_alloc_push(owner, &value), STL_CAPACITY_EXCEEDED);
        check_equal(state.calls, calls);
        vec_alloc_destroy(owner);
        check_equal(vec_alloc_new_bytes(1u, 1u, 3u, &allocator, &owner), STL_OK);
        check_equal(vec_alloc_push(owner, &value), STL_OK);
        check_equal(vec_alloc_resize(owner, 3u), STL_OK);
        check_equal(*(unsigned char *)vec_alloc_at(owner, 0u), value);
        check_equal(*(unsigned char *)vec_alloc_at(owner, 1u), (unsigned char)0);
        check_equal(*(unsigned char *)vec_alloc_at(owner, 2u), (unsigned char)0);
        check_null(vec_alloc_at(owner, 3u));
        check_equal(vec_alloc_resize(owner, 4u), STL_CAPACITY_EXCEEDED);
        check_equal(vec_alloc_resize(owner, 0u), STL_OK);
        check_equal(vec_alloc_resize(owner, 3u), STL_OK);
        check_equal(*(unsigned char *)vec_alloc_at(owner, 0u), (unsigned char)0);
        vec_alloc_destroy(owner);
        check_empty_ledger(&state);
    }

    it("supports over-aligned byte storage and padded stride with exact receipts") {
        ledger state = {.limit = SIZE_MAX};
        const stl_allocator allocator = allocator_for(&state);
        vec_alloc_t *owner = NULL;
        unsigned char input[3] = {1, 0, 2};
        size_t index;
        check_equal(vec_alloc_new_bytes(sizeof(input), TEST_ALIGNMENT, 2u, &allocator, &owner), STL_OK);
        check_equal(vec_alloc_push(owner, input), STL_OK);
        check_equal(vec_alloc_push(owner, vec_alloc_at(owner, 0u)), STL_OK);
        for (index = 0u; index < 2u; ++index) {
            const void *item = vec_alloc_at(owner, index);
            check_equal((uintptr_t)item % TEST_ALIGNMENT, (uintptr_t)0u);
            check_equal(memcmp(item, input, sizeof(input)), 0);
        }
        check_equal(vec_alloc_view(owner)->elem_stride, (size_t)TEST_ALIGNMENT);
        vec_alloc_destroy(owner);
        check_empty_ledger(&state);
    }

    it("checks stride multiplication and alignment overhead before allocating") {
        ledger state = {.limit = SIZE_MAX};
        const stl_allocator allocator = allocator_for(&state);
        vec_alloc_t *owner = NULL;
        size_t calls, bytes;
        check_equal(vec_alloc_new_bytes(SIZE_MAX, 2u, 1u, &allocator, &owner), STL_CAPACITY_EXCEEDED);
        check_equal(state.calls, (size_t)0u);
        check_equal(vec_alloc_new_bytes(2u, 1u, SIZE_MAX, &allocator, &owner), STL_OK);
        calls = state.calls; bytes = state.used;
        check_equal(vec_alloc_resize(owner, SIZE_MAX), STL_CAPACITY_EXCEEDED);
        check_equal(state.calls, calls);
        check_equal(state.used, bytes);
        vec_alloc_destroy(owner);
        check_equal(vec_alloc_new_bytes(SIZE_MAX, 1u, 1u, &allocator, &owner), STL_OK);
        calls = state.calls;
        check_equal(vec_alloc_resize(owner, 1u), STL_CAPACITY_EXCEEDED);
        check_equal(state.calls, calls);
        vec_alloc_destroy(owner);
        check_empty_ledger(&state);
    }

    it("uses canonical managed lifecycle with self-push and failure rollback") {
        ledger state = {.limit = SIZE_MAX};
        const stl_allocator allocator = allocator_for(&state);
        vec_alloc_t *owner = NULL;
        int number = 23;
        managed input = {&number};
        size_t index, capacity, bytes, live;
        managed_live = 0u; reject_copy = 0;
        check_equal(vec_alloc_new(&managed_type, TEST_LIMIT, &allocator, &owner), STL_OK);
        check_equal(vec_alloc_push(owner, &input), STL_OK);
        capacity = vec_capacity(vec_alloc_view(owner));
        for (index = 1u; index < capacity; ++index)
            check_equal(vec_alloc_push(owner, &input), STL_OK);
        check_equal(managed_live, capacity);
        bytes = state.used; live = managed_live;
        state.fail_at = state.calls + 2u; state.failure = STL_CAPACITY_EXCEEDED;
        check_equal(vec_alloc_push(owner, vec_alloc_at(owner, 0u)), STL_CAPACITY_EXCEEDED);
        check_equal(managed_live, live);
        check_equal(state.used, bytes);
        state.fail_at = 0u;
        reject_copy = 1;
        check_equal(vec_alloc_push(owner, &input), STL_OUT_OF_MEMORY);
        check_equal(managed_live, live);
        check_equal(state.used, bytes);
        reject_copy = 0;
        check_equal(vec_alloc_push(owner, vec_alloc_at(owner, 0u)), STL_OK);
        check_equal(managed_live, live + 1u);
        for (index = 0u; index <= capacity; ++index)
            check_equal(*((managed *)vec_alloc_at(owner, index))->value, number);
        check_equal(vec_alloc_resize(owner, capacity + 2u), STL_TRAIT_MISSING);
        check_equal(vec_alloc_resize(owner, 1u), STL_OK);
        check_equal(managed_live, (size_t)1u);
        vec_alloc_destroy(owner);
        check_equal(managed_live, (size_t)0u);
        check_empty_ledger(&state);
    }

    it("keeps independent providers isolated and ordinary Vec operational") {
        ledger first = {.limit = SIZE_MAX}, second = {.limit = SIZE_MAX};
        const stl_allocator a = allocator_for(&first), b = allocator_for(&second);
        vec_alloc_t *left = NULL, *right = NULL;
        vec_t ordinary = {0};
        int value = 5;
        check_equal(vec_init_bytes(&ordinary, sizeof(int), CMETA_ALIGNOF(int), 2u), STL_OK);
        check_equal(vec_alloc_new_bytes(sizeof(int), CMETA_ALIGNOF(int), 2u, &a, &left), STL_OK);
        check_equal(vec_alloc_new_bytes(sizeof(int), CMETA_ALIGNOF(int), 2u, &b, &right), STL_OK);
        check_equal(vec_push(&ordinary, &value), STL_OK);
        check_equal(vec_alloc_push(left, &value), STL_OK);
        check_equal(vec_alloc_push(right, &value), STL_OK);
        vec_raw_destroy_storage(&ordinary);
        vec_alloc_destroy(left);
        check_empty_ledger(&first);
        check_true(second.used > 0u);
        check_equal(*(int *)vec_alloc_at(right, 0u), value);
        vec_alloc_destroy(right);
        check_empty_ledger(&second);
    }

    it("handles null owners only as documented empty views and no-op destroy") {
        int value = 1;
        check_null(vec_alloc_view(NULL));
        check_null(vec_alloc_at(NULL, 0u));
        check_equal(vec_size(vec_alloc_view(NULL)), (size_t)0u);
        check_equal(vec_alloc_push(NULL, &value), STL_INVALID_ARGUMENT);
        check_equal(vec_alloc_resize(NULL, 1u), STL_INVALID_ARGUMENT);
        vec_alloc_destroy(NULL);
    }
}
