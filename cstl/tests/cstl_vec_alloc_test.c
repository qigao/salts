#include <cstl/vec_alloc.h>
#include "tinytest.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* A bounded test allocator verifies the exact requested size of every release. */
enum { RECEIPTS = 64, OVERALIGN = 64 };
typedef struct Receipt { void *data; size_t bytes; } Receipt;
typedef struct Ledger {
    Receipt receipts[RECEIPTS];
    size_t used, peak, live, calls, fail_on, limit;
    stl_status failure;
} Ledger;

static stl_status charge(void *context, size_t bytes, void **out) {
    Ledger *ledger = (Ledger *)context;
    size_t i;
    *out = NULL;
    ++ledger->calls;
    if (ledger->calls == ledger->fail_on) return ledger->failure;
    if (bytes > ledger->limit - ledger->used) return STL_CAPACITY_EXCEEDED;
    for (i = 0; i < RECEIPTS && ledger->receipts[i].data != NULL; ++i) {}
    if (i == RECEIPTS) abort();
    *out = malloc(bytes);
    if (*out == NULL) return STL_OUT_OF_MEMORY;
    ledger->receipts[i] = (Receipt){*out, bytes};
    ledger->used += bytes;
    ++ledger->live;
    if (ledger->used > ledger->peak) ledger->peak = ledger->used;
    return STL_OK;
}

static void release(void *context, void *data, size_t bytes) {
    Ledger *ledger = (Ledger *)context;
    size_t i;
    for (i = 0; i < RECEIPTS && ledger->receipts[i].data != data; ++i) {}
    if (i == RECEIPTS || data == NULL || ledger->receipts[i].bytes != bytes) abort();
    ledger->used -= bytes;
    --ledger->live;
    ledger->receipts[i] = (Receipt){0};
    free(data);
}

static stl_allocator allocator_for(Ledger *ledger) {
    return (stl_allocator){ledger, charge, release};
}

static size_t managed_live;
static bool copy_fails;
typedef struct Managed { int *value; } Managed;
static bool copy_value(void *dst, const void *src) {
    Managed *out = (Managed *)dst;
    if (copy_fails) return false;
    out->value = (int *)malloc(sizeof(int));
    if (out->value == NULL) return false;
    *out->value = *((const Managed *)src)->value;
    ++managed_live;
    return true;
}
static void move_value(void *dst, void *src) {
    *(Managed *)dst = *(Managed *)src;
    ((Managed *)src)->value = NULL;
}
static void destroy_value(void *item) {
    Managed *value = (Managed *)item;
    if (value->value != NULL) { free(value->value); --managed_live; value->value = NULL; }
}
static const cmeta_type_traits managed_traits = {
    CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    NULL, NULL, NULL, copy_value, move_value, destroy_value
};
/* Like Jinja TemplateNode, explicit native storage need not be registered. */
static const cmeta_type_desc managed_type = {
    "test.Managed", sizeof(Managed), CMETA_ALIGNOF(Managed),
    CMETA_T_OBJECT, NULL, &managed_traits, NULL
};

suite("allocator-bound Vec") {
    it("copies allocator callbacks and returns every requested byte") {
        Ledger ledger = {.limit = SIZE_MAX};
        stl_allocator allocator = allocator_for(&ledger);
        vec_alloc_t *owner = NULL;
        const int input = 7;
        check_equal(vec_alloc_new_bytes(sizeof(int), CMETA_ALIGNOF(int), 3u, &allocator, &owner), STL_OK);
        check_true(ledger.used > 0u);
        allocator = (stl_allocator){0};
        check_equal(vec_alloc_push(owner, &input), STL_OK);
        check_equal(vec_alloc_resize(owner, 3u), STL_OK);
        check_equal(*(const int *)vec_at_const(vec_alloc_view(owner), 0), 7);
        check_equal(*(int *)vec_alloc_at(owner, 1), 0);
        check_equal(*(int *)vec_alloc_at(owner, 2), 0);
        check_equal(vec_alloc_push(owner, &input), STL_CAPACITY_EXCEEDED);
        check_null(vec_alloc_at(owner, 3));
        check_equal(vec_alloc_resize(owner, 1u), STL_OK);
        vec_alloc_destroy(owner);
        check_equal(ledger.used, (size_t)0);
        check_equal(ledger.live, (size_t)0);
    }
    it("handles null views and rejects invalid construction without allocation") {
        Ledger ledger = {.limit = SIZE_MAX};
        stl_allocator allocator = allocator_for(&ledger);
        stl_allocator incomplete = {&ledger, charge, NULL};
        vec_alloc_t *owner = NULL;
        check_null(vec_alloc_view(NULL));
        check_null(vec_alloc_at(NULL, 0));
        check_equal(vec_size(vec_alloc_view(NULL)), (size_t)0);
        vec_alloc_destroy(NULL);
        check_equal(vec_alloc_push(NULL, &ledger), STL_INVALID_ARGUMENT);
        check_equal(vec_alloc_resize(NULL, 0), STL_INVALID_ARGUMENT);
        check_equal(vec_alloc_new_bytes(0, 1, 2, &allocator, &owner), STL_INVALID_ARGUMENT);
        check_equal(vec_alloc_new_bytes(1, 3, 2, &allocator, &owner), STL_INVALID_ARGUMENT);
        check_equal(vec_alloc_new_bytes(1, 1, 2, NULL, &owner), STL_INVALID_ARGUMENT);
        check_equal(vec_alloc_new_bytes(1, 1, 2, &incomplete, &owner), STL_INVALID_ARGUMENT);
        check_equal(vec_alloc_new_bytes(1, 1, 2, &allocator, NULL), STL_INVALID_ARGUMENT);
        check_equal(vec_alloc_new(NULL, 2, &allocator, &owner), STL_INVALID_ARGUMENT);
        check_null(owner);
        check_equal(ledger.calls, (size_t)0);
    }
    it("preserves capacity and out-of-memory statuses at owner admission") {
        const stl_status failures[] = {STL_CAPACITY_EXCEEDED, STL_OUT_OF_MEMORY};
        size_t i;
        for (i = 0; i < 2; ++i) {
            Ledger ledger = {.limit = SIZE_MAX, .fail_on = 1, .failure = failures[i]};
            const stl_allocator allocator = allocator_for(&ledger);
            vec_alloc_t *owner = NULL;
            check_equal(vec_alloc_new_bytes(1, 1, 2, &allocator, &owner), failures[i]);
            check_null(owner);
            check_equal(ledger.live, (size_t)0);
        }
    }
    it("permits zero capacity without allocating element storage") {
        Ledger ledger = {.limit = SIZE_MAX};
        const stl_allocator allocator = allocator_for(&ledger);
        vec_alloc_t *owner = NULL;
        const int item = 9;
        check_equal(vec_alloc_new_bytes(sizeof(int), CMETA_ALIGNOF(int), 0, &allocator, &owner), STL_OK);
        check_equal(ledger.calls, (size_t)1);
        check_equal(vec_alloc_resize(owner, 0), STL_OK);
        check_equal(vec_alloc_push(owner, &item), STL_CAPACITY_EXCEEDED);
        check_equal(ledger.calls, (size_t)1);
        vec_alloc_destroy(owner);
        check_equal(ledger.used, (size_t)0);
    }
    it("aligns backing storage and element snapshots without losing byte receipts") {
        Ledger ledger = {.limit = SIZE_MAX};
        const stl_allocator allocator = allocator_for(&ledger);
        vec_alloc_t *owner = NULL;
        unsigned char value[3] = {7, 8, 9};
        size_t i;
        check_equal(vec_alloc_new_bytes(sizeof(value), OVERALIGN, 17, &allocator, &owner), STL_OK);
        for (i = 0; i < 17; ++i) {
            check_equal(vec_alloc_push(owner, value), STL_OK);
            check_equal((uintptr_t)vec_alloc_at(owner, i) % OVERALIGN, (uintptr_t)0);
            check_equal(memcmp(vec_alloc_at(owner, i), value, sizeof(value)), 0);
        }
        check_true(ledger.peak > ledger.used);
        vec_alloc_destroy(owner);
        check_equal(ledger.used, (size_t)0);
        check_equal(ledger.live, (size_t)0);
    }
    it("rejects stride and backing byte overflow before invoking the allocator") {
        Ledger ledger = {.limit = SIZE_MAX};
        const stl_allocator allocator = allocator_for(&ledger);
        vec_alloc_t *owner = NULL;
        size_t calls;
        check_equal(vec_alloc_new_bytes(SIZE_MAX, 8, 1, &allocator, &owner), STL_CAPACITY_EXCEEDED);
        check_null(owner);
        check_equal(ledger.calls, (size_t)0);
        check_equal(vec_alloc_new_bytes(8, 8, SIZE_MAX, &allocator, &owner), STL_OK);
        calls = ledger.calls;
        check_equal(vec_alloc_resize(owner, SIZE_MAX), STL_CAPACITY_EXCEEDED);
        check_equal(ledger.calls, calls);
        check_equal(vec_size(vec_alloc_view(owner)), (size_t)0);
        vec_alloc_destroy(owner);
        check_equal(ledger.used, (size_t)0);
    }
    it("preserves aliased input and all vector state on each allocation failure") {
        const stl_status failures[] = {STL_CAPACITY_EXCEEDED, STL_OUT_OF_MEMORY};
        size_t failure, step;
        for (failure = 0; failure < 2; ++failure) for (step = 1; step <= 2; ++step) {
            Ledger ledger = {.limit = SIZE_MAX};
            const stl_allocator allocator = allocator_for(&ledger);
            vec_alloc_t *owner = NULL;
            const vec_t *view;
            size_t capacity, before;
            uint64_t generation;
            void *data;
            check_equal(vec_alloc_new_bytes(sizeof(int), CMETA_ALIGNOF(int), 32, &allocator, &owner), STL_OK);
            check_equal(vec_alloc_resize(owner, 1), STL_OK);
            view = vec_alloc_view(owner);
            capacity = vec_capacity(view);
            check_equal(vec_alloc_resize(owner, capacity), STL_OK);
            *(int *)vec_alloc_at(owner, 0) = 42;
            before = ledger.used; generation = view->generation; data = view->data;
            ledger.fail_on = ledger.calls + step; ledger.failure = failures[failure];
            check_equal(vec_alloc_push(owner, vec_alloc_at(owner, 0)), failures[failure]);
            check_equal(view->size, capacity);
            check_equal(view->capacity, capacity);
            check_equal(view->generation, generation);
            check_true(view->data == data);
            check_equal(*(const int *)view->data, 42);
            check_equal(ledger.used, before);
            ledger.fail_on = 0;
            check_equal(vec_alloc_push(owner, vec_alloc_at(owner, 0)), STL_OK);
            check_equal(*(int *)vec_alloc_at(owner, capacity), 42);
            vec_alloc_destroy(owner);
            check_equal(ledger.used, (size_t)0);
        }
    }
    it("accounts for old and new storage simultaneously and admits the exact peak") {
        Ledger ledger = {.limit = SIZE_MAX};
        const stl_allocator allocator = allocator_for(&ledger);
        vec_alloc_t *owner = NULL;
        size_t required;
        check_equal(vec_alloc_new_bytes(1, 1, 64, &allocator, &owner), STL_OK);
        check_equal(vec_alloc_resize(owner, 1), STL_OK);
        check_equal(vec_alloc_resize(owner, 64), STL_OK);
        required = ledger.peak;
        check_true(required > ledger.used);
        vec_alloc_destroy(owner);
        ledger.limit = required - 1;
        check_equal(vec_alloc_new_bytes(1, 1, 64, &allocator, &owner), STL_OK);
        check_equal(vec_alloc_resize(owner, 1), STL_OK);
        check_equal(vec_alloc_resize(owner, 64), STL_CAPACITY_EXCEEDED);
        check_equal(vec_size(vec_alloc_view(owner)), (size_t)1);
        ledger.limit = required;
        check_equal(vec_alloc_resize(owner, 64), STL_OK);
        vec_alloc_destroy(owner);
        check_equal(ledger.used, (size_t)0);
    }
    it("rolls back typed copies before failed growth and destroys managed values once") {
        Ledger ledger = {.limit = SIZE_MAX};
        const stl_allocator allocator = allocator_for(&ledger);
        vec_alloc_t *owner = NULL;
        Managed source;
        int number = 17;
        size_t before, capacity, i;
        managed_live = 0; copy_fails = false; source.value = &number;
        check_equal(vec_alloc_new(&managed_type, 32, &allocator, &owner), STL_OK);
        check_equal(vec_alloc_push(owner, &source), STL_OK);
        capacity = vec_capacity(vec_alloc_view(owner));
        for (i = 1; i < capacity; ++i) check_equal(vec_alloc_push(owner, &source), STL_OK);
        before = ledger.used;
        ledger.fail_on = ledger.calls + 2; ledger.failure = STL_CAPACITY_EXCEEDED;
        check_equal(vec_alloc_push(owner, vec_alloc_at(owner, 0)), STL_CAPACITY_EXCEEDED);
        check_equal(managed_live, capacity);
        check_equal(ledger.used, before);
        copy_fails = true;
        check_equal(vec_alloc_push(owner, &source), STL_OUT_OF_MEMORY);
        check_equal(managed_live, capacity);
        check_equal(ledger.used, before);
        copy_fails = false; ledger.fail_on = 0;
        check_equal(vec_alloc_push(owner, vec_alloc_at(owner, 0)), STL_OK);
        check_equal(managed_live, capacity + 1);
        check_equal(vec_alloc_resize(owner, capacity + 2), STL_TRAIT_MISSING);
        check_equal(vec_alloc_resize(owner, 1), STL_OK);
        check_equal(managed_live, (size_t)1);
        check_equal(*((Managed *)vec_alloc_at(owner, 0))->value, 17);
        vec_alloc_destroy(owner);
        check_equal(managed_live, (size_t)0);
        check_equal(ledger.used, (size_t)0);
    }
    it("rejects missing typed lifecycle operations before charging an owner") {
        Ledger ledger = {.limit = SIZE_MAX};
        const stl_allocator allocator = allocator_for(&ledger);
        vec_alloc_t *owner = NULL;
        cmeta_type_desc type = managed_type;
        cmeta_type_traits traits = managed_traits;
        traits.move_construct = NULL; type.traits = &traits;
        check_equal(vec_alloc_new(&type, 8, &allocator, &owner), STL_TRAIT_MISSING);
        check_null(owner);
        check_equal(ledger.calls, (size_t)0);
    }
}
