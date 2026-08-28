#ifndef CFLOW_PROCESS_H
#define CFLOW_PROCESS_H

#include <cflow/io_native.h>
#include <turbo_process.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cflow_process {
  void *impl;
} cflow_process;

typedef enum cflow_process_stream {
  CFLOW_PROCESS_STDIN = 0,
  CFLOW_PROCESS_STDOUT,
  CFLOW_PROCESS_STDERR
} cflow_process_stream;

typedef enum cflow_process_submit_status {
  CFLOW_PROCESS_SUBMIT_ACCEPTED = 0,
  CFLOW_PROCESS_SUBMIT_INVALID_ARGUMENT,
  CFLOW_PROCESS_SUBMIT_UNSUPPORTED,
  CFLOW_PROCESS_SUBMIT_FULL,
  CFLOW_PROCESS_SUBMIT_CLOSED,
  CFLOW_PROCESS_SUBMIT_LEASE_IN_USE,
  CFLOW_PROCESS_SUBMIT_ID_EXHAUSTED
} cflow_process_submit_status;

typedef struct cflow_process_submit_result {
  cflow_process_submit_status status;
  cflow_io_request_id request_id;
} cflow_process_submit_result;

typedef void (*cflow_process_completion_fn)(void *user, cflow_io_request_id request_id,
                                            cflow_io_lease_id lease_id, cflow_process_stream stream,
                                            const cflow_io_completion *completion);

typedef struct cflow_process_config {
  cflow_io_native_backend_kind backend_kind;
  size_t request_capacity;
  size_t command_capacity;
  size_t completion_batch_capacity;
  cflow_process_completion_fn completion;
  void *completion_user;
} cflow_process_config;

typedef struct cflow_process_stats {
  cflow_io_actor_stats io;
  bool stdin_open;
  bool stdout_open;
  bool stderr_open;
  bool close_requested;
  int cleanup_error;
} cflow_process_stats;

/**
 * Starts one process with three adapter-owned asynchronous standard streams.
 * options and config are borrowed only for the call. Capture/pipe flags,
 * zero capacities, a null callback, or an initialized output return
 * TURBO_EINVAL; an unavailable pipe backend returns TURBO_ENOTSUP. On success
 * process owns all runtime resources until quiescent destroy.
 */
int cflow_process_start(cflow_process *process, const turbo_process_options_t *options,
                        const cflow_process_config *config);

/**
 * Admits one bounded write. buffer is borrowed until its terminal callback
 * returns; partial completion is successful and reports the transferred size.
 */
cflow_process_submit_result cflow_process_try_write_stdin(cflow_process *process,
                                                          cflow_io_lease_id lease_id,
                                                          const void *buffer, size_t length);

/** Borrowing and partial-completion semantics match try_write_stdin(). */
cflow_process_submit_result cflow_process_try_read_stdout(cflow_process *process,
                                                          cflow_io_lease_id lease_id, void *buffer,
                                                          size_t length);

/** Borrowing and partial-completion semantics match try_write_stdin(). */
cflow_process_submit_result cflow_process_try_read_stderr(cflow_process *process,
                                                          cflow_io_lease_id lease_id, void *buffer,
                                                          size_t length);

/**
 * Requests cancellation for one admitted request. Acceptance is not terminal
 * evidence; the caller must retain its buffer through the later callback.
 */
cflow_io_cancel_status cflow_process_try_cancel(cflow_process *process,
                                                cflow_io_request_id request_id);

/**
 * Closes stdin after all admitted stdin writes have settled. Returns
 * TURBO_EBUSY while any stdin write is live; success is idempotent.
 */
int cflow_process_close_stdin(cflow_process *process);

int cflow_process_poll(const cflow_process *process, turbo_process_result_t *out_result);

/** Requests termination through the Core process owner. */
int cflow_process_terminate(cflow_process *process);

/** Drives at most max_steps Actor/Executor transitions on one driver thread. */
int cflow_process_run_ready(cflow_process *process, size_t max_steps, size_t *progressed);

/**
 * Copies bounded I/O admission counters and endpoint ownership state.
 * The Actor counters and endpoint fields are individually consistent; callers
 * coordinating concurrent producers must treat the combined value as an
 * observational snapshot rather than one cross-component transaction.
 */
bool cflow_process_get_stats(const cflow_process *process, cflow_process_stats *out);

/**
 * Stops I/O admission, requests cancellation, and terminates a live child.
 * Call from the lifecycle owner after all producer threads have stopped.
 */
int cflow_process_close(cflow_process *process);

bool cflow_process_is_quiescent(const cflow_process *process);

/** Destroys only after close, I/O drain, endpoint close, and process terminal. */
int cflow_process_destroy(cflow_process *process);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_PROCESS_H */
