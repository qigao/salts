#include <salts/plugin_scope.h>
#include "tinytest.hpp"
#include <type_traits>

static_assert(!std::is_copy_constructible<salts::plugin_lease_scope>::value,"lease has one owner");
static_assert(!std::is_copy_assignable<salts::plugin_lease_scope>::value,"lease cannot be copied");
static_assert(std::is_nothrow_move_constructible<salts::plugin_lease_scope>::value,"move transfers ownership");
static_assert(!std::is_move_assignable<salts::plugin_lease_scope>::value,"close a live target explicitly");

suite("Plugin C++ lease ownership") {
    static salts_plugin_registry registry;
    static salts_plugin_ref ref;
    before_each() {
        registry = {};
        ref = {};
        const salts_plugin_registry_config config = {1u};
        check_equal(salts_plugin_registry_init(&registry,&config),SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_load(&registry,PLUGIN_SCOPE_PATH,&ref),SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_start(&registry,ref),SALTS_PLUGIN_OK);
    }
    after_each() {
        salts_plugin_lifecycle_info info = {};
        check_equal(salts_plugin_registry_get_lifecycle(&registry,ref,&info),SALTS_PLUGIN_OK);
        check_equal(info.active_leases,(size_t)0u);
        if (info.state == SALTS_PLUGIN_LIFECYCLE_STARTED)
            check_equal(salts_plugin_registry_request_stop(&registry,ref),SALTS_PLUGIN_OK);
        bool quiet = false;
        check_equal(salts_plugin_registry_poll_quiescent(&registry,ref,&quiet),SALTS_PLUGIN_OK);
        check_true(quiet);
        check_equal(salts_plugin_registry_unload(&registry,ref),SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_destroy(&registry),SALTS_PLUGIN_OK);
    }
    it("releases on early return and can reuse an explicitly closed owner") {
        const auto early_return = []() -> salts_plugin_status {
            salts::plugin_lease_scope owner;
            const salts_plugin_status status = owner.acquire(registry,ref);
            if (status != SALTS_PLUGIN_OK) return status;
            return SALTS_PLUGIN_BUSY;
        };
        check_equal(early_return(),SALTS_PLUGIN_BUSY);
        salts_plugin_lifecycle_info info = {};
        check_equal(salts_plugin_registry_get_lifecycle(&registry,ref,&info),SALTS_PLUGIN_OK);
        check_equal(info.active_leases,(size_t)0u);
        salts::plugin_lease_scope owner;
        check_equal(owner.acquire(registry,ref),SALTS_PLUGIN_OK);
        check_equal(owner.close(),SALTS_PLUGIN_OK);
        check_equal(owner.acquire(registry,ref),SALTS_PLUGIN_OK);
    }
    it("moves exactly one lease and keeps unload blocked until scope exit") {
        salts::plugin_lease_scope first;
        check_equal(first.acquire(registry,ref),SALTS_PLUGIN_OK);
        const salts_plugin_manifest *borrow = first.manifest();
        salts::plugin_lease_scope second(std::move(first));
        check_false(static_cast<bool>(first));
        check_null(first.manifest());
        check_equal(second.manifest(),borrow);
        check_equal(second.acquire(registry,ref),SALTS_PLUGIN_INVALID_STATE);
        check_equal(salts_plugin_registry_request_stop(&registry,ref),SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_unload(&registry,ref),SALTS_PLUGIN_BUSY);
        salts::plugin_lease_scope denied;
        check_equal(denied.acquire(registry,ref),SALTS_PLUGIN_INVALID_STATE);
        check_false(static_cast<bool>(denied));
        check_null(denied.manifest());
        salts_plugin_lifecycle_info info = {};
        check_equal(salts_plugin_registry_get_lifecycle(&registry,ref,&info),SALTS_PLUGIN_OK);
        check_equal(info.active_leases,(size_t)1u);
    }
    it("releases on an exception after dependent borrowed values are destroyed") {
        struct dependent {
            salts_plugin_registry *registry;
            salts_plugin_ref ref;
            bool *observed;
            ~dependent() {
                salts_plugin_lifecycle_info info = {};
                *observed = salts_plugin_registry_get_lifecycle(registry,ref,&info) == SALTS_PLUGIN_OK &&
                    info.active_leases == 1u;
            }
        };
        bool observed = false;
        try {
            salts::plugin_lease_scope owner;
            check_equal(owner.acquire(registry,ref),SALTS_PLUGIN_OK);
            dependent view{&registry,ref,&observed};
            throw SALTS_PLUGIN_BUSY;
        } catch (salts_plugin_status status) {
            check_equal(status,SALTS_PLUGIN_BUSY);
        }
        check_true(observed);
    }
    it("preserves ownership on explicit close failure and permits a corrected retry") {
        salts::plugin_lease_scope owner;
        check_equal(owner.acquire(registry,ref),SALTS_PLUGIN_OK);
        const salts_plugin_manifest *borrow = owner.manifest();
        void *saved = registry.impl;
        registry.impl = nullptr;
        const salts_plugin_status failure = owner.close();
        registry.impl = saved;
        check_equal(failure,SALTS_PLUGIN_INVALID_ARGUMENT);
        check_true(static_cast<bool>(owner));
        check_equal(owner.manifest(),borrow);
        check_equal(owner.close(),SALTS_PLUGIN_OK);
        check_null(owner.manifest());
        check_false(static_cast<bool>(owner));
        check_equal(owner.close(),SALTS_PLUGIN_OK);
    }
}
