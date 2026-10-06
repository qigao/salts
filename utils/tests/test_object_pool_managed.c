#include <object_pool_managed.h>
#include "tinytest.h"
#include <string.h>

typedef struct ManagedValue { long double alignment; int value; } ManagedValue;
enum { TEST_CAPACITY = 2, INVALID_ALIGNMENT = 3, STRESS_CYCLES = 64 };
static object_pool_managed pool;
static object_pool_managed_lease first, second, extra;

static void clean_lease(object_pool_managed_lease *lease) {
    if (lease->self != lease) return;
    if (object_pool_managed_check(&pool) == SALTS_OK)
        check_equal(object_pool_managed_enter(&pool, lease), SALTS_OK);
    memset(lease->value, 0, sizeof(ManagedValue));
    check_equal(object_pool_managed_discard(&pool, lease), SALTS_OK);
}

typedef struct ForeignPool {
    object_pool_managed *pool;
    object_pool_managed_lease *lease;
    int check, claim, enter, publish, discard, move, destroy, lease_check;
    bool get_null;
} ForeignPool;
static void foreign_calls(void *argument) {
    ForeignPool *ctx = argument;
    object_pool_managed_lease unused = {0};
    ManagedValue destination = {0};
    ctx->check = object_pool_managed_check(ctx->pool);
    ctx->claim = object_pool_managed_claim(ctx->pool, &unused);
    ctx->enter = object_pool_managed_enter(ctx->pool, ctx->lease);
    ctx->publish = object_pool_managed_publish(ctx->pool, ctx->lease);
    ctx->discard = object_pool_managed_discard(ctx->pool, ctx->lease);
    ctx->move = object_pool_managed_move_begin(ctx->pool, ctx->lease, &destination);
    ctx->destroy = object_pool_managed_destroy(ctx->pool);
    ctx->lease_check = object_pool_managed_lease_check(ctx->pool, ctx->lease);
    ctx->get_null = object_pool_managed_get(ctx->pool, ctx->lease) == NULL;
}

spec("Core managed object pool ownership") {
    before_each() {
        pool = (object_pool_managed){0};
        first = (object_pool_managed_lease){0};
        second = (object_pool_managed_lease){0};
        extra = (object_pool_managed_lease){0};
    }
    after_each() {
        if (pool.active != NULL) clean_lease(pool.active);
        clean_lease(&first); clean_lease(&second); clean_lease(&extra);
        if (pool.storage != NULL) check_equal(object_pool_managed_destroy(&pool), SALTS_OK);
    }
    it("rejects invalid configuration and checked arithmetic without binding") {
        check_equal(object_pool_managed_init(NULL, 1, 1, 1), SALTS_EINVAL);
        check_equal(object_pool_managed_init(&pool, 0, 1, 1), SALTS_EINVAL);
        check_equal(object_pool_managed_init(&pool, 1, 0, 1), SALTS_EINVAL);
        check_equal(object_pool_managed_init(&pool, 1, INVALID_ALIGNMENT, 1), SALTS_EINVAL);
        check_equal(object_pool_managed_init(&pool, 1, object_pool_max_alignment() * 2, 1), SALTS_EINVAL);
        check_equal(object_pool_managed_init(&pool, 1, 1, 0), SALTS_EINVAL);
        check_equal(object_pool_managed_init(&pool, SIZE_MAX, 1, 1), SALTS_ENOSPC);
        check_equal(object_pool_managed_init(&pool, sizeof(ManagedValue), _Alignof(ManagedValue), SIZE_MAX), SALTS_ENOSPC);
        check_null(pool.storage); check_null(pool.binding.self);
        check_equal(object_pool_managed_check(&pool), SALTS_EINVAL);
        check_equal(object_pool_managed_destroy(&pool), SALTS_EINVAL);
        check_equal(object_pool_managed_check(NULL), SALTS_EINVAL);
        check_equal(object_pool_managed_enter(NULL, NULL), SALTS_EINVAL);
        check_equal(object_pool_managed_publish(NULL, NULL), SALTS_EINVAL);
        check_equal(object_pool_managed_discard(NULL, NULL), SALTS_EINVAL);
        check_equal(object_pool_managed_lease_check(NULL, NULL), SALTS_EINVAL);
        check_equal(object_pool_managed_move_begin(NULL, NULL, NULL), SALTS_EINVAL);
        check_equal(object_pool_managed_destroy(NULL), SALTS_EINVAL);
        check_null(object_pool_managed_get(NULL, NULL));
    }
    it("preallocates a hard bound and leaves full or invalid admission unchanged") {
        ManagedValue *value;
        check_equal(object_pool_managed_init(&pool, sizeof(ManagedValue), _Alignof(ManagedValue), TEST_CAPACITY), SALTS_OK);
        check_equal(object_pool_managed_init(&pool, sizeof(ManagedValue), _Alignof(ManagedValue), 1), SALTS_EINVAL);
        check_equal(object_pool_managed_claim(&pool, &first), SALTS_OK);
        value = first.value;
        check_equal((uintptr_t)value % _Alignof(ManagedValue), (uintptr_t)0);
        memset(value, 0, sizeof(*value)); value->value = 7;
        check_equal(object_pool_managed_check(&pool), SALTS_EBUSY);
        check_null(object_pool_managed_get(&pool, &first));
        check_equal(object_pool_managed_claim(&pool, &extra), SALTS_EBUSY);
        check_equal(object_pool_managed_destroy(&pool), SALTS_EBUSY);
        check_equal(object_pool_managed_publish(&pool, &first), SALTS_OK);
        check_true(object_pool_managed_get(&pool, &first) == value);
        check_equal(value->value, 7);
        check_equal(object_pool_managed_claim(&pool, &second), SALTS_OK);
        memset(second.value, 0, sizeof(ManagedValue));
        check_equal(object_pool_managed_publish(&pool, &second), SALTS_OK);
        check_equal(object_pool_managed_claim(&pool, &extra), SALTS_ENOSPC);
        check_null(extra.self); check_null(extra.value);
        check_equal(object_pool_capacity(pool.storage), (size_t)TEST_CAPACITY);
        check_equal(object_pool_allocated_count(pool.storage), (size_t)TEST_CAPACITY);
        check_equal(object_pool_managed_claim(&pool, &first), SALTS_EINVAL);
        check_equal(object_pool_managed_destroy(&pool), SALTS_EBUSY);
        clean_lease(&first); clean_lease(&second);
        check_equal(object_pool_managed_destroy(&pool), SALTS_OK);
        check_equal(object_pool_managed_destroy(&pool), SALTS_EINVAL);
        check_equal(pool.binding.phase, SALTS_LOCAL_ZERO);
    }
    it("only completes the active transaction and rejects copied or foreign leases") {
        object_pool_managed copied, other = {0};
        object_pool_managed_lease copy;
        check_equal(object_pool_managed_init(&pool, sizeof(ManagedValue), _Alignof(ManagedValue), TEST_CAPACITY), SALTS_OK);
        check_equal(object_pool_managed_claim(&pool, &first), SALTS_OK);
        check_equal(object_pool_managed_publish(&pool, &first), SALTS_OK);
        copied = pool; copy = first;
        check_equal(object_pool_managed_claim(&copied, &extra), SALTS_EINVAL);
        check_equal(object_pool_managed_enter(&pool, &copy), SALTS_EINVAL);
        check_equal(object_pool_managed_init(&other, sizeof(ManagedValue), _Alignof(ManagedValue), 1), SALTS_OK);
        check_equal(object_pool_managed_enter(&other, &first), SALTS_EINVAL);
        check_equal(object_pool_managed_destroy(&other), SALTS_OK);
        check_equal(object_pool_managed_publish(&pool, &first), SALTS_EINVAL);
        check_equal(object_pool_managed_discard(&pool, &first), SALTS_EINVAL);
        check_equal(object_pool_managed_claim(&pool, &second), SALTS_OK);
        check_equal(object_pool_managed_publish(&pool, &first), SALTS_EINVAL);
        check_equal(object_pool_managed_discard(&pool, &first), SALTS_EINVAL);
        check_equal(object_pool_managed_publish(&pool, NULL), SALTS_EINVAL);
        check_equal(object_pool_managed_publish(&pool, &copy), SALTS_EINVAL);
        check_true(pool.active == &second);
        check_equal(object_pool_allocated_count(pool.storage), (size_t)TEST_CAPACITY);
        check_equal(object_pool_managed_discard(&pool, &second), SALTS_OK);
        check_equal(object_pool_managed_enter(&pool, &first), SALTS_OK);
        check_equal(object_pool_managed_publish(&pool, &first), SALTS_OK);
    }
    it("returns failed construction slots without growth through repeated reuse") {
        size_t cycle;
        check_equal(object_pool_managed_init(&pool, sizeof(ManagedValue), _Alignof(ManagedValue), 1), SALTS_OK);
        for (cycle = 0; cycle < STRESS_CYCLES; ++cycle) {
            check_equal(object_pool_managed_claim(&pool, &first), SALTS_OK);
            memset(first.value, 0, sizeof(ManagedValue));
            check_equal(object_pool_managed_discard(&pool, &first), SALTS_OK);
            check_equal(object_pool_allocated_count(pool.storage), (size_t)0);
            check_equal(object_pool_capacity(pool.storage), (size_t)1);
            check_null(first.self); check_null(pool.active);
        }
        check_equal(object_pool_managed_claim(NULL, &first), SALTS_EINVAL);
        check_equal(object_pool_managed_claim(&pool, NULL), SALTS_EINVAL);
        check_equal(object_pool_managed_enter(&pool, &first), SALTS_EINVAL);
        check_equal(object_pool_managed_lease_check(&pool, NULL), SALTS_EINVAL);
        check_null(object_pool_managed_get(&pool, &first));
    }
    it("rejects pool storage destinations including freed and interior addresses") {
        void *freed;
        ManagedValue destination = {0};
        check_equal(object_pool_managed_init(&pool, sizeof(ManagedValue), _Alignof(ManagedValue), TEST_CAPACITY), SALTS_OK);
        check_equal(object_pool_managed_claim(&pool, &first), SALTS_OK);
        check_equal(object_pool_managed_publish(&pool, &first), SALTS_OK);
        check_equal(object_pool_managed_claim(&pool, &second), SALTS_OK);
        freed = second.value;
        check_equal(object_pool_managed_discard(&pool, &second), SALTS_OK);
        check_equal(object_pool_managed_move_begin(&pool, &first, NULL), SALTS_EINVAL);
        check_equal(object_pool_managed_move_begin(&pool, &first, first.value), SALTS_EINVAL);
        check_equal(object_pool_managed_move_begin(&pool, &first, (char *)first.value + 1), SALTS_EINVAL);
        check_equal(object_pool_managed_move_begin(&pool, &first, freed), SALTS_EINVAL);
        check_equal(object_pool_managed_move_begin(&pool, &first, &destination), SALTS_OK);
        check_equal(object_pool_managed_publish(&pool, &first), SALTS_OK);
        check_true(object_pool_managed_get(&pool, &first) == first.value);
    }
    it("rejects every foreign-thread command in READY and BUSY before touching storage") {
        ForeignPool ctx = {0};
        salts_thread_t thread = NULL;
        unsigned phase;
        check_equal(object_pool_managed_init(&pool, sizeof(ManagedValue), _Alignof(ManagedValue), 1), SALTS_OK);
        check_equal(object_pool_managed_claim(&pool, &first), SALTS_OK);
        ctx.pool = &pool; ctx.lease = &first;
        for (phase = 0; phase < 2; ++phase) {
            check_equal(salts_thread_create(&thread, foreign_calls, &ctx), SALTS_OK);
            if (thread != NULL) check_equal(salts_thread_join(&thread), SALTS_OK);
            check_equal(ctx.check, SALTS_EINVAL); check_equal(ctx.claim, SALTS_EINVAL);
            check_equal(ctx.enter, SALTS_EINVAL); check_equal(ctx.publish, SALTS_EINVAL);
            check_equal(ctx.discard, SALTS_EINVAL); check_equal(ctx.move, SALTS_EINVAL);
            check_equal(ctx.destroy, SALTS_EINVAL); check_equal(ctx.lease_check, SALTS_EINVAL);
            check_true(ctx.get_null);
            if (phase == 0) check_equal(object_pool_managed_publish(&pool, &first), SALTS_OK);
        }
        check_equal(object_pool_allocated_count(pool.storage), (size_t)1);
    }
}
