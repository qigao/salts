#include <cmeta/pool.h>
#include <cmeta/local.h>
#include <salts/atomic.h>
#include <salts/rcu.h>
#include <cstl/typed.h>
#include "tinytest.h"
#include <stdlib.h>
#include <string.h>

typedef struct ExecutionValue { cmeta_capture_storage alignment; int *owned; int value; } ExecutionValue;
static atomic_uint destroyed;
static bool fail_init;
static cmeta_pool_state *reentry_pool;
static cmeta_status reentry_status;
static cmeta_status value_init(void *object) {
    ExecutionValue *value = object;
    memset(value, 0, sizeof(*value));
    if (reentry_pool != NULL) reentry_status = cmeta_pool_destroy(reentry_pool);
    if (!fail_init) return CMETA_OK;
    value->owned = malloc(sizeof(*value->owned));
    return value->owned != NULL ? CMETA_CALLBACK_ERROR : CMETA_OUT_OF_MEMORY;
}
static void value_restore(void *object) {
    ExecutionValue *value = object;
    if (value->owned != NULL) { free(value->owned); atomic_fetch_add(&destroyed, 1); }
    memset(value, 0, sizeof(*value));
}
static void value_move(void *destination, void *source) {
    *(ExecutionValue *)destination = *(ExecutionValue *)source;
    memset(source, 0, sizeof(ExecutionValue));
}
static const cmeta_type_desc value_type = {
    .name = "ExecutionValue", .size = sizeof(ExecutionValue),
    .align = _Alignof(ExecutionValue), .kind = CMETA_T_OBJECT
};
static const cmeta_data_construct_ops value_ops = {
    .struct_size = sizeof(cmeta_data_construct_ops), .abi_version = CMETA_DATA_CONSTRUCT_OPS_ABI_VERSION,
    .storage_type = &value_type, .init_zero = value_init, .restore_zero = value_restore, .move = value_move
};
static const cmeta_data_desc value_data = {
    .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ExecutionValue", .display_name = "ExecutionValue",
    .kind = CMETA_DATA_CUSTOM, .storage_type = &value_type, .construct_ops = &value_ops
};
static const cmeta_data_desc *ExecutionValue_cmeta_data(void) { return &value_data; }
cmeta_pool_type(ValuePool, ExecutionValue);
cmeta_local_type(ValueLocal, ExecutionValue);
SALTS_ATOMIC_TYPE(IntAtomic, int);
typedef int *IntPointer;
SALTS_ATOMIC_TYPE(PointerAtomic, IntPointer);
SALTS_RCU_TYPE(IntRcu, int);
cmeta_type(Vec, ExecutionVec, int);
cmeta_pool_type(VecPool, ExecutionVec);
static SALTS_THREAD_LOCAL ValueLocal tls_value = {0};

typedef struct local_worker {
    ValueLocal *foreign;
    ValuePool *pool;
    cmeta_status foreign_destroy;
    cmeta_status foreign_pool;
    cmeta_status init, destroy;
    bool foreign_get_null;
    int value;
} local_worker;
static void exercise_local(void *arg) {
    local_worker *ctx = arg;
    ValuePool_lease lease = {0};
    ctx->foreign_get_null = ValueLocal_get(ctx->foreign) == NULL;
    ctx->foreign_destroy = ValueLocal_destroy(ctx->foreign);
    ctx->foreign_pool = ValuePool_acquire(ctx->pool, &lease);
    ctx->init = ValueLocal_init(&tls_value);
    if (ctx->init == CMETA_OK) {
        ExecutionValue *value = ValueLocal_get(&tls_value);
        value->value = 20;
        ctx->value = value->value;
        ctx->destroy = ValueLocal_destroy(&tls_value);
    }
}
typedef struct publication { IntAtomic ready; int payload; int seen; } publication;
static void acquire_payload(void *arg) {
    publication *ctx = arg;
    int ready = 0;
    while (ready == 0) {
        if (IntAtomic_load(&ctx->ready, memory_order_acquire, &ready) != CMETA_OK) return;
        salts_thread_yield();
    }
    ctx->seen = ctx->payload;
}

spec("CMeta execution primitives") {
    it("reuses slots holding a real CSTL owned vector through canonical lifecycle") {
        VecPool pool = {0}; VecPool_lease lease = {0};
        ExecutionVec destination = {0}; ExecutionVec *value;
        const cmeta_data_construct_ops *ops = ExecutionVec_cmeta_data()->construct_ops;
        bool zero = false;
        check_equal(ops->init_zero(&destination), CMETA_OK);
        check_equal(VecPool_init(&pool, 1), CMETA_OK);
        check_equal(VecPool_acquire(&pool, &lease), CMETA_OK);
        value = VecPool_get(&pool, &lease);
        check_equal(ExecutionVec_init(value, 2), STL_OK);
        check_equal(ExecutionVec_push(value, 8), STL_OK);
        check_equal(VecPool_move_out(&pool, &lease, &destination), CMETA_OK);
        check_equal(cmeta_data_value_is_zero(ExecutionVec_cmeta_data(), value, &zero), CMETA_OK);
        check_true(zero);
        check_equal(VecPool_release(&pool, &lease), CMETA_OK);
        check_equal(ExecutionVec_size(&destination), (size_t)1);
        check_equal(*ExecutionVec_at_const(&destination, 0), 8);
        check_equal(VecPool_acquire(&pool, &lease), CMETA_OK);
        value = VecPool_get(&pool, &lease);
        check_equal(ExecutionVec_init(value, 1), STL_OK);
        check_equal(ExecutionVec_push(value, 9), STL_OK);
        check_equal(VecPool_release(&pool, &lease), CMETA_OK);
        check_equal(VecPool_destroy(&pool), CMETA_OK);
        ops->restore_zero(&destination);
    }
    it("bounds aligned pool slots and preserves canonical move and destruction") {
        ValuePool pool = {0}, other = {0}, copied;
        ValuePool_lease first = {0}, second = {0}, extra = {0}, copy;
        ExecutionValue destination = {0};
        ExecutionValue *value;
        atomic_store(&destroyed, 0);
        check_equal(ValuePool_init(&pool, 2), CMETA_OK);
        check_equal(ValuePool_init(&other, 1), CMETA_OK);
        copied = pool;
        check_equal(ValuePool_acquire(&copied, &extra), CMETA_INVALID_ARGUMENT);
        check_equal(ValuePool_acquire(&pool, &first), CMETA_OK);
        check_equal(ValuePool_acquire(&pool, &second), CMETA_OK);
        check_equal(ValuePool_acquire(&pool, &extra), CMETA_CAPACITY_EXCEEDED);
        check_equal(object_pool_capacity(pool.state.owner.storage), (size_t)2);
        value = ValuePool_get(&pool, &first);
        check_not_null(value);
        check_equal((uintptr_t)value % _Alignof(ExecutionValue), (uintptr_t)0);
        value->owned = malloc(sizeof(*value->owned));
        check_not_null(value->owned);
        value->value = 9;
        copy = first;
        check_equal(ValuePool_release(&pool, &copy), CMETA_INVALID_ARGUMENT);
        check_equal(ValuePool_release(&other, &first), CMETA_INVALID_ARGUMENT);
        check_equal(ValuePool_destroy(&pool), CMETA_BUSY);
        check_equal(ValuePool_move_out(&pool, &first, value), CMETA_INVALID_ARGUMENT);
        check_equal(ValuePool_move_out(&pool, &first, &destination), CMETA_OK);
        check_null(value->owned);
        check_equal(destination.value, 9);
        check_equal(ValuePool_release(&pool, &first), CMETA_OK);
        check_equal(ValuePool_release(&pool, &first), CMETA_INVALID_ARGUMENT);
        check_equal(ValuePool_acquire(&pool, &extra), CMETA_OK);
        check_equal(ValuePool_release(&pool, &extra), CMETA_OK);
        check_equal(ValuePool_release(&pool, &second), CMETA_OK);
        check_equal(atomic_load(&destroyed), 0u);
        value_restore(&destination);
        check_equal(atomic_load(&destroyed), 1u);
        check_equal(ValuePool_destroy(&pool), CMETA_OK);
        check_equal(ValuePool_destroy(&other), CMETA_OK);
    }
    it("restores failed construction and rejects lifecycle callback reentry") {
        ValuePool pool = {0}; ValuePool_lease lease = {0};
        ValueLocal local = {0};
        atomic_store(&destroyed, 0);
        check_equal(ValuePool_init(&pool, 0), CMETA_INVALID_ARGUMENT);
        check_equal(ValuePool_init(&pool, SIZE_MAX), CMETA_CAPACITY_EXCEEDED);
        check_equal(ValuePool_init(&pool, 1), CMETA_OK);
        fail_init = true;
        check_equal(ValuePool_acquire(&pool, &lease), CMETA_CALLBACK_ERROR);
        check_equal(atomic_load(&destroyed), 1u);
        check_equal(object_pool_allocated_count(pool.state.owner.storage), (size_t)0);
        check_equal(ValueLocal_init(&local), CMETA_CALLBACK_ERROR);
        check_null(ValueLocal_get(&local));
        check_equal(atomic_load(&destroyed), 2u);
        fail_init = false;
        reentry_pool = &pool.state;
        check_equal(ValuePool_acquire(&pool, &lease), CMETA_OK);
        reentry_pool = NULL;
        check_equal(reentry_status, CMETA_BUSY);
        check_equal(ValuePool_release(&pool, &lease), CMETA_OK);
        check_equal(ValuePool_destroy(&pool), CMETA_OK);
        check_equal(ValueLocal_init(&local), CMETA_OK);
        check_equal(ValueLocal_destroy(&local), CMETA_OK);
    }
    it("rejects missing or mismatched canonical lifecycle without allocating slots") {
        cmeta_pool_state pool = {0}; cmeta_pool_lease lease = {0};
        cmeta_data_desc data = value_data;
        cmeta_data_construct_ops ops = value_ops;
        ExecutionValue destination = {0};
        data.construct_ops = NULL;
        check_equal(cmeta_pool_init(&pool, &data, sizeof(ExecutionValue),
            _Alignof(ExecutionValue), 1), CMETA_TRAIT_MISSING);
        check_null(pool.owner.storage);
        data.construct_ops = &ops;
        check_equal(cmeta_pool_init(&pool, &data, sizeof(ExecutionValue) + 1,
            _Alignof(ExecutionValue), 1), CMETA_TYPE_MISMATCH);
        check_null(pool.owner.storage);
        ops.move = NULL;
        check_equal(cmeta_pool_init(&pool, &data, sizeof(ExecutionValue),
            _Alignof(ExecutionValue), 1), CMETA_OK);
        check_equal(cmeta_pool_acquire(&pool, &lease), CMETA_OK);
        check_equal(cmeta_pool_move_out(&pool, &lease, &destination), CMETA_TRAIT_MISSING);
        check_equal(cmeta_pool_release(&pool, &lease), CMETA_OK);
        check_equal(cmeta_pool_destroy(&pool), CMETA_OK);
        check_equal(cmeta_pool_init(&pool, &data, sizeof(ExecutionValue),
            object_pool_max_alignment() * 2, 1), CMETA_INVALID_ARGUMENT);
    }
    it("keeps TLS isolated and rejects wrong-thread access to locals and pools") {
        ValuePool pool = {0}; ValueLocal copy;
        salts_thread_t thread = NULL;
        local_worker ctx = {0};
        check_equal(ValuePool_init(&pool, 1), CMETA_OK);
        check_equal(ValueLocal_init(&tls_value), CMETA_OK);
        ValueLocal_get(&tls_value)->value = 10;
        copy = tls_value;
        check_null(ValueLocal_get(&copy));
        ctx.foreign = &tls_value; ctx.pool = &pool;
        check_equal(salts_thread_create(&thread, exercise_local, &ctx), 0);
        if (thread != NULL) check_equal(salts_thread_join(&thread), 0);
        check_true(ctx.foreign_get_null);
        check_equal(ctx.foreign_destroy, CMETA_INVALID_ARGUMENT);
        check_equal(ctx.foreign_pool, CMETA_INVALID_ARGUMENT);
        check_equal(ctx.init, CMETA_OK); check_equal(ctx.destroy, CMETA_OK);
        check_equal(ctx.value, 20);
        check_equal(ValueLocal_get(&tls_value)->value, 10);
        check_equal(ValueLocal_destroy(&tls_value), CMETA_OK);
        check_equal(ValuePool_destroy(&pool), CMETA_OK);
    }
    it("checks atomic orders and native compare-exchange expected-value semantics") {
        IntAtomic value; PointerAtomic pointer;
        int out = 77, expected = 3, first = 1, second = 2;
        IntPointer pointer_expected = &first, pointer_out = NULL;
        bool exchanged = false, lock_free;
        check_equal(IntAtomic_init(&value, 4), SALTS_OK);
        check_equal(IntAtomic_load(&value, memory_order_release, &out), SALTS_EINVAL);
        check_equal(out, 77);
        check_equal(IntAtomic_store(&value, 9, memory_order_acquire), SALTS_EINVAL);
        check_equal(IntAtomic_exchange(&value, 9, (memory_order)99, &out), SALTS_EINVAL);
        check_equal(IntAtomic_compare_exchange(&value, &expected, 5,
            memory_order_release, memory_order_acquire, &exchanged), SALTS_EINVAL);
        check_equal(expected, 3);
        check_equal(IntAtomic_compare_exchange(&value, &expected, 5,
            memory_order_acq_rel, memory_order_acquire, &exchanged), SALTS_OK);
        check_false(exchanged); check_equal(expected, 4);
        check_equal(IntAtomic_compare_exchange(&value, &expected, 5,
            memory_order_acq_rel, memory_order_acquire, &exchanged), SALTS_OK);
        check_true(exchanged);
        check_equal(IntAtomic_exchange(&value, 6, memory_order_relaxed, &out), SALTS_OK);
        check_equal(out, 5);
        check_equal(IntAtomic_is_lock_free(&value, &lock_free), SALTS_OK);
        check_equal(PointerAtomic_init(&pointer, &first), SALTS_OK);
        check_equal(PointerAtomic_compare_exchange(&pointer, &pointer_expected, &second,
            memory_order_release, memory_order_relaxed, &exchanged), SALTS_OK);
        check_true(exchanged);
        check_equal(PointerAtomic_load(&pointer, memory_order_acquire, &pointer_out), SALTS_OK);
        check_true(pointer_out == &second);
    }
    it("publishes payload through explicit release/acquire and exposes typed RCU ownership") {
        publication ctx = {0}; salts_thread_t thread = NULL;
        IntRcu domain = {0}; IntRcu_guard guard = {0};
        int first = 1, second = 2; int *out = NULL;
        check_equal(IntAtomic_init(&ctx.ready, 0), SALTS_OK);
        check_equal(salts_thread_create(&thread, acquire_payload, &ctx), 0);
        ctx.payload = 42;
        check_equal(IntAtomic_store(&ctx.ready, 1, memory_order_release), SALTS_OK);
        if (thread != NULL) check_equal(salts_thread_join(&thread), 0);
        check_equal(ctx.seen, 42);
        check_equal(IntRcu_init(&domain, &first, 1), SALTS_OK);
        check_equal(IntRcu_read_lock(&domain, &guard), SALTS_OK);
        check_equal(IntRcu_replace(&domain, &second), SALTS_OK);
        check_equal(*IntRcu_load(&guard), 1);
        check_equal(IntRcu_try_reclaim(&domain, &out), SALTS_EBUSY);
        check_null(out);
        check_equal(IntRcu_read_unlock(&guard), SALTS_OK);
        check_equal(IntRcu_try_reclaim(&domain, &out), SALTS_OK);
        check_true(out == &first);
        check_equal(IntRcu_close(&domain), SALTS_OK);
        check_equal(IntRcu_destroy(&domain, &out), SALTS_OK);
        check_true(out == &second);
    }
    it("admits exactly the C11 compare-exchange success/failure order pairs") {
        enum { ORDER_COUNT = 6 };
        const memory_order orders[ORDER_COUNT] = {
            memory_order_relaxed, memory_order_consume, memory_order_acquire,
            memory_order_release, memory_order_acq_rel, memory_order_seq_cst
        };
        const bool admitted[ORDER_COUNT][ORDER_COUNT] = {
            {true, false, false, false, false, false},
            {true, true, false, false, false, false},
            {true, true, true, false, false, false},
            {true, false, false, false, false, false},
            {true, true, true, false, false, false},
            {true, true, true, false, false, true}
        };
        for (size_t success = 0; success < ORDER_COUNT; ++success) {
            for (size_t failure = 0; failure < ORDER_COUNT; ++failure) {
                IntAtomic value;
                int expected = 1;
                bool exchanged = false;
                check_equal(IntAtomic_init(&value, 1), SALTS_OK);
                check_equal(IntAtomic_compare_exchange(&value, &expected, 2,
                    orders[success], orders[failure], &exchanged),
                    admitted[success][failure] ? SALTS_OK : SALTS_EINVAL);
                check_equal(exchanged, admitted[success][failure]);
            }
        }
    }
}
