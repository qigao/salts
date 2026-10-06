#include <cmeta/pool.h>
#include <cmeta/local.h>
#include <stdatomic.h>
#include <cstl/typed.h>
#include "tinytest.h"
#include <stdlib.h>
#include <string.h>

typedef struct ExecutionValue { cmeta_capture_storage alignment; int *owned; int value; } ExecutionValue;
static atomic_uint destroyed;
static bool fail_init;
static cmeta_pool_state *reentry_pool;
static cmeta_status reentry_status;
static cmeta_local_state *reentry_local;
static void *reentry_local_owner;
static cmeta_status local_init_reentry, local_restore_reentry;
static cmeta_status value_init(void *object) {
    ExecutionValue *value = object;
    memset(value, 0, sizeof(*value));
    if (reentry_pool != NULL) reentry_status = cmeta_pool_destroy(reentry_pool);
    if (reentry_local != NULL)
        local_init_reentry = cmeta_local_destroy(reentry_local, reentry_local_owner, object);
    if (!fail_init) return CMETA_OK;
    value->owned = malloc(sizeof(*value->owned));
    return value->owned != NULL ? CMETA_CALLBACK_ERROR : CMETA_OUT_OF_MEMORY;
}
static void value_restore(void *object) {
    ExecutionValue *value = object;
    if (reentry_local != NULL)
        local_restore_reentry = cmeta_local_destroy(reentry_local, reentry_local_owner, object);
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
cmeta_type(Pool, ValuePool, ExecutionValue);
cmeta_type(Local, ValueLocal, ExecutionValue);
cmeta_type(Vec, ExecutionVec, int);
cmeta_type(Pool, VecPool, ExecutionVec);
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
spec("CMeta execution primitives") {
    after_each() {
        reentry_local = NULL;
        reentry_local_owner = NULL;
        reentry_pool = NULL;
        fail_init = false;
    }
    it("projects canonical Local callbacks through the Platform-owned busy transitions") {
        ValueLocal local = {0};
        reentry_local = &local.state;
        reentry_local_owner = &local;
        atomic_store(&destroyed, 0u);
        fail_init = true;
        check_equal(ValueLocal_init(&local), CMETA_CALLBACK_ERROR);
        check_equal(local_init_reentry, CMETA_BUSY);
        check_equal(local_restore_reentry, CMETA_BUSY);
        check_equal(atomic_load(&destroyed), 1u);
        check_equal(local.state.binding.phase, SALTS_LOCAL_ZERO);
        check_null(local.state.ops);
        check_null(ValueLocal_get(&local));
        fail_init = false;
        check_equal(ValueLocal_init(&local), CMETA_OK);
        check_equal(local_init_reentry, CMETA_BUSY);
        check_equal(salts_local_check(&local.state.binding, &local), SALTS_OK);
        check_true(ValueLocal_get(&local) == &local.value);
        check_equal(ValueLocal_destroy(&local), CMETA_OK);
        check_equal(local_restore_reentry, CMETA_BUSY);
        check_equal(local.state.binding.phase, SALTS_LOCAL_ZERO);
        check_null(local.state.ops);
    }
    it("rejects missing or mismatched Local metadata before starting the owner binding") {
        ValueLocal local = {0};
        cmeta_data_desc data = value_data;
        data.construct_ops = NULL;
        check_equal(cmeta_local_init(&local.state, &local, &local.value, &data,
            sizeof(ExecutionValue), _Alignof(ExecutionValue)), CMETA_TRAIT_MISSING);
        check_equal(local.state.binding.phase, SALTS_LOCAL_ZERO);
        check_equal(cmeta_local_init(&local.state, &local, &local.value, &value_data,
            sizeof(ExecutionValue) + 1u, _Alignof(ExecutionValue)), CMETA_TYPE_MISMATCH);
        check_equal(cmeta_local_init(&local.state, &local, NULL, &value_data,
            sizeof(ExecutionValue), _Alignof(ExecutionValue)), CMETA_INVALID_ARGUMENT);
        check_equal(local.state.binding.phase, SALTS_LOCAL_ZERO);
        check_null(local.state.ops);
    }
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
        check_equal(object_pool_capacity(pool.state.storage), (size_t)2);
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
        check_equal(object_pool_allocated_count(pool.state.storage), (size_t)0);
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
        check_null(pool.storage);
        data.construct_ops = &ops;
        check_equal(cmeta_pool_init(&pool, &data, sizeof(ExecutionValue) + 1,
            _Alignof(ExecutionValue), 1), CMETA_TYPE_MISMATCH);
        check_null(pool.storage);
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
}
