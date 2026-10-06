#include <salts/plugin.h>
#include <salts/thread.h>
#include <tinytest.h>

#include <cmeta/object_interface.h>

#include "plugin_generic_graph_fixture.h"
#include "plugin_object_interface_fixture.h"
#include "plugin_slow_query_fixture.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#ifndef PLUGIN_VALID_C_PATH
#error "PLUGIN_VALID_C_PATH is required"
#endif
#ifndef PLUGIN_VALID_CPP_PATH
#error "PLUGIN_VALID_CPP_PATH is required"
#endif
#ifndef PLUGIN_OBJECT_INTERFACE_PATH
#error "PLUGIN_OBJECT_INTERFACE_PATH is required"
#endif
#ifndef PLUGIN_GENERIC_GRAPH_PATH
#error "PLUGIN_GENERIC_GRAPH_PATH is required"
#endif
#ifndef PLUGIN_MISSING_QUERY_PATH
#error "PLUGIN_MISSING_QUERY_PATH is required"
#endif
#ifndef PLUGIN_REJECTED_PATH
#error "PLUGIN_REJECTED_PATH is required"
#endif
#ifndef PLUGIN_OBSOLETE_PATH
#error "PLUGIN_OBSOLETE_PATH is required"
#endif
#ifndef PLUGIN_INVALID_PATH
#error "PLUGIN_INVALID_PATH is required"
#endif
#ifndef PLUGIN_LIFECYCLE_PATH
#error "PLUGIN_LIFECYCLE_PATH is required"
#endif
#ifndef PLUGIN_SLOW_QUERY_A_PATH
#error "PLUGIN_SLOW_QUERY_A_PATH is required"
#endif
#ifndef PLUGIN_SLOW_QUERY_B_PATH
#error "PLUGIN_SLOW_QUERY_B_PATH is required"
#endif

CMETA_OBJECT_INTERFACE_ADAPTER(plugin_object_fixture_api);

typedef struct plugin_object_projection_context {
    const cmeta_plugin_export *interface_export;
} plugin_object_projection_context;

static cmeta_status plugin_object_project_interface(
    void *context,
    const cmeta_object_ref *object,
    const cmeta_interface_desc *expected,
    cmeta_interface_projection *out) {
    const plugin_object_projection_context *projection =
        (const plugin_object_projection_context *)context;
    const cmeta_plugin_export *entry;
    const plugin_object_fixture_api *api;

    if (projection == NULL || object == NULL || expected == NULL ||
        out == NULL)
        return CMETA_INVALID_ARGUMENT;
    entry = projection->interface_export;
    if (entry == NULL || entry->kind != CMETA_PLUGIN_EXPORT_INTERFACE ||
        entry->value.interface.desc == NULL ||
        entry->value.interface.value == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (!cmeta_interface_desc_equal(entry->value.interface.desc, expected))
        return CMETA_TRAIT_MISSING;

    api = (const plugin_object_fixture_api *)entry->value.interface.value;
    if (!plugin_object_fixture_api_valid(api) || api->self != object->object)
        return CMETA_TYPE_MISMATCH;

    *out = (cmeta_interface_projection)CMETA_INTERFACE_PROJECTION_INIT;
    out->interface = entry->value.interface.desc;
    out->self = api->self;
    out->dispatch = api->vtable;
    return CMETA_OK;
}

typedef struct plugin_slow_load_context {
    cmeta_plugin_registry *registry;
    const char *path;
    cmeta_plugin_status status;
    cmeta_plugin_ref ref;
} plugin_slow_load_context;

typedef struct plugin_destroy_context {
    cmeta_plugin_registry *registry;
    cmeta_plugin_status status;
    atomic_bool done;
} plugin_destroy_context;

static bool marker_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return false;
    fclose(file);
    return true;
}

static void touch_marker(const char *path) {
    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return;
    fputs("release\n", file);
    fclose(file);
}

static bool wait_for_marker(const char *path, uint32_t timeout_ms) {
    uint32_t elapsed = 0u;
    while (elapsed < timeout_ms) {
        if (marker_exists(path))
            return true;
        cmeta_sleep_ms(1u);
        ++elapsed;
    }
    return marker_exists(path);
}

static bool wait_for_atomic_true(
    atomic_bool *value, uint32_t timeout_ms) {
    uint32_t elapsed = 0u;
    while (elapsed < timeout_ms) {
        if (atomic_load(value))
            return true;
        cmeta_sleep_ms(1u);
        ++elapsed;
    }
    return atomic_load(value);
}

static void plugin_slow_load_thread(void *arg) {
    plugin_slow_load_context *context =
        (plugin_slow_load_context *)arg;
    context->status = cmeta_plugin_registry_load(
        context->registry, context->path, &context->ref);
}

static void plugin_destroy_thread(void *arg) {
    plugin_destroy_context *context =
        (plugin_destroy_context *)arg;
    context->status = cmeta_plugin_registry_destroy(context->registry);
    atomic_store(&context->done, true);
}

static cmeta_plugin_registry make_registry(size_t capacity) {
    cmeta_plugin_registry registry = {0};
    cmeta_plugin_registry_config config = {capacity};
    check_equal(cmeta_plugin_registry_init(&registry, &config),
                CMETA_PLUGIN_OK);
    check_not_null(registry.impl);
    return registry;
}

static void destroy_registry(cmeta_plugin_registry *registry) {
    check_equal(cmeta_plugin_registry_destroy(registry),
                CMETA_PLUGIN_OK);
    check_null(registry->impl);
}

spec("Salts Plugin loader registry") {
describe("bounded registry") {
    it("initializes a fixed-capacity registry and destroys idempotently") {
        cmeta_plugin_registry registry = {0};
        cmeta_plugin_registry_config zero = {0u};
        cmeta_plugin_registry_config one = {1u};

        check_equal(cmeta_plugin_registry_init(NULL, &one),
                    CMETA_PLUGIN_INVALID_ARGUMENT);
        check_equal(cmeta_plugin_registry_init(&registry, NULL),
                    CMETA_PLUGIN_INVALID_ARGUMENT);
        check_equal(cmeta_plugin_registry_init(&registry, &zero),
                    CMETA_PLUGIN_INVALID_ARGUMENT);
        check_equal(cmeta_plugin_registry_init(&registry, &one),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_count(&registry), (size_t)0u);
        check_equal(cmeta_plugin_registry_init(&registry, &one),
                    CMETA_PLUGIN_INVALID_ARGUMENT);
        destroy_registry(&registry);
        check_equal(cmeta_plugin_registry_destroy(&registry),
                    CMETA_PLUGIN_OK);
    }

    it("loads C and C++ query entries and publishes stable refs") {
        cmeta_plugin_registry registry = make_registry(2u);
        cmeta_plugin_ref c_ref = {0};
        cmeta_plugin_ref cpp_ref = {0};
        cmeta_plugin_ref found = {0};
        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_VALID_C_PATH, &c_ref),
                    CMETA_PLUGIN_OK);
        check_true(cmeta_plugin_ref_valid(c_ref));
        check_equal(cmeta_plugin_registry_count(&registry), (size_t)1u);

        check_equal(cmeta_plugin_registry_find(
                        &registry, "test.loader.c", &found),
                    CMETA_PLUGIN_OK);
        check_equal(found.slot, c_ref.slot);
        check_equal(found.generation, c_ref.generation);

        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_VALID_CPP_PATH, &cpp_ref),
                    CMETA_PLUGIN_OK);
        check_true(cmeta_plugin_ref_valid(cpp_ref));
        check_true(cpp_ref.slot != c_ref.slot);
        check_equal(cmeta_plugin_registry_count(&registry), (size_t)2u);

        check_equal(cmeta_plugin_registry_find(
                        &registry, "test.loader.cpp", &found),
                    CMETA_PLUGIN_OK);
        check_equal(found.slot, cpp_ref.slot);
        check_equal(found.generation, cpp_ref.generation);

        destroy_registry(&registry);
    }

    it("executes a reflected Function export through a real DSO lease") {
        cmeta_plugin_registry registry = make_registry(1u);
        cmeta_plugin_ref ref = {0};
        cmeta_plugin_lease lease = {0};
        const cmeta_plugin_manifest *manifest = NULL;
        const cmeta_plugin_export *entry = NULL;
        void *params[1];
        int input = 9;
        int output = 0;
        bool quiescent = false;

        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_VALID_C_PATH, &ref),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_start(&registry, ref),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_acquire(
                        &registry, ref, &lease, &manifest),
                    CMETA_PLUGIN_OK);
        check_true(cmeta_plugin_lease_valid(lease));
        check_not_null(manifest);

        check_equal(cmeta_plugin_manifest_find_export(
                        manifest, "test.loader.math.double", &entry),
                    CMETA_PLUGIN_OK);
        check_not_null(entry);
        check_equal(cmeta_plugin_export_require_function(
                        entry, "test.loader.math", 1u, 1u),
                    CMETA_PLUGIN_OK);
        check_true(cmeta_function_desc_valid(entry->value.function.desc));
        check_true(cmeta_function_abi_desc_valid(entry->value.function.abi));
        check_true(entry->value.function.abi->function == entry->value.function.desc);

        params[0] = &input;
        check_true(entry->value.function.invoke(
            entry->value.function.context, &output, params, 1u));
        check_equal(output, 18);

        check_equal(cmeta_plugin_registry_release(&registry, &lease),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_request_stop(&registry, ref),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_poll_quiescent(
                        &registry, ref, &quiescent),
                    CMETA_PLUGIN_OK);
        check_true(quiescent);
        check_equal(cmeta_plugin_registry_unload(&registry, ref),
                    CMETA_PLUGIN_OK);
        destroy_registry(&registry);
    }

    it("keeps reflected generic descriptor graphs borrowed under one Plugin lease") {
        cmeta_plugin_registry registry = make_registry(1u);
        cmeta_plugin_ref ref = {0};
        cmeta_plugin_lease lease = {0};
        const cmeta_plugin_manifest *manifest = NULL;
        const cmeta_plugin_export *entry = NULL;
        const cmeta_function_desc *provider_function = NULL;
        const cmeta_function_abi_desc *provider_abi = NULL;
        const cmeta_param_desc *provider_param = NULL;
        const cmeta_function_desc *host_function =
            &plugin_generic_graph_probe__function_meta;
        const cmeta_function_abi_desc *host_abi =
            &plugin_generic_graph_probe__function_abi_meta;
        const cmeta_param_desc *host_param =
            &plugin_generic_graph_probe__function_params[0];
        const cmeta_type_identity *provider_identity = NULL;
        const cmeta_generic_desc *provider_constructor = NULL;
        const cmeta_type_identity *provider_argument = NULL;
        plugin_generic_graph_value value = {41};
        void *params[1] = {&value};
        int output = 0;
        bool quiescent = true;

        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_GENERIC_GRAPH_PATH, &ref),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_start(&registry, ref),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_acquire(
                        &registry, ref, &lease, &manifest),
                    CMETA_PLUGIN_OK);
        check_true(cmeta_plugin_lease_valid(lease));
        check_not_null(manifest);

        check_equal(cmeta_plugin_manifest_find_export(
                        manifest, "generic.probe", &entry),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_export_require_function(
                        entry, "test.plugin.generic", 1u, 1u),
                    CMETA_PLUGIN_OK);

        provider_function = entry->value.function.desc;
        provider_abi = entry->value.function.abi;
        provider_param = cmeta_function_param(provider_function, 0u);
        check_not_null(provider_function);
        check_not_null(provider_abi);
        check_not_null(provider_param);
        check_true(cmeta_function_desc_valid(provider_function));
        check_true(cmeta_function_abi_desc_valid(provider_abi));

        /*
         * The provider DSO and host compiled the fixture header independently.
         * Address inequality proves that semantic equality, not descriptor
         * pointer identity, crosses the module boundary.
         */
        check_true(provider_function != host_function);
        check_true(provider_abi != host_abi);
        check_true(provider_param != host_param);
        check_true(provider_param->type != &plugin_generic_graph_value_type);
        check_true(provider_param->type->identity !=
                   &plugin_generic_graph_value_identity);

        check_true(cmeta_function_desc_equal(
            provider_function, host_function));
        check_true(cmeta_function_abi_desc_equal(provider_abi, host_abi));
        check_true(cmeta_type_equal(
            provider_param->type, &plugin_generic_graph_value_type));

        provider_identity = cmeta_type_identity_of(provider_param->type);
        check_true(cmeta_type_identity_is_application(provider_identity));
        check_equal(cmeta_type_identity_arity(provider_identity), (size_t)1u);
        provider_constructor =
            cmeta_type_identity_constructor(provider_identity);
        provider_argument =
            cmeta_type_identity_argument(provider_identity, 0u);
        check_not_null(provider_constructor);
        check_not_null(provider_argument);
        check_true(provider_constructor != &plugin_generic_graph_constructor);
        check_true(provider_argument != &plugin_generic_graph_arg_identity);
        check_true(cmeta_generic_desc_equal(
            provider_constructor, &plugin_generic_graph_constructor));
        check_true(cmeta_type_identity_equal(
            provider_argument, &plugin_generic_graph_arg_identity));

        check_true(entry->value.function.invoke(
            entry->value.function.context, &output, params, 1u));
        check_equal(output, 42);

        check_equal(cmeta_plugin_registry_request_stop(&registry, ref),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_unload(&registry, ref),
                    CMETA_PLUGIN_BUSY);
        check_equal(cmeta_plugin_registry_poll_quiescent(
                        &registry, ref, &quiescent),
                    CMETA_PLUGIN_OK);
        check_false(quiescent);

        /*
         * Every pointer above is borrowed from the provider graph. Drop those
         * views before releasing the one module lifetime authority.
         */
        provider_argument = NULL;
        provider_constructor = NULL;
        provider_identity = NULL;
        provider_param = NULL;
        provider_abi = NULL;
        provider_function = NULL;
        entry = NULL;
        manifest = NULL;

        check_equal(cmeta_plugin_registry_release(&registry, &lease),
                    CMETA_PLUGIN_OK);
        check_false(cmeta_plugin_lease_valid(lease));
        check_equal(cmeta_plugin_registry_poll_quiescent(
                        &registry, ref, &quiescent),
                    CMETA_PLUGIN_OK);
        check_true(quiescent);
        check_equal(cmeta_plugin_registry_unload(&registry, ref),
                    CMETA_PLUGIN_OK);
        destroy_registry(&registry);
    }

    it("keeps ObjectRef and Interface views lease-bound to one DSO identity") {
        cmeta_plugin_registry registry = make_registry(1u);
        cmeta_plugin_ref ref = {0};
        cmeta_plugin_lease lease = {0};
        const cmeta_plugin_manifest *manifest = NULL;
        const cmeta_plugin_export *interface_entry = NULL;
        const cmeta_plugin_export *identity_entry = NULL;
        plugin_object_fixture_state *state = NULL;
        plugin_object_fixture_api *exported_api = NULL;
        plugin_object_fixture_api projected =
            plugin_object_fixture_api_bind(NULL, NULL);
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        plugin_object_projection_context projection_context = {0};
        cmeta_object_interface_provider provider = {
            .size = sizeof(cmeta_object_interface_provider),
            .context = &projection_context,
            .project = plugin_object_project_interface
        };
        const cmeta_data_desc *field_data = NULL;
        const void *field_value = NULL;
        bool quiescent = true;

        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_OBJECT_INTERFACE_PATH, &ref),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_start(&registry, ref),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_acquire(
                        &registry, ref, &lease, &manifest),
                    CMETA_PLUGIN_OK);
        check_true(cmeta_plugin_lease_valid(lease));
        check_not_null(manifest);

        check_equal(cmeta_plugin_manifest_find_export(
                        manifest, "service", &interface_entry),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_export_require_interface(
                        interface_entry, "test.object.service", 1u, 1u,
                        plugin_object_fixture_api_interface()),
                    CMETA_PLUGIN_OK);
        check_not_null(interface_entry);
        exported_api = (plugin_object_fixture_api *)
            interface_entry->value.interface.value;
        check_true(plugin_object_fixture_api_valid(exported_api));

        check_equal(cmeta_plugin_manifest_find_export(
                        manifest, "borrow_identity", &identity_entry),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_export_require_function(
                        identity_entry, "test.object.identity", 1u, 1u),
                    CMETA_PLUGIN_OK);
        check_not_null(identity_entry);
        check_true(cmeta_function_desc_valid(identity_entry->value.function.desc));
        check_true(cmeta_function_abi_desc_valid(identity_entry->value.function.abi));
        check_equal(
            identity_entry->value.function.desc->result_flags &
                CMETA_RESULT_CLASS_MASK,
            CMETA_RESULT_BORROWED);
        check_equal(identity_entry->value.function.abi->return_carrier,
                    CMETA_ABI_OBJECT_POINTER);
        check_true(identity_entry->value.function.invoke(
            identity_entry->value.function.context, &state, NULL, 0u));
        check_not_null(state);
        check_true(exported_api->self == state);

        check_true(cmeta_data_desc_valid(&plugin_object_fixture_data));
        check_equal(cmeta_object_borrow(
                        &object, state, &plugin_object_fixture_data, NULL),
                    CMETA_OK);
        check_true(cmeta_object_ref_valid(&object));

        projection_context.interface_export = interface_entry;
        check_equal(plugin_object_fixture_api_borrow_from_object(
                        &object, &provider, &projected),
                    CMETA_OK);
        check_true(plugin_object_fixture_api_valid(&projected));
        check_true(projected.self == state);
        check_true(projected.vtable == exported_api->vtable);

        check_equal(cmeta_object_field_read(
                        &object, "value", &field_data, &field_value),
                    CMETA_OK);
        check_true(field_data == &cmeta_data_int);
        check_true(field_value == &state->value);
        check_equal(*(const int *)field_value, 7);

        check_equal(plugin_object_fixture_api_add(&projected, 5), 12);
        check_equal(plugin_object_fixture_api_value(exported_api), 12);
        check_equal(cmeta_object_field_read(
                        &object, "value", &field_data, &field_value),
                    CMETA_OK);
        check_equal(*(const int *)field_value, 12);

        check_equal(cmeta_plugin_registry_request_stop(&registry, ref),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_unload(&registry, ref),
                    CMETA_PLUGIN_BUSY);
        check_equal(cmeta_plugin_registry_poll_quiescent(
                        &registry, ref, &quiescent),
                    CMETA_PLUGIN_OK);
        check_false(quiescent);

        projected = plugin_object_fixture_api_bind(NULL, NULL);
        exported_api = NULL;
        interface_entry = NULL;
        identity_entry = NULL;
        manifest = NULL;
        cmeta_object_release(&object);
        check_false(cmeta_object_ref_valid(&object));
        state = NULL;

        check_equal(cmeta_plugin_registry_release(&registry, &lease),
                    CMETA_PLUGIN_OK);
        check_false(cmeta_plugin_lease_valid(lease));
        check_equal(cmeta_plugin_registry_poll_quiescent(
                        &registry, ref, &quiescent),
                    CMETA_PLUGIN_OK);
        check_true(quiescent);
        check_equal(cmeta_plugin_registry_unload(&registry, ref),
                    CMETA_PLUGIN_OK);
        destroy_registry(&registry);
    }

    it("reports stale generation without exposing slot storage") {
        cmeta_plugin_registry registry = make_registry(1u);
        cmeta_plugin_ref ref = {0};
        cmeta_plugin_ref stale;
        cmeta_plugin_lifecycle_info info = {0};

        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_VALID_C_PATH, &ref),
                    CMETA_PLUGIN_OK);
        stale = ref;
        ++stale.generation;
        if (stale.generation == 0u)
            stale.generation = 1u;

        check_equal(cmeta_plugin_registry_get_lifecycle(
                        &registry, stale, &info),
                    CMETA_PLUGIN_STALE);

        stale = ref;
        stale.slot = UINT32_MAX;
        check_equal(cmeta_plugin_registry_get_lifecycle(
                        &registry, stale, &info),
                    CMETA_PLUGIN_STALE);

        destroy_registry(&registry);
    }
}

describe("transactional admission") {
    it("runs plugin query without holding the registry lock") {
        cmeta_plugin_registry registry = make_registry(1u);
        plugin_slow_load_context load = {
            &registry, PLUGIN_SLOW_QUERY_A_PATH,
            CMETA_PLUGIN_INVALID_STATE, {0}
        };
        plugin_destroy_context destroy = {
            &registry, CMETA_PLUGIN_INVALID_STATE
        };
        cmeta_thread_t load_thread = NULL;
        cmeta_thread_t destroy_thread = NULL;
        bool destroy_completed;

        atomic_init(&destroy.done, false);
        (void)remove(PLUGIN_SLOW_QUERY_ENTERED_MARKER_A);
        (void)remove(PLUGIN_SLOW_QUERY_RELEASE_MARKER);

        check_equal(cmeta_thread_create(
                        &load_thread, plugin_slow_load_thread, &load),
                    0);
        check_true(wait_for_marker(
            PLUGIN_SLOW_QUERY_ENTERED_MARKER_A, 5000u));

        check_equal(cmeta_thread_create(
                        &destroy_thread, plugin_destroy_thread, &destroy),
                    0);

        destroy_completed = wait_for_atomic_true(&destroy.done, 500u);
        touch_marker(PLUGIN_SLOW_QUERY_RELEASE_MARKER);

        check_equal(cmeta_thread_join(&destroy_thread), 0);
        cmeta_thread_destroy(&destroy_thread);
        check_equal(cmeta_thread_join(&load_thread), 0);
        cmeta_thread_destroy(&load_thread);

        check_true(destroy_completed);
        check_equal(destroy.status, CMETA_PLUGIN_BUSY);
        check_equal(load.status, CMETA_PLUGIN_OK);
        check_true(cmeta_plugin_ref_valid(load.ref));
        check_equal(cmeta_plugin_registry_count(&registry), (size_t)1u);

        if (destroy.status == CMETA_PLUGIN_BUSY &&
            cmeta_plugin_ref_valid(load.ref))
            check_equal(cmeta_plugin_registry_unload(
                            &registry, load.ref),
                        CMETA_PLUGIN_OK);

        destroy_registry(&registry);
        (void)remove(PLUGIN_SLOW_QUERY_ENTERED_MARKER_A);
        (void)remove(PLUGIN_SLOW_QUERY_RELEASE_MARKER);
    }

    it("publishes at most one concurrent duplicate plugin") {
        cmeta_plugin_registry registry = make_registry(2u);
        plugin_slow_load_context first = {
            &registry, PLUGIN_SLOW_QUERY_A_PATH,
            CMETA_PLUGIN_INVALID_STATE, {0}
        };
        plugin_slow_load_context second = {
            &registry, PLUGIN_SLOW_QUERY_B_PATH,
            CMETA_PLUGIN_INVALID_STATE, {0}
        };
        cmeta_thread_t first_thread = NULL;
        cmeta_thread_t second_thread = NULL;
        cmeta_plugin_ref published = {0};
        unsigned ok_count = 0u;
        unsigned duplicate_count = 0u;

        (void)remove(PLUGIN_SLOW_QUERY_ENTERED_MARKER_A);
        (void)remove(PLUGIN_SLOW_QUERY_ENTERED_MARKER_B);
        (void)remove(PLUGIN_SLOW_QUERY_RELEASE_MARKER);

        check_equal(cmeta_thread_create(
                        &first_thread, plugin_slow_load_thread, &first),
                    0);
        check_equal(cmeta_thread_create(
                        &second_thread, plugin_slow_load_thread, &second),
                    0);

        check_true(wait_for_marker(
            PLUGIN_SLOW_QUERY_ENTERED_MARKER_A, 5000u));
        check_true(wait_for_marker(
            PLUGIN_SLOW_QUERY_ENTERED_MARKER_B, 5000u));

        touch_marker(PLUGIN_SLOW_QUERY_RELEASE_MARKER);

        check_equal(cmeta_thread_join(&first_thread), 0);
        cmeta_thread_destroy(&first_thread);
        check_equal(cmeta_thread_join(&second_thread), 0);
        cmeta_thread_destroy(&second_thread);

        if (first.status == CMETA_PLUGIN_OK) {
            ++ok_count;
            published = first.ref;
        } else if (first.status == CMETA_PLUGIN_DUPLICATE_PLUGIN_ID) {
            ++duplicate_count;
        }

        if (second.status == CMETA_PLUGIN_OK) {
            ++ok_count;
            published = second.ref;
        } else if (second.status == CMETA_PLUGIN_DUPLICATE_PLUGIN_ID) {
            ++duplicate_count;
        }

        check_equal(ok_count, 1u);
        check_equal(duplicate_count, 1u);
        check_equal(cmeta_plugin_registry_count(&registry), (size_t)1u);
        check_true(cmeta_plugin_ref_valid(published));
        check_equal(cmeta_plugin_registry_unload(&registry, published),
                    CMETA_PLUGIN_OK);

        destroy_registry(&registry);
        (void)remove(PLUGIN_SLOW_QUERY_ENTERED_MARKER_A);
        (void)remove(PLUGIN_SLOW_QUERY_ENTERED_MARKER_B);
        (void)remove(PLUGIN_SLOW_QUERY_RELEASE_MARKER);
    }

    it("rejects duplicate plugin IDs and preserves the published instance") {
        cmeta_plugin_registry registry = make_registry(2u);
        cmeta_plugin_ref first = {0};
        cmeta_plugin_ref duplicate = {9u, 9u};
        cmeta_plugin_ref found = {0};

        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_VALID_C_PATH, &first),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_VALID_C_PATH, &duplicate),
                    CMETA_PLUGIN_DUPLICATE_PLUGIN_ID);
        check_false(cmeta_plugin_ref_valid(duplicate));
        check_equal(cmeta_plugin_registry_count(&registry), (size_t)1u);
        check_equal(cmeta_plugin_registry_find(
                        &registry, "test.loader.c", &found),
                    CMETA_PLUGIN_OK);
        check_equal(found.slot, first.slot);
        check_equal(found.generation, first.generation);

        destroy_registry(&registry);
    }

    it("rejects capacity before opening another plugin") {
        cmeta_plugin_registry registry = make_registry(1u);
        cmeta_plugin_ref first = {0};
        cmeta_plugin_ref rejected = {7u, 7u};

        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_VALID_C_PATH, &first),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_VALID_CPP_PATH, &rejected),
                    CMETA_PLUGIN_CAPACITY_EXCEEDED);
        check_false(cmeta_plugin_ref_valid(rejected));
        check_equal(cmeta_plugin_registry_count(&registry), (size_t)1u);

        destroy_registry(&registry);
    }

    it("keeps missing, rejected, obsolete and incompatible failures distinct") {
        cmeta_plugin_registry registry = make_registry(2u);
        cmeta_plugin_ref ref = {3u, 3u};
        const char *missing_file = PLUGIN_VALID_C_PATH ".missing";

        check_equal(cmeta_plugin_registry_load(
                        &registry, missing_file, &ref),
                    CMETA_PLUGIN_LOAD_FAILED);
        check_false(cmeta_plugin_ref_valid(ref));
        check_equal(cmeta_plugin_registry_count(&registry), (size_t)0u);

        ref = (cmeta_plugin_ref){3u, 3u};
        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_MISSING_QUERY_PATH, &ref),
                    CMETA_PLUGIN_QUERY_MISSING);
        check_false(cmeta_plugin_ref_valid(ref));
        check_equal(cmeta_plugin_registry_count(&registry), (size_t)0u);

        ref = (cmeta_plugin_ref){3u, 3u};
        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_REJECTED_PATH, &ref),
                    CMETA_PLUGIN_QUERY_REJECTED);
        check_false(cmeta_plugin_ref_valid(ref));
        check_equal(cmeta_plugin_registry_count(&registry), (size_t)0u);

        ref = (cmeta_plugin_ref){3u, 3u};
        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_OBSOLETE_PATH, &ref),
                    CMETA_PLUGIN_QUERY_REJECTED);
        check_false(cmeta_plugin_ref_valid(ref));
        check_equal(cmeta_plugin_registry_count(&registry), (size_t)0u);

        ref = (cmeta_plugin_ref){3u, 3u};
        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_INVALID_PATH, &ref),
                    CMETA_PLUGIN_UNSUPPORTED_ABI);
        check_false(cmeta_plugin_ref_valid(ref));
        check_equal(cmeta_plugin_registry_count(&registry), (size_t)0u);

        destroy_registry(&registry);
    }

    it("admits a complete lifecycle callback group without invoking it") {
        cmeta_plugin_registry registry = make_registry(1u);
        cmeta_plugin_ref ref = {0};
        cmeta_plugin_lifecycle_info info = {0};

        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_LIFECYCLE_PATH, &ref),
                    CMETA_PLUGIN_OK);
        check_true(cmeta_plugin_ref_valid(ref));
        check_equal(cmeta_plugin_registry_count(&registry), (size_t)1u);
        check_equal(cmeta_plugin_registry_get_lifecycle(
                        &registry, ref, &info),
                    CMETA_PLUGIN_OK);
        check_equal(info.state, CMETA_PLUGIN_LIFECYCLE_LOADED);
        check_equal(info.active_leases, (size_t)0u);
        check_equal(info.callbacks_inflight, (size_t)0u);

        check_equal(cmeta_plugin_registry_unload(&registry, ref),
                    CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_count(&registry), (size_t)0u);
        destroy_registry(&registry);
    }

    it("rejects malformed UTF-8 paths before platform loading") {
        cmeta_plugin_registry registry = make_registry(1u);
        cmeta_plugin_ref ref = {5u, 5u};
        const char invalid_utf8[] = {(char)0xc0, (char)0xaf, '\0'};

        check_equal(cmeta_plugin_registry_load(
                        &registry, invalid_utf8, &ref),
                    CMETA_PLUGIN_INVALID_ARGUMENT);
        check_false(cmeta_plugin_ref_valid(ref));
        check_equal(cmeta_plugin_registry_count(&registry), (size_t)0u);

        destroy_registry(&registry);
    }
}

describe("lookup contract") {
    it("distinguishes bad input from unknown plugin") {
        cmeta_plugin_registry registry = make_registry(1u);
        cmeta_plugin_ref ref = {4u, 4u};

        check_equal(cmeta_plugin_registry_find(
                        &registry, "", &ref),
                    CMETA_PLUGIN_INVALID_ARGUMENT);
        check_false(cmeta_plugin_ref_valid(ref));

        ref = (cmeta_plugin_ref){4u, 4u};
        check_equal(cmeta_plugin_registry_find(
                        &registry, "not.loaded", &ref),
                    CMETA_PLUGIN_UNKNOWN_PLUGIN);
        check_false(cmeta_plugin_ref_valid(ref));

        destroy_registry(&registry);
    }
}
}
