#ifndef CFLOW_IO_FILE_H
#define CFLOW_IO_FILE_H

#include <cflow/io_native.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cflow_io_file {
  void *impl;
} cflow_io_file;

/**
 * Shared bounded execution domain for asynchronous regular files.
 *
 * One owner thread drives the runtime. Native backend workers may publish
 * completions and invoke the advisory wake callback, but file completion
 * callbacks execute only from the runtime driver.
 */
typedef struct cflow_io_file_runtime {
  void *impl;
} cflow_io_file_runtime;

typedef enum cflow_io_file_open_flags {
  CFLOW_IO_FILE_READ = 1u << 0,
  CFLOW_IO_FILE_WRITE = 1u << 1,
  CFLOW_IO_FILE_CREATE = 1u << 2,
  CFLOW_IO_FILE_TRUNCATE = 1u << 3
} cflow_io_file_open_flags;

typedef enum cflow_io_file_submit_status {
  CFLOW_IO_FILE_SUBMIT_ACCEPTED = 0,
  CFLOW_IO_FILE_SUBMIT_INVALID_ARGUMENT,
  CFLOW_IO_FILE_SUBMIT_UNSUPPORTED,
  CFLOW_IO_FILE_SUBMIT_ACCESS_DENIED,
  CFLOW_IO_FILE_SUBMIT_FULL,
  CFLOW_IO_FILE_SUBMIT_CLOSED,
  CFLOW_IO_FILE_SUBMIT_LEASE_IN_USE,
  CFLOW_IO_FILE_SUBMIT_ID_EXHAUSTED
} cflow_io_file_submit_status;

typedef struct cflow_io_file_submit_result {
  cflow_io_file_submit_status status;
  cflow_io_request_id request_id;
} cflow_io_file_submit_result;

typedef void (*cflow_io_file_completion_fn)(void *user, cflow_io_request_id request_id,
                                            cflow_io_lease_id lease_id,
                                            cflow_io_native_file_operation_kind operation_kind,
                                            const cflow_io_completion *completion);

typedef struct cflow_io_file_runtime_config {
  cflow_io_native_backend_kind backend_kind;
  /** Hard cap for open or opening file handles in this runtime. */
  size_t file_capacity;
  size_t request_capacity;
  size_t command_capacity;
  size_t completion_batch_capacity;
  cflow_io_wake_fn wake;
  void *wake_user;
} cflow_io_file_runtime_config;

typedef struct cflow_io_file_runtime_stats {
  cflow_io_actor_stats actor;
  cflow_io_native_backend_stats backend;
  size_t operation_slots_in_use;
  size_t file_capacity;
  size_t open_files;
  bool close_requested;
} cflow_io_file_runtime_stats;

typedef struct cflow_io_file_config {
  cflow_io_native_backend_kind backend_kind;
  size_t request_capacity;
  size_t command_capacity;
  size_t completion_batch_capacity;
  uint32_t open_flags;
  uint32_t create_mode;
  cflow_io_file_completion_fn completion;
  void *completion_user;
  /**
   * Optional borrowed shared runtime. When non-NULL, backend_kind and the
   * three capacity fields must be zero because the runtime is their single
   * source of truth. The runtime must outlive this file.
   */
  cflow_io_file_runtime *runtime;
} cflow_io_file_config;

typedef struct cflow_io_file_stats {
  cflow_io_actor_stats actor;
  cflow_io_native_backend_stats backend;
  size_t operation_slots_in_use;
  bool close_requested;
} cflow_io_file_stats;

/** Initializes one shared native backend, Actor, Executor, and slot set. */
int cflow_io_file_runtime_init(cflow_io_file_runtime *runtime,
                               const cflow_io_file_runtime_config *config);

/**
 * Drives at most max_steps state transitions and file completion callbacks.
 * Exactly one thread may drive a runtime; reentrant or concurrent drive is
 * rejected with SALTS_EBUSY.
 */
int cflow_io_file_runtime_run_ready(cflow_io_file_runtime *runtime, size_t max_steps,
                                    size_t *progressed);

/** Stops admission and requests cancellation for all accepted operations. */
int cflow_io_file_runtime_close(cflow_io_file_runtime *runtime);

/** True after close and terminal completion/acknowledgement drain. */
bool cflow_io_file_runtime_is_quiescent(const cflow_io_file_runtime *runtime);

/** Copies bounded runtime counters and lifecycle state. */
bool cflow_io_file_runtime_get_stats(const cflow_io_file_runtime *runtime,
                                     cflow_io_file_runtime_stats *out);

/**
 * Destroys a closed quiescent runtime with no open files. SALTS_EBUSY leaves
 * the runtime owned and retryable.
 */
int cflow_io_file_runtime_destroy(cflow_io_file_runtime *runtime);

/**
 * Opens one owning asynchronous regular-file facade.
 *
 * Path resolution and handle creation complete synchronously. Native data I/O
 * starts only after a successful try-read/write/flush call is driven. The
 * destination must be zero-initialized. Unsupported backends fail before path
 * creation or truncation; no fallback backend is selected. A file with an
 * injected runtime borrows it through file destroy; otherwise open constructs
 * a private runtime from the legacy backend and capacity fields.
 *
 * @param file Zero-initialized destination handle
 * @param path Non-empty platform path
 * @param config Capacities, explicit backend, access flags, and callback
 * @return SALTS_OK, SALTS_EINVAL, SALTS_ENOTSUP, SALTS_ENOMEM, or a negative
 *         native path/open error
 */
int cflow_io_file_open(cflow_io_file *file, const char *path, const cflow_io_file_config *config);

/** Returns whether this opened facade permits and natively supports an operation. */
bool cflow_io_file_operation_supported(const cflow_io_file *file,
                                       cflow_io_native_file_operation_kind operation_kind);

/**
 * Attempts an offset read without blocking for completion.
 *
 * Accepted submission borrows writable buffer storage through completion
 * callback return. Rejection preserves all caller ownership.
 */
cflow_io_file_submit_result cflow_io_file_try_read_at(cflow_io_file *file,
                                                      cflow_io_lease_id lease_id, void *buffer,
                                                      size_t length, uint64_t offset);

/**
 * Attempts an offset write without blocking for completion.
 *
 * Accepted submission borrows immutable buffer storage through completion
 * callback return. Rejection preserves all caller ownership.
 */
cflow_io_file_submit_result cflow_io_file_try_write_at(cflow_io_file *file,
                                                       cflow_io_lease_id lease_id,
                                                       const void *buffer, size_t length,
                                                       uint64_t offset);

/** Attempts a native asynchronous flush; unsupported backends reject immediately. */
cflow_io_file_submit_result cflow_io_file_try_flush(cflow_io_file *file,
                                                    cflow_io_lease_id lease_id);

/** Requests cancellation; native completion remains the terminal evidence. */
cflow_io_cancel_status cflow_io_file_try_cancel(cflow_io_file *file,
                                                cflow_io_request_id request_id);

/**
 * Drives at most max_steps Actor, callback, or acknowledgement actions.
 *
 * Exactly one thread may drive a file. Completion callbacks run synchronously
 * on that driver thread. Reentrant/concurrent drive returns SALTS_EBUSY and
 * leaves progressed unchanged.
 *
 * @param file Open facade
 * @param max_steps Nonzero hard work bound
 * @param progressed Receives the number of completed driver actions
 * @return SALTS_OK, SALTS_EINVAL, SALTS_EBUSY, or SALTS_EPROTO
 */
int cflow_io_file_run_ready(cflow_io_file *file, size_t max_steps, size_t *progressed);

/** Stops admission and requests cancellation without closing the live handle. */
int cflow_io_file_close(cflow_io_file *file);

/** Returns true only after close and terminal callback/acknowledgement drain. */
bool cflow_io_file_is_quiescent(const cflow_io_file *file);

/** Copies Actor, backend, slot, and close-state statistics into out. */
bool cflow_io_file_get_stats(const cflow_io_file *file, cflow_io_file_stats *out);

/**
 * Destroys a closed quiescent facade and restores it to zero state.
 *
 * The caller must stop and join the driver and all submit/cancel producers
 * before destroy. Destroy is an exclusive control-plane operation and must not
 * race any other facade API entry. SALTS_EBUSY leaves the object owned and
 * retryable. After quiescence, native close errors are returned after remaining
 * owned resources are released and the public handle is cleared.
 */
int cflow_io_file_destroy(cflow_io_file *file);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_IO_FILE_H */
