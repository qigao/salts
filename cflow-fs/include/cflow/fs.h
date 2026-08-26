#ifndef CFLOW_FS_H
#define CFLOW_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <turbo_fs.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cflow_fs_service {
    void *impl;
} cflow_fs_service;

typedef enum cflow_fs_operation_kind {
    CFLOW_FS_STAT = 0,
    CFLOW_FS_LSTAT,
    CFLOW_FS_READ_DIRECTORY,
    CFLOW_FS_MKDIR,
    CFLOW_FS_RMDIR,
    CFLOW_FS_RENAME,
    CFLOW_FS_UNLINK
} cflow_fs_operation_kind;

typedef enum cflow_fs_submit_status {
    CFLOW_FS_SUBMIT_ACCEPTED = 0,
    CFLOW_FS_SUBMIT_INVALID_ARGUMENT,
    CFLOW_FS_SUBMIT_FULL,
    CFLOW_FS_SUBMIT_CLOSED,
    CFLOW_FS_SUBMIT_ID_EXHAUSTED
} cflow_fs_submit_status;

typedef struct cflow_fs_submit_result {
    cflow_fs_submit_status status;
    uint64_t request_id;
} cflow_fs_submit_result;

typedef enum cflow_fs_cancel_status {
    CFLOW_FS_CANCEL_REQUESTED = 0,
    CFLOW_FS_CANCEL_ALREADY_RUNNING,
    CFLOW_FS_CANCEL_NOT_FOUND,
    CFLOW_FS_CANCEL_CLOSED,
    CFLOW_FS_CANCEL_INVALID_ARGUMENT
} cflow_fs_cancel_status;

typedef enum cflow_fs_lifecycle {
    CFLOW_FS_OPEN = 0,
    CFLOW_FS_CLOSING,
    CFLOW_FS_CLOSED
} cflow_fs_lifecycle;

typedef struct cflow_fs_dir_buffer {
    /* Entries and names are caller-owned through terminal callback return. */
    turbo_fs_dirent_t *entries;
    size_t entry_capacity;
    char *names;
    size_t names_capacity;
    size_t entry_count;
    size_t names_used;
} cflow_fs_dir_buffer;

typedef void (*cflow_fs_completion_fn)(
    void *user, uint64_t request_id,
    cflow_fs_operation_kind operation, int result);

typedef struct cflow_fs_config {
    size_t worker_count;
    size_t request_capacity;
    size_t path_capacity;
    cflow_fs_completion_fn completion;
    void *completion_user;
} cflow_fs_config;

typedef struct cflow_fs_stats {
    size_t capacity;
    size_t accepted;
    size_t running;
    size_t completed;
    size_t cancelled;
    size_t in_use;
    size_t rejected_full;
    size_t rejected_closed;
    cflow_fs_lifecycle lifecycle;
} cflow_fs_stats;

/**
 * Initialize an owning bounded worker-backed filesystem service.
 *
 * @param service Zero-initialized destination handle.
 * @param config Positive worker/request/path capacities and terminal callback.
 * @return TURBO_OK, TURBO_EINVAL for an invalid contract, or TURBO_ENOMEM.
 *
 * No pathname side effect occurs during initialization. Successful destroy
 * restores service to the zero state.
 */
int cflow_fs_service_init(cflow_fs_service *service,
                          const cflow_fs_config *config);
/**
 * Submit metadata lookup following links.
 * @param service Initialized service.
 * @param path Nonempty path copied on accepted submission.
 * @param out Caller-owned result borrowed through callback return.
 * @return Exact admission result; rejection does not borrow out.
 */
cflow_fs_submit_result cflow_fs_try_stat(
    cflow_fs_service *service, const char *path, turbo_fs_stat_t *out);
/** Same contract as cflow_fs_try_stat(), without following the final link. */
cflow_fs_submit_result cflow_fs_try_lstat(
    cflow_fs_service *service, const char *path, turbo_fs_stat_t *out);
/**
 * Submit bounded directory enumeration.
 * @param out Caller-owned entries and name arena borrowed through callback.
 * @return Accepted work reports TURBO_ENOBUFS through the callback if either
 * capacity cannot hold the complete listing; used counts are then zero.
 */
cflow_fs_submit_result cflow_fs_try_read_directory(
    cflow_fs_service *service, const char *path,
    cflow_fs_dir_buffer *out);
/** Submit creation of one directory; parents must already exist. */
cflow_fs_submit_result cflow_fs_try_mkdir(
    cflow_fs_service *service, const char *path, int mode);
/** Submit removal of one empty directory. */
cflow_fs_submit_result cflow_fs_try_rmdir(
    cflow_fs_service *service, const char *path);
/** Submit same-filesystem rename/replacement with both paths copied. */
cflow_fs_submit_result cflow_fs_try_rename(
    cflow_fs_service *service, const char *old_path,
    const char *new_path);
/** Submit deletion of a non-directory filesystem object. */
cflow_fs_submit_result cflow_fs_try_unlink(
    cflow_fs_service *service, const char *path);
/**
 * Request cooperative cancellation.
 * Queued work can be cancelled. An already-running blocking syscall cannot be
 * revoked, and its actual result remains authoritative.
 */
cflow_fs_cancel_status cflow_fs_try_cancel(
    cflow_fs_service *service, uint64_t request_id);
/**
 * Deliver at most max_completions terminal callbacks on the calling thread.
 * @return TURBO_OK, TURBO_EINVAL, or TURBO_EBUSY for another/reentrant driver.
 */
int cflow_fs_run_ready(cflow_fs_service *service, size_t max_completions,
                       size_t *completed);
/** Stop admission and cancel queued work without blocking on running syscalls. */
int cflow_fs_close(cflow_fs_service *service);
/** Return true after close, worker settlement, and all callback releases. */
bool cflow_fs_is_quiescent(const cflow_fs_service *service);
/** Copy an observational bounded-protocol snapshot to out. */
bool cflow_fs_get_stats(const cflow_fs_service *service,
                        cflow_fs_stats *out);
/** Destroy a quiescent service, or return TURBO_EBUSY while work remains. */
int cflow_fs_destroy(cflow_fs_service *service);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_FS_H */
