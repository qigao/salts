#include <object_pool_managed.h>
#include "tinytest.hpp"
#include <new>
#include <type_traits>

struct CppManagedValue { double value; };
static_assert(std::is_same_v<decltype(object_pool_managed_get(nullptr, nullptr)), void *>);
static object_pool_managed pool{};
static object_pool_managed_lease lease{};
suite("Core managed object pool C++ consumers") {
    before_each() { pool = {}; lease = {}; }
    after_each() {
        if (lease.self == &lease) {
            if (object_pool_managed_check(&pool) == SALTS_OK)
                check_equal(object_pool_managed_enter(&pool, &lease), SALTS_OK);
            check_equal(object_pool_managed_discard(&pool, &lease), SALTS_OK);
        }
        if (pool.storage != nullptr) check_equal(object_pool_managed_destroy(&pool), SALTS_OK);
    }
    it("constructs a value inside a native transaction and rejects a copied lease") {
        check_equal(object_pool_managed_init(&pool, sizeof(CppManagedValue), alignof(CppManagedValue), size_t{1}), SALTS_OK);
        check_equal(object_pool_managed_claim(&pool, &lease), SALTS_OK);
        auto *value = static_cast<CppManagedValue *>(lease.value);
        value = new (lease.value) CppManagedValue{2.0};
        check_equal(object_pool_managed_publish(&pool, &lease), SALTS_OK);
        check_true(object_pool_managed_get(&pool, &lease) == value);
        auto copy = lease;
        check_true(object_pool_managed_get(&pool, &copy) == nullptr);
        check_equal(object_pool_managed_enter(&pool, &copy), SALTS_EINVAL);
        CppManagedValue destination{};
        check_equal(object_pool_managed_move_begin(&pool, &lease, &destination), SALTS_OK);
        destination = *value; *value = {};
        check_equal(object_pool_managed_publish(&pool, &lease), SALTS_OK);
        check_equal(destination.value, 2.0);
        check_equal(object_pool_managed_enter(&pool, &lease), SALTS_OK);
        value->~CppManagedValue();
        check_equal(object_pool_managed_discard(&pool, &lease), SALTS_OK);
        check_equal(object_pool_managed_destroy(&pool), SALTS_OK);
    }
}
