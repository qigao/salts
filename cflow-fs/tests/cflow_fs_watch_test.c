#include <cflow/fs_watch.h>
#include <turbo/error_codes.h>
#include <turbo/thread.h>
#include <turbo_fs.h>
#include <tinytest.h>

#include <stdlib.h>
#include <string.h>

enum {
    WATCH_TEST_EVENT_CAPACITY = 32,
    WATCH_TEST_PATH_CAPACITY = 256
};

typedef struct watch_probe {
    cflow_fs_watch_event_kind kinds[WATCH_TEST_EVENT_CAPACITY];
    char paths[WATCH_TEST_EVENT_CAPACITY][WATCH_TEST_PATH_CAPACITY];
    char old_paths[WATCH_TEST_EVENT_CAPACITY][WATCH_TEST_PATH_CAPACITY];
    size_t count;
} watch_probe;

static void watch_event(void *user, const cflow_fs_watch_event *event) {
    watch_probe *probe = (watch_probe *)user;
    if (probe != NULL && event != NULL &&
        probe->count < WATCH_TEST_EVENT_CAPACITY) {
        probe->kinds[probe->count] = event->kind;
        if (event->path != NULL) {
            strncpy(probe->paths[probe->count], event->path,
                    WATCH_TEST_PATH_CAPACITY - 1u);
        }
        if (event->old_path != NULL) {
            strncpy(probe->old_paths[probe->count], event->old_path,
                    WATCH_TEST_PATH_CAPACITY - 1u);
        }
        ++probe->count;
    }
}

static cflow_fs_watch_config watch_config(watch_probe *probe,
                                          size_t capacity) {
    cflow_fs_watch_config config = {
        .recursive = false,
        .event_capacity = capacity,
        .watch_capacity = 1u,
        .path_capacity = WATCH_TEST_PATH_CAPACITY,
        .native_buffer_capacity = 4096u,
        .event = watch_event,
        .event_user = probe,
    };
    return config;
}

static int watch_drive_until(cflow_fs_watch *watch, watch_probe *probe,
                             size_t minimum) {
    size_t attempts = 0u;
    while (probe->count < minimum && attempts++ < 5000u) {
        size_t delivered = 0u;
        int status = cflow_fs_watch_run_ready(watch, 8u, &delivered);
        if (status != TURBO_OK)
            return status;
        if (delivered == 0u)
            turbo_sleep_ms(1u);
    }
    return probe->count >= minimum ? TURBO_OK : TURBO_ETIMEDOUT;
}

static bool probe_saw(const watch_probe *probe,
                      cflow_fs_watch_event_kind kind,
                      const char *path) {
    size_t index;
    for (index = 0u; index < probe->count; ++index) {
        if (probe->kinds[index] == kind &&
            (path == NULL || strcmp(probe->paths[index], path) == 0))
            return true;
    }
    return false;
}

static void watch_close_destroy(cflow_fs_watch *watch) {
    size_t attempts = 0u;
    int status = cflow_fs_watch_close(watch);
    check_true(status == TURBO_OK || status == TURBO_EALREADY);
    while (!cflow_fs_watch_is_quiescent(watch) && attempts++ < 5000u) {
        size_t delivered = 0u;
        check_equal(cflow_fs_watch_run_ready(watch, 8u, &delivered), TURBO_OK);
        if (delivered == 0u)
            turbo_sleep_ms(1u);
    }
    check_true(cflow_fs_watch_is_quiescent(watch));
    check_equal(cflow_fs_watch_destroy(watch), TURBO_OK);
    check_null(watch->impl);
}

spec("CFlow filesystem watch") {
    it("declares the bounded watch source contract") {
        cflow_fs_watch watch = {0};
        cflow_fs_watch_config config = {
            .recursive = false,
            .event_capacity = 8u,
            .watch_capacity = 1u,
            .path_capacity = 256u,
            .native_buffer_capacity = 4096u,
            .event = watch_event,
            .event_user = NULL,
        };
        cflow_fs_watch_event event = {
            .kind = CFLOW_FS_WATCH_CREATED,
            .path = "child",
            .old_path = NULL,
            .entry_type = CFLOW_FS_WATCH_ENTRY_UNKNOWN,
        };
        cflow_fs_watch_stats stats = {0};

        check_null(watch.impl);
        check_false(config.recursive);
        check_equal(event.kind, CFLOW_FS_WATCH_CREATED);
        check_equal(stats.queued, (size_t)0u);
        check_true(_Generic(&cflow_fs_watch_open,
            int (*)(cflow_fs_watch *, const char *,
                    const cflow_fs_watch_config *): 1,
            default: 0));
        check_true(_Generic(&cflow_fs_watch_run_ready,
            int (*)(cflow_fs_watch *, size_t, size_t *): 1,
            default: 0));
        check_true(_Generic(&cflow_fs_watch_acknowledge_rescan,
            int (*)(cflow_fs_watch *): 1,
            default: 0));
        check_true(_Generic(&cflow_fs_watch_close,
            int (*)(cflow_fs_watch *): 1,
            default: 0));
        check_true(_Generic(&cflow_fs_watch_is_quiescent,
            bool (*)(const cflow_fs_watch *): 1,
            default: 0));
        check_true(_Generic(&cflow_fs_watch_get_stats,
            bool (*)(const cflow_fs_watch *, cflow_fs_watch_stats *): 1,
            default: 0));
        check_true(_Generic(&cflow_fs_watch_destroy,
            int (*)(cflow_fs_watch *): 1,
            default: 0));
    }

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
    it("reports create rename and remove through the driver") {
        char *root = tt_make_temp_dir("cflow-watch-");
        char first[1024];
        char second[1024];
        cflow_fs_watch watch = {0};
        watch_probe probe = {0};
        cflow_fs_watch_config config = watch_config(&probe, 8u);

        check_not_null(root);
        check_equal(turbo_fs_path_join(first, sizeof(first), root,
                                       "first.txt"), TURBO_OK);
        check_equal(turbo_fs_path_join(second, sizeof(second), root,
                                       "second.txt"), TURBO_OK);
        check_equal(cflow_fs_watch_open(NULL, root, &config), TURBO_EINVAL);
        check_equal(cflow_fs_watch_open(&watch, root, &config), TURBO_OK);
        check_equal(tt_write_file(first, "value", 5u), TURBO_OK);
        check_equal(watch_drive_until(&watch, &probe, 1u), TURBO_OK);
        check_true(probe_saw(&probe, CFLOW_FS_WATCH_CREATED, "first.txt") ||
                   probe_saw(&probe, CFLOW_FS_WATCH_MODIFIED, "first.txt"));

        check_equal(turbo_fs_rename(first, second), TURBO_OK);
        check_equal(watch_drive_until(&watch, &probe, 2u), TURBO_OK);
#if defined(_WIN32) || defined(__linux__)
        {
            size_t attempts = 0u;
            while (!probe_saw(&probe, CFLOW_FS_WATCH_RENAMED, "second.txt") &&
                   attempts++ < 5000u) {
                size_t delivered = 0u;
                check_equal(cflow_fs_watch_run_ready(
                                &watch, 8u, &delivered), TURBO_OK);
                if (delivered == 0u)
                    turbo_sleep_ms(1u);
            }
            check_true(probe_saw(&probe, CFLOW_FS_WATCH_RENAMED, "second.txt"));
        }
#elif defined(__APPLE__)
        {
            size_t attempts = 0u;
            while (!probe_saw(&probe,
                              CFLOW_FS_WATCH_RESCAN_REQUIRED, NULL) &&
                   attempts++ < 5000u) {
                size_t delivered = 0u;
                check_equal(cflow_fs_watch_run_ready(
                                &watch, 8u, &delivered), TURBO_OK);
                if (delivered == 0u)
                    turbo_sleep_ms(1u);
            }
            check_true(probe_saw(&probe,
                                 CFLOW_FS_WATCH_RESCAN_REQUIRED, NULL));
            check_equal(cflow_fs_watch_acknowledge_rescan(&watch), TURBO_OK);
        }
#endif
        check_equal(tt_remove_file(second), TURBO_OK);
        {
            size_t attempts = 0u;
            while (!probe_saw(&probe, CFLOW_FS_WATCH_REMOVED, "second.txt") &&
                   attempts++ < 5000u) {
                size_t delivered = 0u;
                check_equal(cflow_fs_watch_run_ready(
                                &watch, 8u, &delivered), TURBO_OK);
                if (delivered == 0u)
                    turbo_sleep_ms(1u);
            }
            check_true(probe_saw(&probe, CFLOW_FS_WATCH_REMOVED, "second.txt"));
        }

        watch_close_destroy(&watch);
        check_equal(tt_remove_tree(root), TURBO_OK);
        free(root);
    }

    it("turns bounded queue loss into one rescan-required event") {
        char *root = tt_make_temp_dir("cflow-watch-overflow-");
        char first[1024];
        char second[1024];
        cflow_fs_watch watch = {0};
        watch_probe probe = {0};
        cflow_fs_watch_config config = watch_config(&probe, 1u);
        cflow_fs_watch_stats stats = {0};
        size_t attempts = 0u;

        check_not_null(root);
        check_equal(turbo_fs_path_join(first, sizeof(first), root,
                                       "one.txt"), TURBO_OK);
        check_equal(turbo_fs_path_join(second, sizeof(second), root,
                                       "two.txt"), TURBO_OK);
        check_equal(cflow_fs_watch_open(&watch, root, &config), TURBO_OK);
        check_equal(tt_write_file(first, "1", 1u), TURBO_OK);
        check_equal(tt_write_file(second, "2", 1u), TURBO_OK);
        while (attempts++ < 5000u) {
            check_true(cflow_fs_watch_get_stats(&watch, &stats));
            if (stats.awaiting_rescan)
                break;
            turbo_sleep_ms(1u);
        }
        check_true(stats.awaiting_rescan);
        check_equal(watch_drive_until(&watch, &probe, 2u), TURBO_OK);
        check_true(probe_saw(&probe, CFLOW_FS_WATCH_RESCAN_REQUIRED, NULL));
        check_equal(cflow_fs_watch_acknowledge_rescan(&watch), TURBO_OK);
        check_true(cflow_fs_watch_get_stats(&watch, &stats));
        check_false(stats.awaiting_rescan);

        watch_close_destroy(&watch);
        check_equal(tt_remove_tree(root), TURBO_OK);
        free(root);
    }
#else
    it("fails fast when the native watch backend is unavailable") {
        char *root = tt_make_temp_dir("cflow-watch-unsupported-");
        cflow_fs_watch watch = {0};
        watch_probe probe = {0};
        cflow_fs_watch_config config = watch_config(&probe, 1u);

        check_not_null(root);
        check_equal(cflow_fs_watch_open(&watch, root, &config), TURBO_ENOTSUP);
        check_null(watch.impl);
        check_equal(tt_remove_tree(root), TURBO_OK);
        free(root);
    }
#endif

#if defined(_WIN32) || defined(__linux__)
    it("reports recursive descendants with normalized relative paths") {
        char *root = tt_make_temp_dir("cflow-watch-recursive-");
        char nested[1024];
        char child[1024];
        char dynamic[1024];
        char dynamic_child[1024];
        cflow_fs_watch watch = {0};
        watch_probe probe = {0};
        cflow_fs_watch_config config = watch_config(&probe, 8u);
        size_t attempts = 0u;

        config.recursive = true;
        config.watch_capacity = 8u;
        check_not_null(root);
        check_equal(turbo_fs_path_join(nested, sizeof(nested), root,
                                       "nested"), TURBO_OK);
        check_equal(turbo_fs_path_join(child, sizeof(child), nested,
                                       "child.txt"), TURBO_OK);
        check_equal(turbo_fs_path_join(dynamic, sizeof(dynamic), root,
                                       "dynamic"), TURBO_OK);
        check_equal(turbo_fs_path_join(dynamic_child, sizeof(dynamic_child),
                                       dynamic, "later.txt"), TURBO_OK);
        check_equal(tt_make_dir(nested), TURBO_OK);
        check_equal(cflow_fs_watch_open(&watch, root, &config), TURBO_OK);
        check_equal(tt_write_file(child, "x", 1u), TURBO_OK);
        while (!probe_saw(&probe, CFLOW_FS_WATCH_CREATED,
                          "nested/child.txt") && attempts++ < 5000u) {
            size_t delivered = 0u;
            check_equal(cflow_fs_watch_run_ready(
                            &watch, 8u, &delivered), TURBO_OK);
            if (delivered == 0u)
                turbo_sleep_ms(1u);
        }
        check_true(probe_saw(&probe, CFLOW_FS_WATCH_CREATED,
                             "nested/child.txt"));
        check_equal(tt_make_dir(dynamic), TURBO_OK);
        attempts = 0u;
        while (!probe_saw(&probe, CFLOW_FS_WATCH_CREATED, "dynamic") &&
               attempts++ < 5000u) {
            size_t delivered = 0u;
            check_equal(cflow_fs_watch_run_ready(
                            &watch, 8u, &delivered), TURBO_OK);
            if (delivered == 0u)
                turbo_sleep_ms(1u);
        }
        check_true(probe_saw(&probe, CFLOW_FS_WATCH_CREATED, "dynamic"));
        check_equal(tt_write_file(dynamic_child, "y", 1u), TURBO_OK);
        attempts = 0u;
        while (!probe_saw(&probe, CFLOW_FS_WATCH_CREATED,
                          "dynamic/later.txt") && attempts++ < 5000u) {
            size_t delivered = 0u;
            check_equal(cflow_fs_watch_run_ready(
                            &watch, 8u, &delivered), TURBO_OK);
            if (delivered == 0u)
                turbo_sleep_ms(1u);
        }
        check_true(probe_saw(&probe, CFLOW_FS_WATCH_CREATED,
                             "dynamic/later.txt"));
        watch_close_destroy(&watch);
        check_equal(tt_remove_tree(root), TURBO_OK);
        free(root);
    }
#endif
}
