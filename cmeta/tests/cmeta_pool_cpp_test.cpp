#include <cmeta/pool.h>
#include "tinytest.hpp"
#include <climits>
#include <new>
#include <type_traits>

using PoolInt = int;
static cmeta_status pool_int_init(void *object) {
    new (object) PoolInt(0);
    return CMETA_OK;
}
static void pool_int_restore(void *object) { *static_cast<PoolInt *>(object) = 0; }
static void pool_int_move(void *destination, void *source) {
    *static_cast<PoolInt *>(destination) = *static_cast<PoolInt *>(source);
    *static_cast<PoolInt *>(source) = 0;
}
static const cmeta_data_construct_ops pool_int_ops = {
    sizeof(cmeta_data_construct_ops), CMETA_DATA_CONSTRUCT_OPS_ABI_VERSION,
    &cmeta_type_int, pool_int_init, pool_int_restore, pool_int_move, 0
};
CMETA_DEFINE_STATIC_LIFECYCLE(PoolInt, pool_int_ops)
static_assert(std::is_same_v<decltype(&PoolInt_cmeta_lifecycle),
    const cmeta_data_construct_ops *(*)(const PoolInt *)>);
static const cmeta_data_integer_shape pool_int_shape = {sizeof(PoolInt) * CHAR_BIT};
static const cmeta_data_desc pool_int_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION, "test.PoolInt", "PoolInt",
    CMETA_DATA_SINT, &cmeta_type_int, &pool_int_shape, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, &pool_int_ops
};
static const cmeta_data_desc *PoolInt_cmeta_data() { return &pool_int_data; }
cmeta_pool_type(CppPool, PoolInt);
static_assert(std::is_same_v<CppPool_value_type, PoolInt>);
static_assert(std::is_same_v<decltype(CppPool_get(nullptr, nullptr)), PoolInt *>);
static CppPool pool{};
static CppPool_lease lease{};
suite("CMeta optional Pool lifecycle in C++") {
    before_each() { pool = {}; lease = {}; }
    after_each() {
        if (lease.state.owner.self == &lease.state.owner) check_equal(CppPool_release(&pool, &lease), CMETA_OK);
        if (pool.state.ops != nullptr) check_equal(CppPool_destroy(&pool), CMETA_OK);
    }
    it("checks canonical lifecycle identity independently of display names in C++") {
        const cmeta_type_identity foreign_identity =
            CMETA_TYPE_ID_ATOM_INIT("test.OtherPoolInt");
        cmeta_type_desc provider = cmeta_type_int;
        cmeta_data_construct_ops ops = pool_int_ops;
        cmeta_data_desc data = pool_int_data;
        const cmeta_data_construct_ops *bound = &pool_int_ops;
        provider.identity = &foreign_identity;
        ops.storage_type = &provider;
        data.construct_ops = &ops;
        check_equal(cmeta_lifecycle_bind(&data, sizeof(PoolInt), alignof(PoolInt),
            &bound), CMETA_TYPE_MISMATCH);
        check_null(bound);
        provider.identity = cmeta_type_int.identity;
        provider.name = "RenamedPoolInt";
        check_equal(cmeta_lifecycle_bind(&data, sizeof(PoolInt), alignof(PoolInt),
            &bound), CMETA_OK);
        check_true(bound == &ops);
        provider.kind = CMETA_T_OBJECT;
        check_equal(cmeta_lifecycle_bind(&data, sizeof(PoolInt), alignof(PoolInt),
            &bound), CMETA_TYPE_MISMATCH);
        check_null(bound);
    }
    it("binds canonical lifecycle while Core owns the native lease") {
        PoolInt destination = 0;
        check_equal(CppPool_init(&pool, size_t{1}), CMETA_OK);
        check_true(pool.state.ops == &pool_int_ops);
        check_true(PoolInt_cmeta_lifecycle(nullptr) == pool.state.ops);
        check_equal(CppPool_acquire(&pool, &lease), CMETA_OK);
        check_true(lease.state.owner.owner == &pool.state.owner);
        auto *value = CppPool_get(&pool, &lease);
        check_true(value != nullptr);
        *value = 9;
        auto copied = lease;
        check_true(CppPool_get(&pool, &copied) == nullptr);
        check_equal(CppPool_release(&pool, &copied), CMETA_INVALID_ARGUMENT);
        check_equal(CppPool_move_out(&pool, &lease, &destination), CMETA_OK);
        check_equal(destination, 9); check_equal(*value, 0);
        check_equal(CppPool_destroy(&pool), CMETA_BUSY);
        check_equal(CppPool_release(&pool, &lease), CMETA_OK);
        check_equal(CppPool_destroy(&pool), CMETA_OK);
        check_true(pool.state.ops == nullptr);
    }
}
