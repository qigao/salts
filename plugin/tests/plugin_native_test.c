#include "plugin_native_fixture.h"
#include "tinytest.h"
#include <stdlib.h>

static cmeta_plugin_registry registry;
static cmeta_plugin_ref ref;
static cmeta_plugin_lease lease;
static cmeta_native_thunk thunk;
static const plugin_native_offer *offer;
static void check_one_lease(void) {
    cmeta_plugin_lifecycle_info info;
    check_equal(cmeta_plugin_registry_get_lifecycle(&registry, ref, &info), CMETA_PLUGIN_OK);
    check_equal(info.active_leases, (size_t)1);
}
suite("Plugin native opt-in keeps the original lease authority") {
    before_each() {
        const cmeta_plugin_registry_config config = {1};
        const cmeta_plugin_manifest *manifest = NULL;
        const cmeta_plugin_export *entry = NULL;
        cmeta_native_binding binding = CMETA_NATIVE_BINDING_INIT;
        check_equal(cmeta_plugin_registry_init(&registry, &config), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_load(&registry, NATIVE_PLUGIN_PATH, &ref), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_start(&registry, ref), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_acquire(&registry, ref, &lease, &manifest), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_manifest_find_export(manifest, NATIVE_OFFER_EXPORT, &entry), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_export_require_function(entry, NATIVE_OFFER_CONTRACT,
            NATIVE_OFFER_VERSION, 0), CMETA_PLUGIN_OK);
        check_true(cmeta_function_abi_contract_compatible(FunctionAbi(plugin_native_get_offer), entry->value.function.abi));
        check_true(entry->value.function.invoke(entry->value.function.context, &offer, NULL, 0));
        check_not_null(offer);
        check_true(cmeta_invokable_valid(&offer->reference));
        check_equal(cmeta_native_context_i32_admit(offer->source, offer->binding.abi,
            offer->binding.contextual, offer->binding.context, &binding), CMETA_OK);
        check_equal(cmeta_native_thunk_create(&binding, NATIVE_PLUGIN_BUDGET, &thunk), CMETA_OK);
        check_one_lease();
    }
    after_each() {
        cmeta_plugin_lifecycle_info info;
        bool quiet = false;
        /* Revoke entry use before releasing the DSO borrow. */
        check_equal(cmeta_native_thunk_destroy(&thunk), CMETA_OK);
        check_one_lease();
        offer = NULL;
        check_equal(cmeta_plugin_registry_get_lifecycle(&registry, ref, &info), CMETA_PLUGIN_OK);
        if (info.state == CMETA_PLUGIN_LIFECYCLE_STARTED)
            check_equal(cmeta_plugin_registry_request_stop(&registry, ref), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_release(&registry, &lease), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_poll_quiescent(&registry, ref, &quiet), CMETA_PLUGIN_OK);
        check_true(quiet);
        check_equal(cmeta_plugin_registry_unload(&registry, ref), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_destroy(&registry), CMETA_PLUGIN_OK);
    }
    it("matches the generated receiver and permits unload only after explicit release") {
        int input = -3, output = 0;
        const void *args[] = {&input};
        check_equal(cmeta_invokable_invoke_admitted(&offer->reference, &output, args), CMETA_OK);
        check_equal(cmeta_native_thunk_entry(&thunk)(input), output);
        check_equal(output, input + NATIVE_PLUGIN_BIAS);
        check_equal(cmeta_plugin_registry_request_stop(&registry, ref), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_unload(&registry, ref), CMETA_PLUGIN_BUSY);
        check_one_lease();
    }
#ifdef CMETA_NATIVE_PLUGIN_BENCHMARK
    bench("compares the same DSO target under one explicit lease") {
        enum { SAMPLES = 25, OPERATIONS = 1000000 };
        static volatile int sink;
        cmeta_native_i32_fn entry = cmeta_native_thunk_entry(&thunk);
        benchmark_ops("Plugin admitted receiver", SAMPLES, OPERATIONS) {
            int output = 0;
            for (int i = 0; i < OPERATIONS; ++i) {
                const void *args[] = {&i};
                if (cmeta_invokable_invoke_admitted(&offer->reference, &output, args) != CMETA_OK) abort();
            }
            sink = output;
        }
        check_equal(sink, OPERATIONS - 1 + NATIVE_PLUGIN_BIAS);
        benchmark_ops("Plugin native receiver", SAMPLES, OPERATIONS) {
            int output = 0;
            for (int i = 0; i < OPERATIONS; ++i) output = entry(i);
            sink = output;
        }
        check_equal(sink, OPERATIONS - 1 + NATIVE_PLUGIN_BIAS);
        check_one_lease();
    }
#endif
}
