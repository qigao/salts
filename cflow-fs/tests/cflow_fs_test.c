#include <cflow/fs.h>
#include <turbo/error_codes.h>
#include <turbo/thread.h>
#include <tinytest.h>

#include <stdlib.h>
#include <string.h>

enum { FS_TEST_COMPLETION_CAPACITY = 32 };

typedef struct fs_completion_probe {
    uint64_t request_ids[FS_TEST_COMPLETION_CAPACITY];
    cflow_fs_operation_kind operations[FS_TEST_COMPLETION_CAPACITY];
    int results[FS_TEST_COMPLETION_CAPACITY];
    size_t count;
    cflow_fs_service *reentrant_service;
    int reentrant_status;
} fs_completion_probe;

static void contract_completion(void *user, uint64_t request_id,
                                cflow_fs_operation_kind operation,
                                int result) {
    fs_completion_probe *probe = (fs_completion_probe *)user;
    if (probe != NULL && probe->count < FS_TEST_COMPLETION_CAPACITY) {
        probe->request_ids[probe->count] = request_id;
        probe->operations[probe->count] = operation;
        probe->results[probe->count] = result;
        ++probe->count;
    }
    if (probe != NULL && probe->reentrant_service != NULL) {
        cflow_fs_service *service = probe->reentrant_service;
        size_t completed = 0u;
        probe->reentrant_service = NULL;
        probe->reentrant_status = cflow_fs_run_ready(
            service, 1u, &completed);
    }
}

static cflow_fs_config fs_test_config(fs_completion_probe *probe,
                                      size_t capacity) {
    cflow_fs_config config = {
        .worker_count = 1u,
        .request_capacity = capacity,
        .path_capacity = 1024u,
        .completion = contract_completion,
        .completion_user = probe,
    };
    return config;
}

static int fs_wait(cflow_fs_service *service, fs_completion_probe *probe,
                   size_t expected) {
    size_t attempts = 0u;
    while (probe->count < expected && attempts++ < 5000u) {
        size_t completed = 0u;
        int status = cflow_fs_run_ready(service, 8u, &completed);
        if (status != TURBO_OK)
            return status;
        if (completed == 0u)
            turbo_sleep_ms(1u);
    }
    return probe->count >= expected ? TURBO_OK : TURBO_ETIMEDOUT;
}

static void fs_close_destroy(cflow_fs_service *service,
                             fs_completion_probe *probe) {
    size_t attempts = 0u;
    int close_status = cflow_fs_close(service);
    check_true(close_status == TURBO_OK || close_status == TURBO_EALREADY);
    while (!cflow_fs_is_quiescent(service) && attempts++ < 5000u) {
        size_t completed = 0u;
        check_equal(cflow_fs_run_ready(service, 8u, &completed), TURBO_OK);
        if (completed == 0u)
            turbo_sleep_ms(1u);
    }
    check_true(cflow_fs_is_quiescent(service));
    check_equal(cflow_fs_destroy(service), TURBO_OK);
    check_null(service->impl);
    (void)probe;
}

spec("CFlow filesystem service") {
describe("public contract") {
    it("declares the bounded filesystem service surface") {
        cflow_fs_service service = {0};
        cflow_fs_config config = {
            .worker_count = 1u,
            .request_capacity = 2u,
            .path_capacity = 64u,
            .completion = contract_completion,
            .completion_user = NULL,
        };
        cflow_fs_dir_buffer directory = {0};
        cflow_fs_stats stats = {0};
        cflow_fs_submit_result submission = {
            .status = CFLOW_FS_SUBMIT_ACCEPTED,
            .request_id = 1u,
        };
        cflow_fs_cancel_status cancellation = CFLOW_FS_CANCEL_REQUESTED;

        check_equal(config.worker_count, (size_t)1u);
        check_equal(directory.entry_count, (size_t)0u);
        check_equal(stats.accepted, (size_t)0u);
        check_equal(submission.request_id, (uint64_t)1u);
        check_equal(cancellation, CFLOW_FS_CANCEL_REQUESTED);
        check_null(service.impl);

        check_true(_Generic(&cflow_fs_service_init,
            int (*)(cflow_fs_service *, const cflow_fs_config *): 1,
            default: 0));
        check_true(_Generic(&cflow_fs_try_stat,
            cflow_fs_submit_result (*)(cflow_fs_service *, const char *,
                                       turbo_fs_stat_t *): 1,
            default: 0));
        check_true(_Generic(&cflow_fs_try_lstat,
            cflow_fs_submit_result (*)(cflow_fs_service *, const char *,
                                       turbo_fs_stat_t *): 1,
            default: 0));
        check_true(_Generic(&cflow_fs_try_read_directory,
            cflow_fs_submit_result (*)(cflow_fs_service *, const char *,
                                       cflow_fs_dir_buffer *): 1,
            default: 0));
        check_true(_Generic(&cflow_fs_try_mkdir,
            cflow_fs_submit_result (*)(cflow_fs_service *, const char *, int): 1,
            default: 0));
        check_true(_Generic(&cflow_fs_try_rmdir,
            cflow_fs_submit_result (*)(cflow_fs_service *, const char *): 1,
            default: 0));
        check_true(_Generic(&cflow_fs_try_rename,
            cflow_fs_submit_result (*)(cflow_fs_service *, const char *,
                                       const char *): 1,
            default: 0));
        check_true(_Generic(&cflow_fs_try_unlink,
            cflow_fs_submit_result (*)(cflow_fs_service *, const char *): 1,
            default: 0));
        check_true(_Generic(&cflow_fs_try_cancel,
            cflow_fs_cancel_status (*)(cflow_fs_service *, uint64_t): 1,
            default: 0));
        check_true(_Generic(&cflow_fs_run_ready,
            int (*)(cflow_fs_service *, size_t, size_t *): 1,
            default: 0));
        check_true(_Generic(&cflow_fs_close,
            int (*)(cflow_fs_service *): 1,
            default: 0));
        check_true(_Generic(&cflow_fs_is_quiescent,
            bool (*)(const cflow_fs_service *): 1,
            default: 0));
        check_true(_Generic(&cflow_fs_get_stats,
            bool (*)(const cflow_fs_service *, cflow_fs_stats *): 1,
            default: 0));
        check_true(_Generic(&cflow_fs_destroy,
            int (*)(cflow_fs_service *): 1,
            default: 0));
    }
}

describe("lifecycle and metadata") {
    it("validates configuration and asynchronously stats copied paths") {
        char *path = tt_make_temp_file("cflow-fs-stat-", ".txt");
        cflow_fs_service service = {0};
        fs_completion_probe probe = {0};
        cflow_fs_config config = fs_test_config(&probe, 2u);
        turbo_fs_stat_t stat_result = {0};
        cflow_fs_submit_result submitted;
        char original_path[1024];

        check_not_null(path);
        check_less(strlen(path), sizeof(original_path));
        memcpy(original_path, path, strlen(path) + 1u);
        check_equal(tt_write_file(path, "value", 5u), 0);
        check_equal(cflow_fs_service_init(NULL, &config), TURBO_EINVAL);
        check_equal(cflow_fs_service_init(&service, NULL), TURBO_EINVAL);
        config.worker_count = 0u;
        check_equal(cflow_fs_service_init(&service, &config), TURBO_EINVAL);
        config = fs_test_config(&probe, 2u);
        check_equal(cflow_fs_service_init(&service, &config), TURBO_OK);
        check_equal(cflow_fs_destroy(&service), TURBO_EBUSY);

        submitted = cflow_fs_try_stat(&service, path, &stat_result);
        check_equal(submitted.status, CFLOW_FS_SUBMIT_ACCEPTED);
        memset(path, 'x', strlen(path));
        check_equal(fs_wait(&service, &probe, 1u), TURBO_OK);
        check_equal(probe.operations[0], CFLOW_FS_STAT);
        check_equal(probe.results[0], TURBO_OK);
        check_equal(stat_result.size, (uint64_t)5u);
        check_true(stat_result.is_file);

        fs_close_destroy(&service, &probe);
        check_equal(tt_remove_file(original_path), 0);
        free(path);
    }

    it("rejects over-capacity paths before admission") {
        cflow_fs_service service = {0};
        fs_completion_probe probe = {0};
        cflow_fs_config config = fs_test_config(&probe, 1u);
        turbo_fs_stat_t stat_result = {0};
        char path[4] = {'a', 'b', 'c', '\0'};

        config.path_capacity = sizeof(path);
        check_equal(cflow_fs_service_init(&service, &config), TURBO_OK);
        check_equal(cflow_fs_try_stat(&service, path, &stat_result).status,
                    CFLOW_FS_SUBMIT_ACCEPTED);
        check_equal(fs_wait(&service, &probe, 1u), TURBO_OK);
        path[3] = 'd';
        check_equal(cflow_fs_try_stat(&service, path, &stat_result).status,
                    CFLOW_FS_SUBMIT_INVALID_ARGUMENT);
        fs_close_destroy(&service, &probe);
    }

    it("settles an accepted close race exactly once") {
        char *path = tt_make_temp_file("cflow-fs-close-", ".txt");
        cflow_fs_service service = {0};
        fs_completion_probe probe = {0};
        cflow_fs_config config = fs_test_config(&probe, 1u);
        turbo_fs_stat_t stat_result = {0};
        cflow_fs_submit_result submitted;
        size_t attempts = 0u;

        check_not_null(path);
        check_equal(cflow_fs_service_init(&service, &config), TURBO_OK);
        submitted = cflow_fs_try_stat(&service, path, &stat_result);
        check_equal(submitted.status, CFLOW_FS_SUBMIT_ACCEPTED);
        check_equal(cflow_fs_close(&service), TURBO_OK);
        while (!cflow_fs_is_quiescent(&service) && attempts++ < 5000u) {
            size_t completed = 0u;
            check_equal(cflow_fs_run_ready(&service, 1u, &completed), TURBO_OK);
            if (completed == 0u)
                turbo_sleep_ms(1u);
        }
        check_equal(probe.count, (size_t)1u);
        check_equal(probe.request_ids[0], submitted.request_id);
        check_true(probe.results[0] == TURBO_OK ||
                   probe.results[0] == TURBO_ECANCELED);
        check_true(cflow_fs_is_quiescent(&service));
        check_equal(cflow_fs_destroy(&service), TURBO_OK);
        check_equal(tt_remove_file(path), 0);
        free(path);
    }

    it("keeps capacity occupied until the driver delivers completion") {
        char *path = tt_make_temp_file("cflow-fs-capacity-", ".txt");
        cflow_fs_service service = {0};
        fs_completion_probe probe = {0};
        cflow_fs_config config = fs_test_config(&probe, 1u);
        turbo_fs_stat_t first = {0};
        turbo_fs_stat_t second = {0};
        cflow_fs_submit_result submitted;
        cflow_fs_stats stats = {0};

        check_not_null(path);
        check_equal(cflow_fs_service_init(&service, &config), TURBO_OK);
        submitted = cflow_fs_try_stat(&service, path, &first);
        check_equal(submitted.status, CFLOW_FS_SUBMIT_ACCEPTED);
        probe.reentrant_service = &service;
        submitted = cflow_fs_try_lstat(&service, path, &second);
        check_equal(submitted.status, CFLOW_FS_SUBMIT_FULL);
        check_equal(fs_wait(&service, &probe, 1u), TURBO_OK);
        check_equal(probe.reentrant_status, TURBO_EBUSY);
        submitted = cflow_fs_try_lstat(&service, path, &second);
        check_equal(submitted.status, CFLOW_FS_SUBMIT_ACCEPTED);
        check_equal(fs_wait(&service, &probe, 2u), TURBO_OK);
        check_equal(probe.operations[1], CFLOW_FS_LSTAT);
        check_equal(probe.results[1], TURBO_OK);
        check_true(cflow_fs_get_stats(&service, &stats));
        check_equal(stats.capacity, (size_t)1u);
        check_equal(stats.accepted, (size_t)2u);
        check_equal(stats.completed, (size_t)2u);
        check_equal(stats.in_use, (size_t)0u);
        check_equal(stats.rejected_full, (size_t)1u);

        check_equal(cflow_fs_close(&service), TURBO_OK);
        check_equal(cflow_fs_close(&service), TURBO_EALREADY);
        submitted = cflow_fs_try_stat(&service, path, &first);
        check_equal(submitted.status, CFLOW_FS_SUBMIT_CLOSED);

        fs_close_destroy(&service, &probe);
        check_equal(tt_remove_file(path), 0);
        free(path);
    }
}

describe("directory and path mutations") {
    it("enumerates into caller-owned bounded storage transactionally") {
        char *root = tt_make_temp_dir("cflow-fs-dir-");
        char child[1024];
        char nested[1024];
        turbo_fs_dirent_t entries[4] = {0};
        char names[128] = {0};
        cflow_fs_dir_buffer directory = {
            .entries = entries,
            .entry_capacity = 4u,
            .names = names,
            .names_capacity = sizeof(names),
        };
        cflow_fs_service service = {0};
        fs_completion_probe probe = {0};
        cflow_fs_config config = fs_test_config(&probe, 2u);
        bool saw_child = false;
        bool saw_nested = false;
        size_t index;

        check_not_null(root);
        check_equal(turbo_fs_path_join(child, sizeof(child), root,
                                       "child.txt"), 0);
        check_equal(turbo_fs_path_join(nested, sizeof(nested), root,
                                       "nested"), 0);
        check_equal(tt_write_file(child, "x", 1u), 0);
        check_equal(tt_make_dir(nested), 0);
        check_equal(cflow_fs_service_init(&service, &config), TURBO_OK);
        check_equal(cflow_fs_try_read_directory(&service, root, &directory).status,
                    CFLOW_FS_SUBMIT_ACCEPTED);
        check_equal(fs_wait(&service, &probe, 1u), TURBO_OK);
        check_equal(probe.results[0], TURBO_OK);
        check_equal(directory.entry_count, (size_t)2u);
        for (index = 0u; index < directory.entry_count; ++index) {
            if (strcmp(directory.entries[index].name, "child.txt") == 0)
                saw_child = true;
            if (strcmp(directory.entries[index].name, "nested") == 0)
                saw_nested = true;
        }
        check_true(saw_child);
        check_true(saw_nested);

        directory.names_capacity = 1u;
        directory.entry_count = 99u;
        directory.names_used = 99u;
        check_equal(cflow_fs_try_read_directory(&service, root, &directory).status,
                    CFLOW_FS_SUBMIT_ACCEPTED);
        check_equal(fs_wait(&service, &probe, 2u), TURBO_OK);
        check_equal(probe.results[1], TURBO_ENOBUFS);
        check_equal(directory.entry_count, (size_t)0u);
        check_equal(directory.names_used, (size_t)0u);

        fs_close_destroy(&service, &probe);
        check_equal(tt_remove_tree(root), 0);
        free(root);
    }

    it("creates renames deletes and removes filesystem objects") {
        char *root = tt_make_temp_dir("cflow-fs-mutate-");
        char directory[1024];
        char source[1024];
        char destination[1024];
        cflow_fs_service service = {0};
        fs_completion_probe probe = {0};
        cflow_fs_config config = fs_test_config(&probe, 2u);
        turbo_fs_stat_t stat_result = {0};

        check_not_null(root);
        check_equal(turbo_fs_path_join(directory, sizeof(directory), root,
                                       "created"), 0);
        check_equal(turbo_fs_path_join(source, sizeof(source), root,
                                       "source.txt"), 0);
        check_equal(turbo_fs_path_join(destination, sizeof(destination), root,
                                       "destination.txt"), 0);
        check_equal(cflow_fs_service_init(&service, &config), TURBO_OK);

        check_equal(cflow_fs_try_mkdir(&service, directory, 0755).status,
                    CFLOW_FS_SUBMIT_ACCEPTED);
        check_equal(fs_wait(&service, &probe, 1u), TURBO_OK);
        check_equal(probe.results[0], TURBO_OK);
        check_equal(turbo_fs_stat(directory, &stat_result), TURBO_OK);
        check_true(stat_result.is_directory);

        check_equal(tt_write_file(source, "move", 4u), 0);
        check_equal(cflow_fs_try_rename(&service, source, destination).status,
                    CFLOW_FS_SUBMIT_ACCEPTED);
        check_equal(fs_wait(&service, &probe, 2u), TURBO_OK);
        check_equal(probe.results[1], TURBO_OK);
        check_less(turbo_fs_stat(source, &stat_result), 0);
        check_equal(turbo_fs_stat(destination, &stat_result), TURBO_OK);

        check_equal(cflow_fs_try_unlink(&service, destination).status,
                    CFLOW_FS_SUBMIT_ACCEPTED);
        check_equal(fs_wait(&service, &probe, 3u), TURBO_OK);
        check_equal(probe.results[2], TURBO_OK);
        check_equal(cflow_fs_try_rmdir(&service, directory).status,
                    CFLOW_FS_SUBMIT_ACCEPTED);
        check_equal(fs_wait(&service, &probe, 4u), TURBO_OK);
        check_equal(probe.results[3], TURBO_OK);

        fs_close_destroy(&service, &probe);
        check_equal(tt_remove_tree(root), 0);
        free(root);
    }
}
}
