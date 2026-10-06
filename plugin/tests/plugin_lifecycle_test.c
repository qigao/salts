#include <salts/plugin.h>
#include <salts/thread.h>
#include <tinytest.h>

#include "plugin_lifecycle_test_interface.h"

#include <stdatomic.h>
#include <string.h>

#ifndef PLUGIN_LIFECYCLE_PATH
#error "PLUGIN_LIFECYCLE_PATH is required"
#endif
#ifndef PLUGIN_PARTIAL_LIFECYCLE_PATH
#error "PLUGIN_PARTIAL_LIFECYCLE_PATH is required"
#endif
#ifndef PLUGIN_START_FAIL_PATH
#error "PLUGIN_START_FAIL_PATH is required"
#endif
#ifndef PLUGIN_STOP_FAIL_PATH
#error "PLUGIN_STOP_FAIL_PATH is required"
#endif
#ifndef PLUGIN_SLOW_STOP_PATH
#error "PLUGIN_SLOW_STOP_PATH is required"
#endif

typedef struct lifecycle_worker {
    cmeta_plugin_registry *registry;
    cmeta_plugin_ref ref;
    atomic_uint successful;
    atomic_int unexpected;
} lifecycle_worker;

typedef struct stop_worker {
    cmeta_plugin_registry *registry;
    cmeta_plugin_ref ref;
    atomic_int status;
} stop_worker;

static cmeta_plugin_registry make_registry(size_t capacity) {
    cmeta_plugin_registry registry = {0};
    cmeta_plugin_registry_config config = {capacity};

    check_equal(cmeta_plugin_registry_init(&registry, &config),
                SALTS_PLUGIN_OK);
    check_not_null(registry.impl);
    return registry;
}

static void destroy_registry(cmeta_plugin_registry *registry) {
    check_equal(cmeta_plugin_registry_destroy(registry),
                SALTS_PLUGIN_OK);
    check_null(registry->impl);
}

static cmeta_plugin_status lifecycle_info(
    cmeta_plugin_registry *registry,
    cmeta_plugin_ref ref,
    cmeta_plugin_lifecycle_info *out) {
    return cmeta_plugin_registry_get_lifecycle(registry, ref, out);
}

static void lifecycle_send_worker(void *arg) {
    lifecycle_worker *worker = (lifecycle_worker *)arg;
    unsigned index;

    for (index = 0u; index < 1000000u; ++index) {
        cmeta_plugin_lease lease = {0};
        const cmeta_plugin_manifest *manifest = NULL;
        const cmeta_plugin_export *entry = NULL;
        cmeta_plugin_status status = cmeta_plugin_registry_acquire(
            worker->registry, worker->ref, &lease, &manifest);

        if (status == SALTS_PLUGIN_INVALID_STATE ||
            status == SALTS_PLUGIN_BUSY ||
            status == SALTS_PLUGIN_STALE)
            return;
        if (status != SALTS_PLUGIN_OK) {
            atomic_store(&worker->unexpected, (int)status);
            return;
        }

        status = cmeta_plugin_manifest_find_export(
            manifest, "service", &entry);
        if (status != SALTS_PLUGIN_OK || entry == NULL ||
            entry->kind != SALTS_PLUGIN_EXPORT_INTERFACE) {
            atomic_store(&worker->unexpected,
                         status == SALTS_PLUGIN_OK
                             ? (int)SALTS_PLUGIN_INCOMPATIBLE_CONTRACT
                             : (int)status);
            (void)cmeta_plugin_registry_release(
                worker->registry, &lease);
            return;
        }

        {
            plugin_lifecycle_test_api *api =
                (plugin_lifecycle_test_api *)entry->value.interface.value;
            if (!plugin_lifecycle_test_api_valid(api)) {
                atomic_store(&worker->unexpected,
                             (int)SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);
                (void)cmeta_plugin_registry_release(
                    worker->registry, &lease);
                return;
            }
            (void)plugin_lifecycle_test_api_send(api, (int)index);
        }

        status = cmeta_plugin_registry_release(
            worker->registry, &lease);
        if (status != SALTS_PLUGIN_OK) {
            atomic_store(&worker->unexpected, (int)status);
            return;
        }
        atomic_fetch_add(&worker->successful, 1u);
        cmeta_thread_yield();
    }

    /* The worker is expected to terminate because stop closes admission. */
    atomic_store(&worker->unexpected, (int)SALTS_PLUGIN_BUSY);
}

static void lifecycle_stop_worker(void *arg) {
    stop_worker *worker = (stop_worker *)arg;
    cmeta_plugin_status status = cmeta_plugin_registry_request_stop(
        worker->registry, worker->ref);
    atomic_store(&worker->status, (int)status);
}

spec("Salts Plugin lifecycle and quiescent unload") {
describe("lease-owned DSO access") {
    it("closes admission on stop and unloads only after every lease returns") {
        cmeta_plugin_registry registry = make_registry(1u);
        cmeta_plugin_ref ref = {0};
        cmeta_plugin_ref reloaded = {0};
        cmeta_plugin_lease lease = {0};
        cmeta_plugin_lease rejected = {9u, 9u, 9u};
        const cmeta_plugin_manifest *manifest = NULL;
        const cmeta_plugin_export *entry = NULL;
        cmeta_plugin_lifecycle_info info = {0};
        bool quiescent = true;

        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_LIFECYCLE_PATH, &ref),
                    SALTS_PLUGIN_OK);
        check_equal(lifecycle_info(&registry, ref, &info),
                    SALTS_PLUGIN_OK);
        check_equal(info.state, SALTS_PLUGIN_LIFECYCLE_LOADED);

        check_equal(cmeta_plugin_registry_acquire(
                        &registry, ref, &lease, &manifest),
                    SALTS_PLUGIN_INVALID_STATE);
        check_false(cmeta_plugin_lease_valid(lease));
        check_null(manifest);

        check_equal(cmeta_plugin_registry_start(&registry, ref),
                    SALTS_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_start(&registry, ref),
                    SALTS_PLUGIN_ALREADY);

        check_equal(cmeta_plugin_registry_acquire(
                        &registry, ref, &lease, &manifest),
                    SALTS_PLUGIN_OK);
        check_true(cmeta_plugin_lease_valid(lease));
        check_not_null(manifest);
        check_equal(cmeta_plugin_manifest_find_export(
                        manifest, "service", &entry),
                    SALTS_PLUGIN_OK);
        check_not_null(entry);
        {
            plugin_lifecycle_test_api *api =
                (plugin_lifecycle_test_api *)entry->value.interface.value;
            check_true(plugin_lifecycle_test_api_valid(api));
            check_true(plugin_lifecycle_test_api_send(api, 7));
            check_equal(plugin_lifecycle_test_api_accepted(api),
                        (uint64_t)1u);
        }

        check_equal(cmeta_plugin_registry_request_stop(
                        &registry, ref),
                    SALTS_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_request_stop(
                        &registry, ref),
                    SALTS_PLUGIN_ALREADY);

        manifest = (const cmeta_plugin_manifest *)(uintptr_t)1u;
        check_equal(cmeta_plugin_registry_acquire(
                        &registry, ref, &rejected, &manifest),
                    SALTS_PLUGIN_INVALID_STATE);
        check_false(cmeta_plugin_lease_valid(rejected));
        check_null(manifest);

        check_equal(cmeta_plugin_registry_unload(&registry, ref),
                    SALTS_PLUGIN_BUSY);
        check_equal(cmeta_plugin_registry_poll_quiescent(
                        &registry, ref, &quiescent),
                    SALTS_PLUGIN_OK);
        check_false(quiescent);

        check_equal(cmeta_plugin_registry_release(
                        &registry, &lease),
                    SALTS_PLUGIN_OK);
        check_false(cmeta_plugin_lease_valid(lease));

        check_equal(cmeta_plugin_registry_poll_quiescent(
                        &registry, ref, &quiescent),
                    SALTS_PLUGIN_OK);
        check_true(quiescent);
        check_equal(lifecycle_info(&registry, ref, &info),
                    SALTS_PLUGIN_OK);
        check_equal(info.state, SALTS_PLUGIN_LIFECYCLE_QUIESCENT);
        check_equal(info.active_leases, (size_t)0u);

        check_equal(cmeta_plugin_registry_unload(&registry, ref),
                    SALTS_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_count(&registry), (size_t)0u);
        check_equal(cmeta_plugin_registry_unload(&registry, ref),
                    SALTS_PLUGIN_STALE);
        check_equal(lifecycle_info(&registry, ref, &info),
                    SALTS_PLUGIN_STALE);

        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_LIFECYCLE_PATH, &reloaded),
                    SALTS_PLUGIN_OK);
        check_equal(reloaded.slot, ref.slot);
        check_true(reloaded.generation != ref.generation);
        check_equal(lifecycle_info(&registry, ref, &info),
                    SALTS_PLUGIN_STALE);
        check_equal(cmeta_plugin_registry_unload(
                        &registry, reloaded),
                    SALTS_PLUGIN_OK);

        destroy_registry(&registry);
    }

    it("leaves the lease unchanged when release fails before a successful retry") {
        cmeta_plugin_registry registry = make_registry(1u);
        cmeta_plugin_registry wrong_registry = make_registry(1u);
        cmeta_plugin_ref ref = {0};
        cmeta_plugin_lease lease = {0};
        cmeta_plugin_lease saved = {0};
        const cmeta_plugin_manifest *manifest = NULL;
        cmeta_plugin_lifecycle_info info = {0};
        bool quiescent = false;

        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_LIFECYCLE_PATH, &ref),
                    SALTS_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_start(&registry, ref),
                    SALTS_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_acquire(
                        &registry, ref, &lease, &manifest),
                    SALTS_PLUGIN_OK);
        check_true(cmeta_plugin_lease_valid(lease));
        saved = lease;

        check_equal(cmeta_plugin_registry_release(
                        &wrong_registry, &lease),
                    SALTS_PLUGIN_STALE);
        check_equal(memcmp(&lease, &saved, sizeof(lease)), 0);
        check_equal(lifecycle_info(&registry, ref, &info),
                    SALTS_PLUGIN_OK);
        check_equal(info.active_leases, (size_t)1u);

        check_equal(cmeta_plugin_registry_release(
                        &registry, &lease),
                    SALTS_PLUGIN_OK);
        check_false(cmeta_plugin_lease_valid(lease));
        check_equal(lifecycle_info(&registry, ref, &info),
                    SALTS_PLUGIN_OK);
        check_equal(info.active_leases, (size_t)0u);

        check_equal(cmeta_plugin_registry_request_stop(
                        &registry, ref),
                    SALTS_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_poll_quiescent(
                        &registry, ref, &quiescent),
                    SALTS_PLUGIN_OK);
        check_true(quiescent);
        check_equal(cmeta_plugin_registry_unload(&registry, ref),
                    SALTS_PLUGIN_OK);
        destroy_registry(&registry);
        destroy_registry(&wrong_registry);
    }
}

describe("lifecycle failures") {
    it("rejects partial lifecycle callback groups during manifest admission") {
        cmeta_plugin_registry registry = make_registry(1u);
        cmeta_plugin_ref ref = {4u, 4u};

        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_PARTIAL_LIFECYCLE_PATH, &ref),
                    SALTS_PLUGIN_INVALID_MANIFEST);
        check_false(cmeta_plugin_ref_valid(ref));
        check_equal(cmeta_plugin_registry_count(&registry), (size_t)0u);

        destroy_registry(&registry);
    }

    it("keeps failed start failure-atomic and unloadable") {
        cmeta_plugin_registry registry = make_registry(1u);
        cmeta_plugin_ref ref = {0};
        cmeta_plugin_lifecycle_info info = {0};

        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_START_FAIL_PATH, &ref),
                    SALTS_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_start(&registry, ref),
                    SALTS_PLUGIN_BUSY);
        check_equal(lifecycle_info(&registry, ref, &info),
                    SALTS_PLUGIN_OK);
        check_equal(info.state, SALTS_PLUGIN_LIFECYCLE_QUIESCENT);
        check_equal(info.failure, SALTS_PLUGIN_BUSY);
        check_equal(info.active_leases, (size_t)0u);
        check_equal(info.callbacks_inflight, (size_t)0u);

        check_equal(cmeta_plugin_registry_unload(&registry, ref),
                    SALTS_PLUGIN_OK);
        destroy_registry(&registry);
    }

    it("records stop failure but still permits quiescence and safe unload") {
        cmeta_plugin_registry registry = make_registry(1u);
        cmeta_plugin_ref ref = {0};
        cmeta_plugin_lifecycle_info info = {0};
        bool quiescent = false;

        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_STOP_FAIL_PATH, &ref),
                    SALTS_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_start(&registry, ref),
                    SALTS_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_request_stop(
                        &registry, ref),
                    SALTS_PLUGIN_BUSY);
        check_equal(lifecycle_info(&registry, ref, &info),
                    SALTS_PLUGIN_OK);
        check_equal(info.state, SALTS_PLUGIN_LIFECYCLE_STOPPING);
        check_equal(info.failure, SALTS_PLUGIN_BUSY);

        check_equal(cmeta_plugin_registry_poll_quiescent(
                        &registry, ref, &quiescent),
                    SALTS_PLUGIN_OK);
        check_true(quiescent);
        check_equal(lifecycle_info(&registry, ref, &info),
                    SALTS_PLUGIN_OK);
        check_equal(info.state, SALTS_PLUGIN_LIFECYCLE_QUIESCENT);
        check_equal(info.failure, SALTS_PLUGIN_BUSY);

        check_equal(cmeta_plugin_registry_unload(&registry, ref),
                    SALTS_PLUGIN_OK);
        destroy_registry(&registry);
    }
}

describe("concurrent stop boundaries") {
    it("rejects unload while a lifecycle callback is in flight") {
        cmeta_plugin_registry registry = make_registry(1u);
        cmeta_plugin_ref ref = {0};
        stop_worker worker;
        cmeta_thread_t thread = NULL;
        cmeta_plugin_lifecycle_info info = {0};
        bool observed_inflight = false;
        bool quiescent = false;
        unsigned attempt;

        memset(&worker, 0, sizeof(worker));
        worker.registry = &registry;

        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_SLOW_STOP_PATH, &ref),
                    SALTS_PLUGIN_OK);
        worker.ref = ref;
        check_equal(cmeta_plugin_registry_start(&registry, ref),
                    SALTS_PLUGIN_OK);

        atomic_init(&worker.status, -1);
        check_equal(cmeta_thread_create(
                        &thread, lifecycle_stop_worker, &worker), 0);
        check_not_null(thread);

        for (attempt = 0u; attempt < 1000u; ++attempt) {
            check_equal(lifecycle_info(&registry, ref, &info),
                        SALTS_PLUGIN_OK);
            if (info.callbacks_inflight != 0u) {
                observed_inflight = true;
                break;
            }
            cmeta_sleep_ms(1u);
        }
        check_true(observed_inflight);
        check_equal(info.state, SALTS_PLUGIN_LIFECYCLE_STOPPING);
        check_equal(cmeta_plugin_registry_unload(&registry, ref),
                    SALTS_PLUGIN_BUSY);

        check_equal(cmeta_thread_join(&thread), 0);
        cmeta_thread_destroy(&thread);
        check_equal(atomic_load(&worker.status), (int)SALTS_PLUGIN_OK);

        check_equal(cmeta_plugin_registry_poll_quiescent(
                        &registry, ref, &quiescent),
                    SALTS_PLUGIN_OK);
        check_true(quiescent);
        check_equal(cmeta_plugin_registry_unload(&registry, ref),
                    SALTS_PLUGIN_OK);
        destroy_registry(&registry);
    }

    it("serializes acquire/send against stop without use-after-unload") {
        cmeta_plugin_registry registry = make_registry(1u);
        cmeta_plugin_ref ref = {0};
        lifecycle_worker worker;
        cmeta_thread_t thread = NULL;
        bool quiescent = false;
        unsigned attempt;

        memset(&worker, 0, sizeof(worker));
        worker.registry = &registry;

        check_equal(cmeta_plugin_registry_load(
                        &registry, PLUGIN_LIFECYCLE_PATH, &ref),
                    SALTS_PLUGIN_OK);
        worker.ref = ref;
        check_equal(cmeta_plugin_registry_start(&registry, ref),
                    SALTS_PLUGIN_OK);

        atomic_init(&worker.successful, 0u);
        atomic_init(&worker.unexpected, 0);
        check_equal(cmeta_thread_create(
                        &thread, lifecycle_send_worker, &worker), 0);
        check_not_null(thread);

        for (attempt = 0u; attempt < 1000u; ++attempt) {
            if (atomic_load(&worker.successful) >= 32u)
                break;
            cmeta_sleep_ms(1u);
        }
        check_true(atomic_load(&worker.successful) != 0u);

        check_equal(cmeta_plugin_registry_request_stop(
                        &registry, ref),
                    SALTS_PLUGIN_OK);
        check_equal(cmeta_thread_join(&thread), 0);
        cmeta_thread_destroy(&thread);
        check_equal(atomic_load(&worker.unexpected), 0);

        for (attempt = 0u; attempt < 1000u; ++attempt) {
            check_equal(cmeta_plugin_registry_poll_quiescent(
                            &registry, ref, &quiescent),
                        SALTS_PLUGIN_OK);
            if (quiescent)
                break;
            cmeta_sleep_ms(1u);
        }
        check_true(quiescent);
        check_equal(cmeta_plugin_registry_unload(&registry, ref),
                    SALTS_PLUGIN_OK);

        destroy_registry(&registry);
    }
}
}
