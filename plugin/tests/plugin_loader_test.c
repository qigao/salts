#include <salts/plugin.h>
#include <salts/thread.h>
#include <tinytest.h>

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

typedef struct plugin_slow_load_context {
    salts_plugin_registry *registry;
    const char *path;
    salts_plugin_status status;
    salts_plugin_ref ref;
} plugin_slow_load_context;

typedef struct plugin_destroy_context {
    salts_plugin_registry *registry;
    salts_plugin_status status;
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
        salts_sleep_ms(1u);
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
        salts_sleep_ms(1u);
        ++elapsed;
    }
    return atomic_load(value);
}

static void plugin_slow_load_thread(void *arg) {
    plugin_slow_load_context *context =
        (plugin_slow_load_context *)arg;
    context->status = salts_plugin_registry_load(
        context->registry, context->path, &context->ref);
}

static void plugin_destroy_thread(void *arg) {
    plugin_destroy_context *context =
        (plugin_destroy_context *)arg;
    context->status = salts_plugin_registry_destroy(context->registry);
    atomic_store(&context->done, true);
}

static salts_plugin_registry make_registry(size_t capacity) {
    salts_plugin_registry registry = {0};
    salts_plugin_registry_config config = {capacity};
    check_equal(salts_plugin_registry_init(&registry, &config),
                SALTS_PLUGIN_OK);
    check_not_null(registry.impl);
    return registry;
}

static void destroy_registry(salts_plugin_registry *registry) {
    check_equal(salts_plugin_registry_destroy(registry),
                SALTS_PLUGIN_OK);
    check_null(registry->impl);
}

spec("Salts Plugin loader registry") {
describe("bounded registry") {
    it("initializes a fixed-capacity registry and destroys idempotently") {
        salts_plugin_registry registry = {0};
        salts_plugin_registry_config zero = {0u};
        salts_plugin_registry_config one = {1u};

        check_equal(salts_plugin_registry_init(NULL, &one),
                    SALTS_PLUGIN_INVALID_ARGUMENT);
        check_equal(salts_plugin_registry_init(&registry, NULL),
                    SALTS_PLUGIN_INVALID_ARGUMENT);
        check_equal(salts_plugin_registry_init(&registry, &zero),
                    SALTS_PLUGIN_INVALID_ARGUMENT);
        check_equal(salts_plugin_registry_init(&registry, &one),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_count(&registry), (size_t)0u);
        check_equal(salts_plugin_registry_init(&registry, &one),
                    SALTS_PLUGIN_INVALID_ARGUMENT);
        destroy_registry(&registry);
        check_equal(salts_plugin_registry_destroy(&registry),
                    SALTS_PLUGIN_OK);
    }

    it("loads C and C++ query entries and publishes stable refs") {
        salts_plugin_registry registry = make_registry(2u);
        salts_plugin_ref c_ref = {0};
        salts_plugin_ref cpp_ref = {0};
        salts_plugin_ref found = {0};
        check_equal(salts_plugin_registry_load(
                        &registry, PLUGIN_VALID_C_PATH, &c_ref),
                    SALTS_PLUGIN_OK);
        check_true(salts_plugin_ref_valid(c_ref));
        check_equal(salts_plugin_registry_count(&registry), (size_t)1u);

        check_equal(salts_plugin_registry_find(
                        &registry, "test.loader.c", &found),
                    SALTS_PLUGIN_OK);
        check_equal(found.slot, c_ref.slot);
        check_equal(found.generation, c_ref.generation);

        check_equal(salts_plugin_registry_load(
                        &registry, PLUGIN_VALID_CPP_PATH, &cpp_ref),
                    SALTS_PLUGIN_OK);
        check_true(salts_plugin_ref_valid(cpp_ref));
        check_true(cpp_ref.slot != c_ref.slot);
        check_equal(salts_plugin_registry_count(&registry), (size_t)2u);

        check_equal(salts_plugin_registry_find(
                        &registry, "test.loader.cpp", &found),
                    SALTS_PLUGIN_OK);
        check_equal(found.slot, cpp_ref.slot);
        check_equal(found.generation, cpp_ref.generation);

        destroy_registry(&registry);
    }

    it("executes a reflected Function export through a real DSO lease") {
        salts_plugin_registry registry = make_registry(1u);
        salts_plugin_ref ref = {0};
        salts_plugin_lease lease = {0};
        const salts_plugin_manifest *manifest = NULL;
        const salts_plugin_export *entry = NULL;
        void *params[1];
        int input = 9;
        int output = 0;
        bool quiescent = false;

        check_equal(salts_plugin_registry_load(
                        &registry, PLUGIN_VALID_C_PATH, &ref),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_start(&registry, ref),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_acquire(
                        &registry, ref, &lease, &manifest),
                    SALTS_PLUGIN_OK);
        check_true(salts_plugin_lease_valid(lease));
        check_not_null(manifest);

        check_equal(salts_plugin_manifest_find_export(
                        manifest, "test.loader.math.double", &entry),
                    SALTS_PLUGIN_OK);
        check_not_null(entry);
        check_equal(salts_plugin_export_require_function(
                        entry, "test.loader.math", 1u, 1u),
                    SALTS_PLUGIN_OK);
        check_true(cmeta_function_desc_valid(entry->value.function.desc));
        check_true(cmeta_function_abi_desc_valid(entry->value.function.abi));
        check_true(entry->value.function.abi->function == entry->value.function.desc);

        params[0] = &input;
        check_true(entry->value.function.invoke(
            entry->value.function.context, &output, params, 1u));
        check_equal(output, 18);

        check_equal(salts_plugin_registry_release(&registry, &lease),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_request_stop(&registry, ref),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_poll_quiescent(
                        &registry, ref, &quiescent),
                    SALTS_PLUGIN_OK);
        check_true(quiescent);
        check_equal(salts_plugin_registry_unload(&registry, ref),
                    SALTS_PLUGIN_OK);
        destroy_registry(&registry);
    }

    it("reports stale generation without exposing slot storage") {
        salts_plugin_registry registry = make_registry(1u);
        salts_plugin_ref ref = {0};
        salts_plugin_ref stale;
        salts_plugin_lifecycle_info info = {0};

        check_equal(salts_plugin_registry_load(
                        &registry, PLUGIN_VALID_C_PATH, &ref),
                    SALTS_PLUGIN_OK);
        stale = ref;
        ++stale.generation;
        if (stale.generation == 0u)
            stale.generation = 1u;

        check_equal(salts_plugin_registry_get_lifecycle(
                        &registry, stale, &info),
                    SALTS_PLUGIN_STALE);

        stale = ref;
        stale.slot = UINT32_MAX;
        check_equal(salts_plugin_registry_get_lifecycle(
                        &registry, stale, &info),
                    SALTS_PLUGIN_STALE);

        destroy_registry(&registry);
    }
}

describe("transactional admission") {
    it("runs plugin query without holding the registry lock") {
        salts_plugin_registry registry = make_registry(1u);
        plugin_slow_load_context load = {
            &registry, PLUGIN_SLOW_QUERY_A_PATH,
            SALTS_PLUGIN_INVALID_STATE, {0}
        };
        plugin_destroy_context destroy = {
            &registry, SALTS_PLUGIN_INVALID_STATE
        };
        salts_thread_t load_thread = NULL;
        salts_thread_t destroy_thread = NULL;
        bool destroy_completed;

        atomic_init(&destroy.done, false);
        (void)remove(PLUGIN_SLOW_QUERY_ENTERED_MARKER_A);
        (void)remove(PLUGIN_SLOW_QUERY_RELEASE_MARKER);

        check_equal(salts_thread_create(
                        &load_thread, plugin_slow_load_thread, &load),
                    0);
        check_true(wait_for_marker(
            PLUGIN_SLOW_QUERY_ENTERED_MARKER_A, 5000u));

        check_equal(salts_thread_create(
                        &destroy_thread, plugin_destroy_thread, &destroy),
                    0);

        destroy_completed = wait_for_atomic_true(&destroy.done, 500u);
        touch_marker(PLUGIN_SLOW_QUERY_RELEASE_MARKER);

        check_equal(salts_thread_join(&destroy_thread), 0);
        salts_thread_destroy(&destroy_thread);
        check_equal(salts_thread_join(&load_thread), 0);
        salts_thread_destroy(&load_thread);

        check_true(destroy_completed);
        check_equal(destroy.status, SALTS_PLUGIN_BUSY);
        check_equal(load.status, SALTS_PLUGIN_OK);
        check_true(salts_plugin_ref_valid(load.ref));
        check_equal(salts_plugin_registry_count(&registry), (size_t)1u);

        if (destroy.status == SALTS_PLUGIN_BUSY &&
            salts_plugin_ref_valid(load.ref))
            check_equal(salts_plugin_registry_unload(
                            &registry, load.ref),
                        SALTS_PLUGIN_OK);

        destroy_registry(&registry);
        (void)remove(PLUGIN_SLOW_QUERY_ENTERED_MARKER_A);
        (void)remove(PLUGIN_SLOW_QUERY_RELEASE_MARKER);
    }

    it("publishes at most one concurrent duplicate plugin") {
        salts_plugin_registry registry = make_registry(2u);
        plugin_slow_load_context first = {
            &registry, PLUGIN_SLOW_QUERY_A_PATH,
            SALTS_PLUGIN_INVALID_STATE, {0}
        };
        plugin_slow_load_context second = {
            &registry, PLUGIN_SLOW_QUERY_B_PATH,
            SALTS_PLUGIN_INVALID_STATE, {0}
        };
        salts_thread_t first_thread = NULL;
        salts_thread_t second_thread = NULL;
        salts_plugin_ref published = {0};
        unsigned ok_count = 0u;
        unsigned duplicate_count = 0u;

        (void)remove(PLUGIN_SLOW_QUERY_ENTERED_MARKER_A);
        (void)remove(PLUGIN_SLOW_QUERY_ENTERED_MARKER_B);
        (void)remove(PLUGIN_SLOW_QUERY_RELEASE_MARKER);

        check_equal(salts_thread_create(
                        &first_thread, plugin_slow_load_thread, &first),
                    0);
        check_equal(salts_thread_create(
                        &second_thread, plugin_slow_load_thread, &second),
                    0);

        check_true(wait_for_marker(
            PLUGIN_SLOW_QUERY_ENTERED_MARKER_A, 5000u));
        check_true(wait_for_marker(
            PLUGIN_SLOW_QUERY_ENTERED_MARKER_B, 5000u));

        touch_marker(PLUGIN_SLOW_QUERY_RELEASE_MARKER);

        check_equal(salts_thread_join(&first_thread), 0);
        salts_thread_destroy(&first_thread);
        check_equal(salts_thread_join(&second_thread), 0);
        salts_thread_destroy(&second_thread);

        if (first.status == SALTS_PLUGIN_OK) {
            ++ok_count;
            published = first.ref;
        } else if (first.status == SALTS_PLUGIN_DUPLICATE_PLUGIN_ID) {
            ++duplicate_count;
        }

        if (second.status == SALTS_PLUGIN_OK) {
            ++ok_count;
            published = second.ref;
        } else if (second.status == SALTS_PLUGIN_DUPLICATE_PLUGIN_ID) {
            ++duplicate_count;
        }

        check_equal(ok_count, 1u);
        check_equal(duplicate_count, 1u);
        check_equal(salts_plugin_registry_count(&registry), (size_t)1u);
        check_true(salts_plugin_ref_valid(published));
        check_equal(salts_plugin_registry_unload(&registry, published),
                    SALTS_PLUGIN_OK);

        destroy_registry(&registry);
        (void)remove(PLUGIN_SLOW_QUERY_ENTERED_MARKER_A);
        (void)remove(PLUGIN_SLOW_QUERY_ENTERED_MARKER_B);
        (void)remove(PLUGIN_SLOW_QUERY_RELEASE_MARKER);
    }

    it("rejects duplicate plugin IDs and preserves the published instance") {
        salts_plugin_registry registry = make_registry(2u);
        salts_plugin_ref first = {0};
        salts_plugin_ref duplicate = {9u, 9u};
        salts_plugin_ref found = {0};

        check_equal(salts_plugin_registry_load(
                        &registry, PLUGIN_VALID_C_PATH, &first),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_load(
                        &registry, PLUGIN_VALID_C_PATH, &duplicate),
                    SALTS_PLUGIN_DUPLICATE_PLUGIN_ID);
        check_false(salts_plugin_ref_valid(duplicate));
        check_equal(salts_plugin_registry_count(&registry), (size_t)1u);
        check_equal(salts_plugin_registry_find(
                        &registry, "test.loader.c", &found),
                    SALTS_PLUGIN_OK);
        check_equal(found.slot, first.slot);
        check_equal(found.generation, first.generation);

        destroy_registry(&registry);
    }

    it("rejects capacity before opening another plugin") {
        salts_plugin_registry registry = make_registry(1u);
        salts_plugin_ref first = {0};
        salts_plugin_ref rejected = {7u, 7u};

        check_equal(salts_plugin_registry_load(
                        &registry, PLUGIN_VALID_C_PATH, &first),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_load(
                        &registry, PLUGIN_VALID_CPP_PATH, &rejected),
                    SALTS_PLUGIN_CAPACITY_EXCEEDED);
        check_false(salts_plugin_ref_valid(rejected));
        check_equal(salts_plugin_registry_count(&registry), (size_t)1u);

        destroy_registry(&registry);
    }

    it("keeps missing, rejected, obsolete and incompatible failures distinct") {
        salts_plugin_registry registry = make_registry(2u);
        salts_plugin_ref ref = {3u, 3u};
        const char *missing_file = PLUGIN_VALID_C_PATH ".missing";

        check_equal(salts_plugin_registry_load(
                        &registry, missing_file, &ref),
                    SALTS_PLUGIN_LOAD_FAILED);
        check_false(salts_plugin_ref_valid(ref));
        check_equal(salts_plugin_registry_count(&registry), (size_t)0u);

        ref = (salts_plugin_ref){3u, 3u};
        check_equal(salts_plugin_registry_load(
                        &registry, PLUGIN_MISSING_QUERY_PATH, &ref),
                    SALTS_PLUGIN_QUERY_MISSING);
        check_false(salts_plugin_ref_valid(ref));
        check_equal(salts_plugin_registry_count(&registry), (size_t)0u);

        ref = (salts_plugin_ref){3u, 3u};
        check_equal(salts_plugin_registry_load(
                        &registry, PLUGIN_REJECTED_PATH, &ref),
                    SALTS_PLUGIN_QUERY_REJECTED);
        check_false(salts_plugin_ref_valid(ref));
        check_equal(salts_plugin_registry_count(&registry), (size_t)0u);

        ref = (salts_plugin_ref){3u, 3u};
        check_equal(salts_plugin_registry_load(
                        &registry, PLUGIN_OBSOLETE_PATH, &ref),
                    SALTS_PLUGIN_QUERY_REJECTED);
        check_false(salts_plugin_ref_valid(ref));
        check_equal(salts_plugin_registry_count(&registry), (size_t)0u);

        ref = (salts_plugin_ref){3u, 3u};
        check_equal(salts_plugin_registry_load(
                        &registry, PLUGIN_INVALID_PATH, &ref),
                    SALTS_PLUGIN_UNSUPPORTED_ABI);
        check_false(salts_plugin_ref_valid(ref));
        check_equal(salts_plugin_registry_count(&registry), (size_t)0u);

        destroy_registry(&registry);
    }

    it("admits a complete lifecycle callback group without invoking it") {
        salts_plugin_registry registry = make_registry(1u);
        salts_plugin_ref ref = {0};
        salts_plugin_lifecycle_info info = {0};

        check_equal(salts_plugin_registry_load(
                        &registry, PLUGIN_LIFECYCLE_PATH, &ref),
                    SALTS_PLUGIN_OK);
        check_true(salts_plugin_ref_valid(ref));
        check_equal(salts_plugin_registry_count(&registry), (size_t)1u);
        check_equal(salts_plugin_registry_get_lifecycle(
                        &registry, ref, &info),
                    SALTS_PLUGIN_OK);
        check_equal(info.state, SALTS_PLUGIN_LIFECYCLE_LOADED);
        check_equal(info.active_leases, (size_t)0u);
        check_equal(info.callbacks_inflight, (size_t)0u);

        check_equal(salts_plugin_registry_unload(&registry, ref),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_count(&registry), (size_t)0u);
        destroy_registry(&registry);
    }

    it("rejects malformed UTF-8 paths before platform loading") {
        salts_plugin_registry registry = make_registry(1u);
        salts_plugin_ref ref = {5u, 5u};
        const char invalid_utf8[] = {(char)0xc0, (char)0xaf, '\0'};

        check_equal(salts_plugin_registry_load(
                        &registry, invalid_utf8, &ref),
                    SALTS_PLUGIN_INVALID_ARGUMENT);
        check_false(salts_plugin_ref_valid(ref));
        check_equal(salts_plugin_registry_count(&registry), (size_t)0u);

        destroy_registry(&registry);
    }
}

describe("lookup contract") {
    it("distinguishes bad input from unknown plugin") {
        salts_plugin_registry registry = make_registry(1u);
        salts_plugin_ref ref = {4u, 4u};

        check_equal(salts_plugin_registry_find(
                        &registry, "", &ref),
                    SALTS_PLUGIN_INVALID_ARGUMENT);
        check_false(salts_plugin_ref_valid(ref));

        ref = (salts_plugin_ref){4u, 4u};
        check_equal(salts_plugin_registry_find(
                        &registry, "not.loaded", &ref),
                    SALTS_PLUGIN_UNKNOWN_PLUGIN);
        check_false(salts_plugin_ref_valid(ref));

        destroy_registry(&registry);
    }
}
}
