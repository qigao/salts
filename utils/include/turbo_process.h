/**
 * @file turbo_process.h
 * @brief Cross-platform child process management without an event-loop dependency.
 *
 * A turbo_process_t exclusively owns the child process, its process group or
 * Windows Job Object, redirected standard-I/O handles, monitor thread, and
 * terminal result. Operations are thread-safe except destroy, which requires
 * exclusive ownership. Destroying a running handle terminates and reaps the
 * owned process tree.
 */

#ifndef TURBO_PROCESS_H
#define TURBO_PROCESS_H

#include "platform.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TURBO_PROCESS_DEFAULT_MAX_OUTPUT_BYTES (16U * 1024U * 1024U)

typedef struct turbo_process_s turbo_process_t;

typedef enum {
  TURBO_PROCESS_STARTING = 0,
  TURBO_PROCESS_RUNNING,
  TURBO_PROCESS_EXITED,
  TURBO_PROCESS_SIGNALED,
  TURBO_PROCESS_TIMED_OUT,
  TURBO_PROCESS_TERMINATED,
  TURBO_PROCESS_OUTPUT_LIMIT_EXCEEDED,
  TURBO_PROCESS_WAIT_FAILED
} turbo_process_state_t;

typedef enum {
  TURBO_PROCESS_PIPE_STDIN = 1U << 0,
  TURBO_PROCESS_CAPTURE_STDOUT = 1U << 1,
  TURBO_PROCESS_CAPTURE_STDERR = 1U << 2,
  /** Do not inherit parent variables; env becomes the complete child environment. */
  TURBO_PROCESS_CLEAN_ENVIRONMENT = 1U << 3
} turbo_process_flags_t;

typedef struct {
  /** Executable path or name. A shell is never invoked implicitly. */
  const char *program;
  /** NULL-terminated arguments excluding argv[0], or NULL for no arguments. */
  const char *const *args;
  /**
   * NULL inherits the parent environment. A non-NULL, NULL-terminated list of
   * KEY=VALUE entries overrides matching parent variables. With
   * TURBO_PROCESS_CLEAN_ENVIRONMENT, this list is the complete child
   * environment and NULL creates an empty environment.
   */
  const char *const *env;
  /** Child working directory, or NULL to inherit the current directory. */
  const char *cwd;
  /** Combination of turbo_process_flags_t. */
  unsigned int flags;
  /** Execution deadline in milliseconds. Zero disables the deadline. */
  uint64_t timeout_ms;
  /** Shared stdout/stderr capture limit. Zero selects the default limit. */
  size_t max_output_bytes;
} turbo_process_options_t;

typedef struct {
  turbo_process_state_t state;
  int pid;
  int exit_code;
  int term_signal;
  int error_code;
} turbo_process_result_t;

/** Select parent standard-handle inheritance for one spawn binding. */
#define TURBO_PROCESS_STDIO_INHERIT UINTPTR_MAX

/**
 * Child-side standard handles borrowed until turbo_process_spawn_with_stdio()
 * returns. Each value is a native HANDLE/fd or TURBO_PROCESS_STDIO_INHERIT.
 * The spawn call never closes or consumes a supplied handle.
 */
typedef struct {
  uintptr_t stdin_handle;
  uintptr_t stdout_handle;
  uintptr_t stderr_handle;
} turbo_process_stdio_bindings_t;

/** Fill options with defaults: stdout/stderr capture and no execution deadline. */
TURBO_C_API void turbo_process_options_init(turbo_process_options_t *options);

/** Return the stable string name of a process state. */
TURBO_C_API const char *turbo_process_state_name(turbo_process_state_t state);

/**
 * Start a child process and its monitor thread.
 *
 * On success, *out_process owns all resources and must be destroyed. On
 * failure, *out_process is NULL and the return value is a TurboUtils or
 * negative native platform error code.
 */
TURBO_C_API int turbo_process_spawn(const turbo_process_options_t *options,
                                  turbo_process_t **out_process);

/**
 * Start a child with explicitly borrowed child-side standard handles.
 *
 * A supplied stdin handle conflicts with TURBO_PROCESS_PIPE_STDIN. Supplied
 * stdout/stderr handles conflict with their corresponding capture flags.
 * Contradictory bindings return TURBO_EINVAL without consuming any handle.
 * The returned process owns its lifecycle but never reads or closes a supplied
 * parent handle.
 */
TURBO_C_API int turbo_process_spawn_with_stdio(const turbo_process_options_t *options,
                                               const turbo_process_stdio_bindings_t *bindings,
                                               turbo_process_t **out_process);

/** Return the owned child PID, or -1 for an invalid handle. */
TURBO_C_API int turbo_process_pid(const turbo_process_t *process);

/** Return the current lifecycle state. */
TURBO_C_API turbo_process_state_t turbo_process_state(const turbo_process_t *process);

/** Return true while the owned child has not reached a terminal state. */
TURBO_C_API bool turbo_process_is_running(const turbo_process_t *process);

/**
 * Copy the terminal result without blocking.
 *
 * Returns TURBO_OK when terminal, TURBO_EBUSY while running, or TURBO_EINVAL
 * for invalid arguments.
 */
TURBO_C_API int turbo_process_poll(const turbo_process_t *process,
                                 turbo_process_result_t *out_result);

/** Wait indefinitely for the terminal result. */
TURBO_C_API int turbo_process_wait(turbo_process_t *process, turbo_process_result_t *out_result);

/**
 * Wait at most timeout_ms for the terminal result.
 *
 * TURBO_ETIMEDOUT only describes this wait operation and does not terminate
 * the process. Use options.timeout_ms for an execution deadline.
 */
TURBO_C_API int turbo_process_wait_for(turbo_process_t *process, uint64_t timeout_ms,
                                     turbo_process_result_t *out_result);

/** Terminate the owned process tree. This operation is idempotent. */
TURBO_C_API int turbo_process_terminate(turbo_process_t *process);

/**
 * Queue bytes for the child's stdin pipe.
 *
 * This call may block until the OS pipe accepts the bytes. It returns
 * TURBO_EPIPE after stdin has been closed or when no stdin pipe was requested.
 */
TURBO_C_API int turbo_process_write_stdin(turbo_process_t *process, const void *data, size_t size,
                                        size_t *out_written);

/** Close the parent's stdin pipe so the child observes EOF. */
TURBO_C_API int turbo_process_close_stdin(turbo_process_t *process);

/**
 * Consume currently captured stdout bytes.
 *
 * Returns TURBO_OK with *out_read possibly zero, or TURBO_EOF after the stream
 * is closed and all captured bytes have been consumed.
 */
TURBO_C_API int turbo_process_read_stdout(turbo_process_t *process, void *buffer, size_t capacity,
                                        size_t *out_read);

/** Consume currently captured stderr bytes; semantics match stdout. */
TURBO_C_API int turbo_process_read_stderr(turbo_process_t *process, void *buffer, size_t capacity,
                                        size_t *out_read);

/**
 * Check whether a PID currently names a live process.
 *
 * This is observation only. Managed termination uses the retained native
 * handle to avoid PID-reuse races.
 */
TURBO_C_API int turbo_process_is_pid_alive(int pid, bool *out_alive);

/** Terminate, reap, close, and free a process handle; no concurrent calls are allowed. */
TURBO_C_API void turbo_process_destroy(turbo_process_t *process);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_PROCESS_H */
