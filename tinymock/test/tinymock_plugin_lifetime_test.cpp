#include "tinytest.hpp"
#include "tinymock_history.h"
#include <salts/plugin_scope.h>

struct plugin_history {
    tinymock_cmeta_history history{};
    tinymock_cmeta_captor captor{};
    cmeta_plugin_registry *registry;
    cmeta_plugin_ref ref;
    bool *released_inside_lease;
};
static void release_plugin_history(void *, void *resource) {
    auto *state = static_cast<plugin_history *>(resource);
    tinymock_cmeta_captor_destroy(&state->captor);
    tinymock_cmeta_history_destroy(&state->history);
    cmeta_plugin_lifecycle_info info{};
    *state->released_inside_lease =
        cmeta_plugin_registry_get_lifecycle(state->registry, state->ref, &info) == CMETA_PLUGIN_OK &&
        info.active_leases == 1;
}

suite("TinyMock borrowed Plugin metadata") {
    it("destroys history and captor before the sole Plugin lease during exception unwind") {
        cmeta_plugin_registry registry{};
        cmeta_plugin_ref ref{};
        const cmeta_plugin_registry_config config{1};
        bool released_inside_lease = false;
        check_equal(cmeta_plugin_registry_init(&registry, &config), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_load(&registry, TINYMOCK_PLUGIN_PATH, &ref), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_start(&registry, ref), CMETA_PLUGIN_OK);
        try {
            salts::plugin_lease_scope lease;
            check_equal(lease.acquire(registry, ref), CMETA_PLUGIN_OK);
            const cmeta_plugin_export *entry = nullptr;
            check_equal(cmeta_plugin_manifest_find_export(lease.manifest(), "increment", &entry), CMETA_PLUGIN_OK);
            plugin_history state{{}, {}, &registry, ref, &released_inside_lease};
            cmeta_cleanup obligation = CMETA_CLEANUP_INIT;
            tinymock_cmeta_history_init(&state.history, entry->value.function.desc);
            tinymock_cmeta_captor_init(&state.captor);
            check_equal(cmeta_cleanup_arm(&obligation, release_plugin_history, nullptr, &state), CMETA_OK);
            cmeta::cleanup_scope guard(obligation);
            int input = 17;
            tinymock_cmeta_arg_view argument{&input, false, nullptr};
            check_true(tinymock_cmeta_history_record_admitted(&state.history, 1, &argument));
            check_true(tinymock_cmeta_captor_capture(&state.captor, &state.history, 0, 0));
            cmeta_plugin_manifest old = *lease.manifest();
            --old.abi_version;
            check_equal(cmeta_plugin_manifest_validate(&old), CMETA_PLUGIN_UNSUPPORTED_ABI);
            check_equal(cmeta_plugin_registry_request_stop(&registry, ref), CMETA_PLUGIN_OK);
            check_equal(cmeta_plugin_registry_unload(&registry, ref), CMETA_PLUGIN_BUSY);
            throw 1;
        } catch (int) {}
        check_true(released_inside_lease);
        cmeta_plugin_lifecycle_info info{};
        check_equal(cmeta_plugin_registry_get_lifecycle(&registry, ref, &info), CMETA_PLUGIN_OK);
        check_equal(info.active_leases, size_t{0});
        bool quiet = false;
        check_equal(cmeta_plugin_registry_poll_quiescent(&registry, ref, &quiet), CMETA_PLUGIN_OK);
        check_true(quiet);
        check_equal(cmeta_plugin_registry_unload(&registry, ref), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_destroy(&registry), CMETA_PLUGIN_OK);
    }
}
