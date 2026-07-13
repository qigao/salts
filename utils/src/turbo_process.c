/**
 * @file turbo_process.c
 * @brief Native Windows/POSIX child process management backend.
 */

#include "turbo_process.h"

#include "turbo_error.h"
#include "turbo_str.h"
#include "turbo_thread.h"

#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TURBO_PROCESS_POLL_INTERVAL_MS 10U
#define TURBO_PROCESS_WINDOWS_BLOCK_MAX 32767U

#ifdef _WIN32
  #include <wchar.h>
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <poll.h>
  #include <pthread.h>
  #include <signal.h>
  #include <sys/wait.h>
  #include <unistd.h>

extern char **environ;
#endif

struct turbo_process_s {
  turbo_mutex_t mutex;
  turbo_cond_t changed;
  turbo_mutex_t stdin_mutex;
  turbo_thread_t monitor_thread;
  int monitor_started;
  int monitor_joined;

  turbo_process_state_t state;
  turbo_process_result_t result;
  int terminate_requested;
  int native_active;
  uint64_t timeout_ms;
  size_t max_output_bytes;
  size_t captured_bytes;

  tstr_t stdout_data;
  tstr_t stderr_data;
  size_t stdout_offset;
  size_t stderr_offset;
  atomic_int stdout_open;
  atomic_int stderr_open;

#ifdef _WIN32
  HANDLE process_handle;
  HANDLE job_handle;
  HANDLE stdin_write;
  HANDLE stdout_read;
  HANDLE stderr_read;
#else
  pid_t pid;
  int stdin_fd;
  int stdout_fd;
  int stderr_fd;
#endif
};

static int process_state_terminal(turbo_process_state_t state) {
  return state == TURBO_PROCESS_EXITED || state == TURBO_PROCESS_SIGNALED ||
         state == TURBO_PROCESS_TIMED_OUT || state == TURBO_PROCESS_TERMINATED ||
         state == TURBO_PROCESS_OUTPUT_LIMIT_EXCEEDED || state == TURBO_PROCESS_WAIT_FAILED;
}

static int process_validate_options(const turbo_process_options_t *options) {
  const unsigned int known_flags =
      TURBO_PROCESS_PIPE_STDIN | TURBO_PROCESS_CAPTURE_STDOUT | TURBO_PROCESS_CAPTURE_STDERR;
  if (!options || !options->program || options->program[0] == '\0') return TURBO_EINVAL;
  if ((options->flags & ~known_flags) != 0U) return TURBO_EINVAL;
  return TURBO_OK;
}

static int process_init(turbo_process_t *process, const turbo_process_options_t *options) {
  memset(process, 0, sizeof(*process));
#ifdef _WIN32
  process->process_handle = NULL;
  process->job_handle = NULL;
  process->stdin_write = INVALID_HANDLE_VALUE;
  process->stdout_read = INVALID_HANDLE_VALUE;
  process->stderr_read = INVALID_HANDLE_VALUE;
#else
  process->pid = -1;
  process->stdin_fd = -1;
  process->stdout_fd = -1;
  process->stderr_fd = -1;
#endif
  turbo_mutex_init(&process->mutex);
  turbo_cond_init(&process->changed);
  turbo_mutex_init(&process->stdin_mutex);
  if (!process->mutex || !process->changed || !process->stdin_mutex) return TURBO_ENOMEM;

  process->state = TURBO_PROCESS_STARTING;
  process->result.state = TURBO_PROCESS_STARTING;
  process->result.pid = -1;
  process->result.exit_code = -1;
  process->timeout_ms = options->timeout_ms;
  process->max_output_bytes = options->max_output_bytes != 0
                                  ? options->max_output_bytes
                                  : TURBO_PROCESS_DEFAULT_MAX_OUTPUT_BYTES;
  process->stdout_data = tstr_new();
  process->stderr_data = tstr_new();
  if (!process->stdout_data || !process->stderr_data) return TURBO_ENOMEM;
  return TURBO_OK;
}

static void process_finish(turbo_process_t *process, turbo_process_state_t state, int exit_code,
                           int term_signal, int error_code) {
  turbo_mutex_lock(&process->mutex);
  process->state = state;
  process->result.state = state;
  process->result.exit_code = exit_code;
  process->result.term_signal = term_signal;
  process->result.error_code = error_code;
  process->stdout_open = 0;
  process->stderr_open = 0;
  turbo_cond_broadcast(&process->changed);
  turbo_mutex_unlock(&process->mutex);
}

static int process_append_output(turbo_process_t *process, int is_stdout, const char *data,
                                 size_t size) {
  tstr_t *target;
  tstr_t appended;
  size_t remaining;
  size_t accepted;

  if (size == 0) return TURBO_OK;
  turbo_mutex_lock(&process->mutex);
  remaining = process->captured_bytes < process->max_output_bytes
                  ? process->max_output_bytes - process->captured_bytes
                  : 0;
  accepted = size < remaining ? size : remaining;
  target = is_stdout ? &process->stdout_data : &process->stderr_data;
  if (accepted > 0) {
    appended = tstr_cat_len(*target, data, accepted);
    if (!appended) {
      turbo_mutex_unlock(&process->mutex);
      return TURBO_ENOMEM;
    }
    *target = appended;
    process->captured_bytes += accepted;
  }
  turbo_cond_broadcast(&process->changed);
  turbo_mutex_unlock(&process->mutex);
  return accepted == size ? TURBO_OK : TURBO_ERANGE;
}

static int process_read_buffer(turbo_process_t *process, int is_stdout, void *buffer,
                               size_t capacity, size_t *out_read) {
  tstr_t data;
  size_t *offset;
  size_t available;
  size_t count;
  int stream_open;

  if (!process || !out_read || (capacity > 0 && !buffer)) return TURBO_EINVAL;
  *out_read = 0;
  turbo_mutex_lock(&process->mutex);
  data = is_stdout ? process->stdout_data : process->stderr_data;
  offset = is_stdout ? &process->stdout_offset : &process->stderr_offset;
  stream_open = is_stdout ? process->stdout_open : process->stderr_open;
  available = tstr_len(data) - *offset;
  count = available < capacity ? available : capacity;
  if (count > 0) {
    memcpy(buffer, data + *offset, count);
    *offset += count;
    *out_read = count;
  }
  turbo_mutex_unlock(&process->mutex);
  return count == 0 && !stream_open ? TURBO_EOF : TURBO_OK;
}

static void process_join_monitor(turbo_process_t *process) {
  int should_join = 0;
  turbo_mutex_lock(&process->mutex);
  if (process->monitor_started && !process->monitor_joined) {
    process->monitor_joined = 1;
    should_join = 1;
  }
  turbo_mutex_unlock(&process->mutex);
  if (should_join) turbo_thread_join(&process->monitor_thread);
}

void turbo_process_options_init(turbo_process_options_t *options) {
  if (!options) return;
  memset(options, 0, sizeof(*options));
  options->flags = TURBO_PROCESS_CAPTURE_STDOUT | TURBO_PROCESS_CAPTURE_STDERR;
  options->max_output_bytes = TURBO_PROCESS_DEFAULT_MAX_OUTPUT_BYTES;
}

const char *turbo_process_state_name(turbo_process_state_t state) {
  switch (state) {
  case TURBO_PROCESS_STARTING:
    return "starting";
  case TURBO_PROCESS_RUNNING:
    return "running";
  case TURBO_PROCESS_EXITED:
    return "exited";
  case TURBO_PROCESS_SIGNALED:
    return "signaled";
  case TURBO_PROCESS_TIMED_OUT:
    return "timed_out";
  case TURBO_PROCESS_TERMINATED:
    return "terminated";
  case TURBO_PROCESS_OUTPUT_LIMIT_EXCEEDED:
    return "output_limit_exceeded";
  case TURBO_PROCESS_WAIT_FAILED:
    return "wait_failed";
  }
  return "unknown";
}

int turbo_process_pid(const turbo_process_t *process) {
  int pid;
  if (!process) return -1;
  turbo_mutex_lock((turbo_mutex_t *)&process->mutex);
  pid = process->result.pid;
  turbo_mutex_unlock((turbo_mutex_t *)&process->mutex);
  return pid;
}

turbo_process_state_t turbo_process_state(const turbo_process_t *process) {
  turbo_process_state_t state;
  if (!process) return TURBO_PROCESS_WAIT_FAILED;
  turbo_mutex_lock((turbo_mutex_t *)&process->mutex);
  state = process->state;
  turbo_mutex_unlock((turbo_mutex_t *)&process->mutex);
  return state;
}

bool turbo_process_is_running(const turbo_process_t *process) {
  return process && !process_state_terminal(turbo_process_state(process));
}

int turbo_process_poll(const turbo_process_t *process, turbo_process_result_t *out_result) {
  if (!process || !out_result) return TURBO_EINVAL;
  turbo_mutex_lock((turbo_mutex_t *)&process->mutex);
  if (!process_state_terminal(process->state)) {
    turbo_mutex_unlock((turbo_mutex_t *)&process->mutex);
    return TURBO_EBUSY;
  }
  *out_result = process->result;
  turbo_mutex_unlock((turbo_mutex_t *)&process->mutex);
  return TURBO_OK;
}

int turbo_process_wait(turbo_process_t *process, turbo_process_result_t *out_result) {
  if (!process || !out_result) return TURBO_EINVAL;
  turbo_mutex_lock(&process->mutex);
  while (!process_state_terminal(process->state)) {
    turbo_cond_wait(&process->changed, &process->mutex);
  }
  *out_result = process->result;
  turbo_mutex_unlock(&process->mutex);
  return TURBO_OK;
}

int turbo_process_wait_for(turbo_process_t *process, uint64_t timeout_ms,
                           turbo_process_result_t *out_result) {
  uint64_t deadline;
  int rc = TURBO_OK;
  if (!process || !out_result) return TURBO_EINVAL;
  deadline = turbo_monotonic_ms();
  deadline = timeout_ms > UINT64_MAX - deadline ? UINT64_MAX : deadline + timeout_ms;

  turbo_mutex_lock(&process->mutex);
  while (!process_state_terminal(process->state)) {
    uint64_t now = turbo_monotonic_ms();
    uint64_t remaining_ms;
    if (now >= deadline) {
      rc = TURBO_ETIMEDOUT;
      break;
    }
    remaining_ms = deadline - now;
    if (remaining_ms > 1000U) remaining_ms = 1000U;
    if (turbo_cond_timedwait(&process->changed, &process->mutex, remaining_ms * 1000000ULL) != 0 &&
        !process_state_terminal(process->state) && turbo_monotonic_ms() >= deadline) {
      rc = TURBO_ETIMEDOUT;
      break;
    }
  }
  if (rc == TURBO_OK) *out_result = process->result;
  turbo_mutex_unlock(&process->mutex);
  return rc;
}

int turbo_process_read_stdout(turbo_process_t *process, void *buffer, size_t capacity,
                              size_t *out_read) {
  return process_read_buffer(process, 1, buffer, capacity, out_read);
}

int turbo_process_read_stderr(turbo_process_t *process, void *buffer, size_t capacity,
                              size_t *out_read) {
  return process_read_buffer(process, 0, buffer, capacity, out_read);
}

#ifdef _WIN32

static int win32_error(void) {
  DWORD error = GetLastError();
  return error > (DWORD)INT_MAX ? TURBO_UNKNOWN : -(int)error;
}

static void close_win_handle(HANDLE *handle) {
  if (*handle != NULL && *handle != INVALID_HANDLE_VALUE) CloseHandle(*handle);
  *handle = INVALID_HANDLE_VALUE;
}

static int duplicate_standard_handle(DWORD standard_handle, DWORD fallback_access,
                                     HANDLE *out_handle) {
  HANDLE source = GetStdHandle(standard_handle);
  if (source != NULL && source != INVALID_HANDLE_VALUE &&
      DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), out_handle, 0, TRUE,
                      DUPLICATE_SAME_ACCESS)) {
    return TURBO_OK;
  }
  *out_handle = CreateFileW(L"NUL", fallback_access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (*out_handle == INVALID_HANDLE_VALUE) return win32_error();
  if (!SetHandleInformation(*out_handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
    int rc = win32_error();
    close_win_handle(out_handle);
    return rc;
  }
  return TURBO_OK;
}

static int utf8_to_utf16(const char *input, wchar_t **out) {
  int length;
  wchar_t *wide;
  if (!input) {
    *out = NULL;
    return TURBO_OK;
  }
  length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, -1, NULL, 0);
  if (length <= 0) return win32_error();
  wide = (wchar_t *)malloc((size_t)length * sizeof(wchar_t));
  if (!wide) return TURBO_ENOMEM;
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, -1, wide, length) <= 0) {
    int rc = win32_error();
    free(wide);
    return rc;
  }
  *out = wide;
  return TURBO_OK;
}

static int windows_arg_needs_quotes(const wchar_t *arg) {
  if (*arg == L'\0') return 1;
  for (; *arg; ++arg) {
    if (*arg == L' ' || *arg == L'\t' || *arg == L'\n' || *arg == L'\v' || *arg == L'"') return 1;
  }
  return 0;
}

static int windows_quoted_length(const wchar_t *arg, size_t *out_length) {
  size_t length = wcslen(arg);
  if (windows_arg_needs_quotes(arg)) {
    if (length > (SIZE_MAX - 3U) / 2U) return TURBO_ENOMEM;
    *out_length = length * 2U + 3U;
  } else {
    if (length == SIZE_MAX) return TURBO_ENOMEM;
    *out_length = length + 1U;
  }
  return TURBO_OK;
}

static wchar_t *windows_quote_arg(wchar_t *output, const wchar_t *arg) {
  size_t slashes = 0;
  const wchar_t *cursor;
  if (!windows_arg_needs_quotes(arg)) {
    while (*arg)
      *output++ = *arg++;
    return output;
  }
  *output++ = L'"';
  for (cursor = arg; *cursor; ++cursor) {
    if (*cursor == L'\\') {
      ++slashes;
      continue;
    }
    if (*cursor == L'"') {
      while (slashes-- > 0) {
        *output++ = L'\\';
        *output++ = L'\\';
      }
      *output++ = L'\\';
      *output++ = L'"';
      slashes = 0;
      continue;
    }
    while (slashes-- > 0)
      *output++ = L'\\';
    slashes = 0;
    *output++ = *cursor;
  }
  while (slashes-- > 0) {
    *output++ = L'\\';
    *output++ = L'\\';
  }
  *output++ = L'"';
  return output;
}

static int build_windows_command(const turbo_process_options_t *options, wchar_t **out_command) {
  size_t count = 1;
  size_t total = 0;
  size_t i;
  wchar_t **items;
  wchar_t *command;
  wchar_t *cursor;
  int rc;

  if (options->args) {
    while (options->args[count - 1]) {
      if (count == SIZE_MAX / sizeof(*items)) return TURBO_ENOMEM;
      ++count;
    }
  }
  items = (wchar_t **)calloc(count, sizeof(*items));
  if (!items) return TURBO_ENOMEM;
  rc = utf8_to_utf16(options->program, &items[0]);
  for (i = 1; rc == TURBO_OK && i < count; ++i)
    rc = utf8_to_utf16(options->args[i - 1], &items[i]);
  if (rc != TURBO_OK) goto cleanup;
  for (i = 0; i < count; ++i) {
    size_t item_length;
    rc = windows_quoted_length(items[i], &item_length);
    if (rc != TURBO_OK || item_length > TURBO_PROCESS_WINDOWS_BLOCK_MAX - total) {
      rc = rc != TURBO_OK ? rc : TURBO_EINVAL;
      goto cleanup;
    }
    total += item_length;
  }
  if (total >= TURBO_PROCESS_WINDOWS_BLOCK_MAX) {
    rc = TURBO_EINVAL;
    goto cleanup;
  }
  command = (wchar_t *)calloc(total + 1, sizeof(wchar_t));
  if (!command) {
    rc = TURBO_ENOMEM;
    goto cleanup;
  }
  cursor = command;
  for (i = 0; i < count; ++i) {
    if (i != 0) *cursor++ = L' ';
    cursor = windows_quote_arg(cursor, items[i]);
  }
  *cursor = L'\0';
  *out_command = command;

cleanup:
  for (i = 0; i < count; ++i)
    free(items[i]);
  free(items);
  return rc;
}

static size_t windows_env_key_length(const wchar_t *entry) {
  const wchar_t *equals = wcschr(entry + (entry[0] == L'=' ? 1 : 0), L'=');
  return equals ? (size_t)(equals - entry) : 0;
}

static int windows_env_key_equal(const wchar_t *left, const wchar_t *right) {
  size_t left_length = windows_env_key_length(left);
  size_t right_length = windows_env_key_length(right);
  return left_length != 0 && left_length == right_length &&
         _wcsnicmp(left, right, left_length) == 0;
}

static int windows_env_entry_compare(const void *left, const void *right) {
  const wchar_t *const *left_entry = (const wchar_t *const *)left;
  const wchar_t *const *right_entry = (const wchar_t *const *)right;
  return _wcsicmp(*left_entry, *right_entry);
}

static int build_windows_environment(const char *const *env, wchar_t **out_environment) {
  LPWCH parent_block;
  wchar_t **overrides = NULL;
  wchar_t **entries = NULL;
  size_t override_count = 0;
  size_t parent_count = 0;
  size_t entry_count = 0;
  size_t total = 1;
  size_t i;
  wchar_t *block = NULL;
  wchar_t *cursor;
  int rc = TURBO_OK;
  if (!env) {
    *out_environment = NULL;
    return TURBO_OK;
  }

  while (env[override_count])
    ++override_count;
  overrides = (wchar_t **)calloc(override_count, sizeof(*overrides));
  if (override_count != 0 && !overrides) return TURBO_ENOMEM;
  for (i = 0; i < override_count; ++i) {
    const char *equals = strchr(env[i], '=');
    if (!equals || equals == env[i]) {
      rc = TURBO_EINVAL;
      goto cleanup;
    }
    rc = utf8_to_utf16(env[i], &overrides[i]);
    if (rc != TURBO_OK) goto cleanup;
    for (size_t j = 0; j < i; ++j) {
      if (windows_env_key_equal(overrides[i], overrides[j])) {
        rc = TURBO_EINVAL;
        goto cleanup;
      }
    }
  }

  parent_block = GetEnvironmentStringsW();
  if (!parent_block) {
    rc = win32_error();
    goto cleanup;
  }
  for (cursor = parent_block; *cursor; cursor += wcslen(cursor) + 1)
    ++parent_count;
  if (override_count > SIZE_MAX - parent_count ||
      parent_count + override_count > SIZE_MAX / sizeof(*entries)) {
    rc = TURBO_ENOMEM;
    FreeEnvironmentStringsW(parent_block);
    goto cleanup;
  }
  entries = (wchar_t **)calloc(parent_count + override_count, sizeof(*entries));
  if (parent_count + override_count != 0 && !entries) {
    rc = TURBO_ENOMEM;
    FreeEnvironmentStringsW(parent_block);
    goto cleanup;
  }
  for (cursor = parent_block; *cursor; cursor += wcslen(cursor) + 1) {
    int overridden = 0;
    for (i = 0; i < override_count; ++i) {
      if (windows_env_key_equal(cursor, overrides[i])) {
        overridden = 1;
        break;
      }
    }
    if (!overridden) entries[entry_count++] = cursor;
  }
  for (i = 0; i < override_count; ++i)
    entries[entry_count++] = overrides[i];
  qsort(entries, entry_count, sizeof(*entries), windows_env_entry_compare);
  for (i = 0; i < entry_count; ++i) {
    size_t length = wcslen(entries[i]) + 1U;
    if (length > TURBO_PROCESS_WINDOWS_BLOCK_MAX - total) {
      rc = TURBO_EINVAL;
      FreeEnvironmentStringsW(parent_block);
      goto cleanup;
    }
    total += length;
  }
  block = (wchar_t *)calloc(total, sizeof(wchar_t));
  if (!block) {
    rc = TURBO_ENOMEM;
    FreeEnvironmentStringsW(parent_block);
    goto cleanup;
  }
  cursor = block;
  for (i = 0; i < entry_count; ++i) {
    size_t length = wcslen(entries[i]) + 1;
    memcpy(cursor, entries[i], length * sizeof(wchar_t));
    cursor += length;
  }
  *cursor = L'\0';
  FreeEnvironmentStringsW(parent_block);
  *out_environment = block;

cleanup:
  for (i = 0; i < override_count; ++i)
    free(overrides[i]);
  free(overrides);
  free(entries);
  return rc;
}

static int process_platform_terminate(turbo_process_t *process) {
  HANDLE job;
  HANDLE child;
  turbo_mutex_lock(&process->mutex);
  if (process_state_terminal(process->state) || !process->native_active) {
    turbo_mutex_unlock(&process->mutex);
    return TURBO_OK;
  }
  process->terminate_requested = 1;
  job = process->job_handle;
  child = process->process_handle;
  turbo_mutex_unlock(&process->mutex);
  if (job != NULL && job != INVALID_HANDLE_VALUE) {
    return TerminateJobObject(job, 1) ? TURBO_OK : win32_error();
  }
  return TerminateProcess(child, 1) ? TURBO_OK : win32_error();
}

static int drain_windows_pipe(turbo_process_t *process, HANDLE pipe, int is_stdout,
                              atomic_int *open) {
  char buffer[4096];
  while (*open) {
    DWORD available = 0;
    DWORD read_count = 0;
    if (!PeekNamedPipe(pipe, NULL, 0, NULL, &available, NULL)) {
      DWORD error = GetLastError();
      if (error == ERROR_BROKEN_PIPE) {
        *open = 0;
        return TURBO_OK;
      }
      return -(int)error;
    }
    if (available == 0) return TURBO_OK;
    if (!ReadFile(pipe, buffer, available < sizeof(buffer) ? available : sizeof(buffer),
                  &read_count, NULL)) {
      DWORD error = GetLastError();
      if (error == ERROR_BROKEN_PIPE) {
        *open = 0;
        return TURBO_OK;
      }
      return -(int)error;
    }
    {
      int rc = process_append_output(process, is_stdout, buffer, read_count);
      if (rc != TURBO_OK) return rc;
    }
  }
  return TURBO_OK;
}

static void process_monitor(void *arg) {
  turbo_process_t *process = (turbo_process_t *)arg;
  uint64_t started = turbo_monotonic_ms();
  turbo_process_state_t terminal = TURBO_PROCESS_EXITED;
  int error_code = TURBO_OK;
  DWORD exit_code = 1;

  for (;;) {
    int out_rc = process->stdout_open
                     ? drain_windows_pipe(process, process->stdout_read, 1, &process->stdout_open)
                     : TURBO_OK;
    int err_rc = process->stderr_open
                     ? drain_windows_pipe(process, process->stderr_read, 0, &process->stderr_open)
                     : TURBO_OK;
    DWORD wait_result;
    if (out_rc == TURBO_ERANGE || err_rc == TURBO_ERANGE) {
      terminal = TURBO_PROCESS_OUTPUT_LIMIT_EXCEEDED;
      process_platform_terminate(process);
    } else if (out_rc != TURBO_OK || err_rc != TURBO_OK) {
      terminal = TURBO_PROCESS_WAIT_FAILED;
      error_code = out_rc != TURBO_OK ? out_rc : err_rc;
      process_platform_terminate(process);
    }

    wait_result = WaitForSingleObject(process->process_handle, TURBO_PROCESS_POLL_INTERVAL_MS);
    if (wait_result == WAIT_OBJECT_0) {
      turbo_mutex_lock(&process->mutex);
      process->native_active = 0;
      turbo_mutex_unlock(&process->mutex);
      break;
    }
    if (wait_result != WAIT_TIMEOUT) {
      terminal = TURBO_PROCESS_WAIT_FAILED;
      error_code = win32_error();
      process_platform_terminate(process);
      WaitForSingleObject(process->process_handle, INFINITE);
      turbo_mutex_lock(&process->mutex);
      process->native_active = 0;
      turbo_mutex_unlock(&process->mutex);
      break;
    }
    if (terminal != TURBO_PROCESS_EXITED) continue;
    turbo_mutex_lock(&process->mutex);
    if (process->terminate_requested) terminal = TURBO_PROCESS_TERMINATED;
    turbo_mutex_unlock(&process->mutex);
    if (terminal == TURBO_PROCESS_EXITED && process->timeout_ms != 0 &&
        turbo_monotonic_ms() - started >= process->timeout_ms) {
      terminal = TURBO_PROCESS_TIMED_OUT;
      process_platform_terminate(process);
    }
  }

  if (process->stdout_open) {
    int final_rc = drain_windows_pipe(process, process->stdout_read, 1, &process->stdout_open);
    if (terminal == TURBO_PROCESS_EXITED && final_rc == TURBO_ERANGE)
      terminal = TURBO_PROCESS_OUTPUT_LIMIT_EXCEEDED;
    else if (terminal == TURBO_PROCESS_EXITED && final_rc != TURBO_OK) {
      terminal = TURBO_PROCESS_WAIT_FAILED;
      error_code = final_rc;
    }
  }
  if (process->stderr_open) {
    int final_rc = drain_windows_pipe(process, process->stderr_read, 0, &process->stderr_open);
    if (terminal == TURBO_PROCESS_EXITED && final_rc == TURBO_ERANGE)
      terminal = TURBO_PROCESS_OUTPUT_LIMIT_EXCEEDED;
    else if (terminal == TURBO_PROCESS_EXITED && final_rc != TURBO_OK) {
      terminal = TURBO_PROCESS_WAIT_FAILED;
      error_code = final_rc;
    }
  }
  close_win_handle(&process->stdout_read);
  close_win_handle(&process->stderr_read);
  if (!GetExitCodeProcess(process->process_handle, &exit_code) &&
      terminal == TURBO_PROCESS_EXITED) {
    terminal = TURBO_PROCESS_WAIT_FAILED;
    error_code = win32_error();
  }
  if (terminal == TURBO_PROCESS_EXITED) {
    turbo_mutex_lock(&process->mutex);
    if (process->terminate_requested) terminal = TURBO_PROCESS_TERMINATED;
    turbo_mutex_unlock(&process->mutex);
  }
  process_finish(process, terminal, terminal == TURBO_PROCESS_EXITED ? (int)exit_code : -1, 0,
                 error_code);
}

static int process_platform_spawn(turbo_process_t *process,
                                  const turbo_process_options_t *options) {
  SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
  STARTUPINFOEXW startup;
  PROCESS_INFORMATION info;
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
  HANDLE stdin_read = INVALID_HANDLE_VALUE;
  HANDLE stdout_write = INVALID_HANDLE_VALUE;
  HANDLE stderr_write = INVALID_HANDLE_VALUE;
  HANDLE inherited_handles[3] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
  SIZE_T attribute_size = 0;
  wchar_t *command = NULL;
  wchar_t *cwd = NULL;
  wchar_t *environment = NULL;
  DWORD flags = CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT |
                EXTENDED_STARTUPINFO_PRESENT;
  int rc;

  memset(&startup, 0, sizeof(startup));
  memset(&info, 0, sizeof(info));
  memset(&limits, 0, sizeof(limits));
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;

  rc = build_windows_command(options, &command);
  if (rc == TURBO_OK) rc = utf8_to_utf16(options->cwd, &cwd);
  if (rc == TURBO_OK) rc = build_windows_environment(options->env, &environment);
  if (rc != TURBO_OK) goto cleanup;

  if ((options->flags & TURBO_PROCESS_PIPE_STDIN) != 0U) {
    if (!CreatePipe(&stdin_read, &process->stdin_write, &security, 0) ||
        !SetHandleInformation(process->stdin_write, HANDLE_FLAG_INHERIT, 0)) {
      rc = win32_error();
      goto cleanup;
    }
    inherited_handles[0] = stdin_read;
  } else {
    rc = duplicate_standard_handle(STD_INPUT_HANDLE, GENERIC_READ, &inherited_handles[0]);
    if (rc != TURBO_OK) goto cleanup;
  }
  if ((options->flags & TURBO_PROCESS_CAPTURE_STDOUT) != 0U) {
    if (!CreatePipe(&process->stdout_read, &stdout_write, &security, 0) ||
        !SetHandleInformation(process->stdout_read, HANDLE_FLAG_INHERIT, 0)) {
      rc = win32_error();
      goto cleanup;
    }
    inherited_handles[1] = stdout_write;
    process->stdout_open = 1;
  } else {
    rc = duplicate_standard_handle(STD_OUTPUT_HANDLE, GENERIC_WRITE, &inherited_handles[1]);
    if (rc != TURBO_OK) goto cleanup;
  }
  if ((options->flags & TURBO_PROCESS_CAPTURE_STDERR) != 0U) {
    if (!CreatePipe(&process->stderr_read, &stderr_write, &security, 0) ||
        !SetHandleInformation(process->stderr_read, HANDLE_FLAG_INHERIT, 0)) {
      rc = win32_error();
      goto cleanup;
    }
    inherited_handles[2] = stderr_write;
    process->stderr_open = 1;
  } else {
    rc = duplicate_standard_handle(STD_ERROR_HANDLE, GENERIC_WRITE, &inherited_handles[2]);
    if (rc != TURBO_OK) goto cleanup;
  }

  startup.StartupInfo.hStdInput = inherited_handles[0];
  startup.StartupInfo.hStdOutput = inherited_handles[1];
  startup.StartupInfo.hStdError = inherited_handles[2];
  InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_size);
  startup.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attribute_size);
  if (!startup.lpAttributeList) {
    rc = TURBO_ENOMEM;
    goto cleanup;
  }
  if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_size) ||
      !UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                 inherited_handles, sizeof(inherited_handles), NULL, NULL)) {
    rc = win32_error();
    goto cleanup;
  }

  if (!CreateProcessW(NULL, command, NULL, NULL, TRUE, flags, environment, cwd,
                      &startup.StartupInfo, &info)) {
    rc = win32_error();
    goto cleanup;
  }
  process->process_handle = info.hProcess;
  process->job_handle = CreateJobObjectW(NULL, NULL);
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!process->job_handle ||
      !SetInformationJobObject(process->job_handle, JobObjectExtendedLimitInformation, &limits,
                               sizeof(limits)) ||
      !AssignProcessToJobObject(process->job_handle, process->process_handle)) {
    rc = win32_error();
    TerminateProcess(process->process_handle, 1);
    WaitForSingleObject(process->process_handle, INFINITE);
    goto cleanup;
  }
  if (ResumeThread(info.hThread) == (DWORD)-1) {
    rc = win32_error();
    TerminateJobObject(process->job_handle, 1);
    WaitForSingleObject(process->process_handle, INFINITE);
    goto cleanup;
  }

  process->result.pid = (int)info.dwProcessId;
  process->native_active = 1;
  process->state = TURBO_PROCESS_RUNNING;
  process->result.state = TURBO_PROCESS_RUNNING;
  rc = TURBO_OK;

cleanup:
  if (startup.lpAttributeList) {
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    free(startup.lpAttributeList);
  }
  if (inherited_handles[0] != stdin_read) close_win_handle(&inherited_handles[0]);
  if (inherited_handles[1] != stdout_write) close_win_handle(&inherited_handles[1]);
  if (inherited_handles[2] != stderr_write) close_win_handle(&inherited_handles[2]);
  close_win_handle(&stdin_read);
  close_win_handle(&stdout_write);
  close_win_handle(&stderr_write);
  if (info.hThread) CloseHandle(info.hThread);
  free(command);
  free(cwd);
  free(environment);
  return rc;
}

int turbo_process_write_stdin(turbo_process_t *process, const void *data, size_t size,
                              size_t *out_written) {
  const char *cursor = (const char *)data;
  size_t total = 0;
  int rc = TURBO_OK;
  if (!process || (size > 0 && !data)) return TURBO_EINVAL;
  if (out_written) *out_written = 0;
  turbo_mutex_lock(&process->stdin_mutex);
  if (process->stdin_write == INVALID_HANDLE_VALUE) {
    turbo_mutex_unlock(&process->stdin_mutex);
    return TURBO_EPIPE;
  }
  while (total < size) {
    DWORD chunk = (DWORD)((size - total) > UINT32_MAX ? UINT32_MAX : (size - total));
    DWORD written = 0;
    if (!WriteFile(process->stdin_write, cursor + total, chunk, &written, NULL)) {
      rc = win32_error();
      break;
    }
    if (written == 0) {
      rc = TURBO_EPIPE;
      break;
    }
    total += written;
  }
  if (out_written) *out_written = total;
  turbo_mutex_unlock(&process->stdin_mutex);
  return rc;
}

int turbo_process_close_stdin(turbo_process_t *process) {
  if (!process) return TURBO_EINVAL;
  turbo_mutex_lock(&process->stdin_mutex);
  close_win_handle(&process->stdin_write);
  turbo_mutex_unlock(&process->stdin_mutex);
  return TURBO_OK;
}

int turbo_process_is_pid_alive(int pid, bool *out_alive) {
  HANDLE handle;
  DWORD wait_result;
  DWORD wait_error = ERROR_SUCCESS;
  if (pid <= 0 || !out_alive) return TURBO_EINVAL;
  handle = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
  if (!handle) {
    DWORD error = GetLastError();
    if (error == ERROR_INVALID_PARAMETER) {
      *out_alive = false;
      return TURBO_OK;
    }
    return -(int)error;
  }
  wait_result = WaitForSingleObject(handle, 0);
  if (wait_result == WAIT_FAILED) wait_error = GetLastError();
  CloseHandle(handle);
  *out_alive = wait_result == WAIT_TIMEOUT;
  return wait_result == WAIT_FAILED ? -(int)wait_error : TURBO_OK;
}

#else

static void close_fd(int *fd) {
  if (*fd >= 0) close(*fd);
  *fd = -1;
}

static int create_pipe(int fds[2]) {
  fds[0] = -1;
  fds[1] = -1;
  return pipe(fds) == 0 ? TURBO_OK : -errno;
}

static void close_pipe(int fds[2]) {
  close_fd(&fds[0]);
  close_fd(&fds[1]);
}

static const char *environment_path(const char *const *env) {
  size_t i;
  if (env) {
    for (i = 0; env[i]; ++i) {
      if (strncmp(env[i], "PATH=", 5) == 0) return env[i] + 5;
    }
  }
  return getenv("PATH");
}

static char **build_argv(const turbo_process_options_t *options) {
  size_t count = 0;
  char **argv;
  if (options->args) {
    while (options->args[count]) {
      if (count > (SIZE_MAX / sizeof(*argv)) - 2U) return NULL;
      ++count;
    }
  }
  argv = (char **)calloc(count + 2, sizeof(*argv));
  if (!argv) return NULL;
  argv[0] = (char *)options->program;
  for (size_t i = 0; i < count; ++i)
    argv[i + 1] = (char *)options->args[i];
  return argv;
}

static size_t posix_env_key_length(const char *entry) {
  const char *equals = strchr(entry, '=');
  return equals ? (size_t)(equals - entry) : 0;
}

static int posix_env_key_equal(const char *left, const char *right) {
  size_t left_length = posix_env_key_length(left);
  size_t right_length = posix_env_key_length(right);
  return left_length != 0 && left_length == right_length && memcmp(left, right, left_length) == 0;
}

static int build_posix_environment(const char *const *overrides, char ***out_environment) {
  size_t parent_count = 0;
  size_t override_count = 0;
  size_t entry_count = 0;
  char **environment;
  size_t i;

  *out_environment = NULL;
  if (!overrides) return TURBO_OK;
  while (environ[parent_count])
    ++parent_count;
  while (overrides[override_count]) {
    if (posix_env_key_length(overrides[override_count]) == 0) return TURBO_EINVAL;
    for (size_t j = 0; j < override_count; ++j) {
      if (posix_env_key_equal(overrides[override_count], overrides[j])) return TURBO_EINVAL;
    }
    if (override_count == SIZE_MAX) return TURBO_ENOMEM;
    ++override_count;
  }
  if (override_count > SIZE_MAX - parent_count - 1U ||
      parent_count + override_count + 1U > SIZE_MAX / sizeof(*environment))
    return TURBO_ENOMEM;
  environment = (char **)calloc(parent_count + override_count + 1, sizeof(*environment));
  if (!environment) return TURBO_ENOMEM;
  for (i = 0; i < parent_count; ++i) {
    int overridden = 0;
    for (size_t j = 0; j < override_count; ++j) {
      if (posix_env_key_equal(environ[i], overrides[j])) {
        overridden = 1;
        break;
      }
    }
    if (!overridden) environment[entry_count++] = environ[i];
  }
  for (i = 0; i < override_count; ++i)
    environment[entry_count++] = (char *)overrides[i];
  *out_environment = environment;
  return TURBO_OK;
}

static char **build_program_candidates(const turbo_process_options_t *options, size_t *out_count) {
  const char *path;
  const char *cursor;
  size_t count = 1;
  size_t index = 0;
  char **candidates;
  if (strchr(options->program, '/')) {
    candidates = (char **)calloc(1, sizeof(*candidates));
    if (!candidates) return NULL;
    candidates[0] = strdup(options->program);
    if (!candidates[0]) {
      free(candidates);
      return NULL;
    }
    *out_count = 1;
    return candidates;
  }
  path = environment_path(options->env);
  if (!path || path[0] == '\0') path = "/usr/local/bin:/usr/bin:/bin";
  for (cursor = path; *cursor; ++cursor)
    if (*cursor == ':') ++count;
  candidates = (char **)calloc(count, sizeof(*candidates));
  if (!candidates) return NULL;
  cursor = path;
  while (index < count) {
    const char *end = strchr(cursor, ':');
    size_t dir_length = end ? (size_t)(end - cursor) : strlen(cursor);
    size_t program_length = strlen(options->program);
    size_t prefix_length = dir_length ? dir_length : 1U;
    size_t length;
    if (prefix_length > SIZE_MAX - program_length - 2U) goto error;
    length = prefix_length + program_length + 2U;
    candidates[index] = (char *)malloc(length);
    if (!candidates[index]) goto error;
    if (dir_length == 0) {
      candidates[index][0] = '.';
      candidates[index][1] = '/';
      memcpy(candidates[index] + 2, options->program, program_length + 1U);
    } else {
      memcpy(candidates[index], cursor, dir_length);
      candidates[index][dir_length] = '/';
      memcpy(candidates[index] + dir_length + 1U, options->program, program_length + 1U);
    }
    ++index;
    if (!end) break;
    cursor = end + 1;
  }
  *out_count = index;
  return candidates;

error:
  while (index > 0)
    free(candidates[--index]);
  free(candidates);
  return NULL;
}

static void free_candidates(char **candidates, size_t count) {
  for (size_t i = 0; i < count; ++i)
    free(candidates[i]);
  free(candidates);
}

static ssize_t write_without_sigpipe(int fd, const void *data, size_t size) {
  sigset_t blocked;
  sigset_t previous;
  sigset_t pending;
  int had_pending = 0;
  ssize_t result;

  sigemptyset(&blocked);
  sigaddset(&blocked, SIGPIPE);
  if (pthread_sigmask(SIG_BLOCK, &blocked, &previous) != 0) {
    errno = EIO;
    return -1;
  }
  if (sigpending(&pending) == 0) had_pending = sigismember(&pending, SIGPIPE);
  result = write(fd, data, size);
  if (result < 0 && errno == EPIPE && !had_pending) {
    struct timespec timeout = {0, 0};
    while (sigtimedwait(&blocked, NULL, &timeout) < 0 && errno == EINTR) {
    }
    errno = EPIPE;
  }
  pthread_sigmask(SIG_SETMASK, &previous, NULL);
  return result;
}

static void child_exec(char **candidates, size_t candidate_count, char *const argv[],
                       char *const envp[], int error_fd) {
  int last_error = ENOENT;
  for (size_t i = 0; i < candidate_count; ++i) {
    execve(candidates[i], argv, envp);
    if (errno != ENOENT && errno != ENOTDIR) last_error = errno;
  }
  (void)write(error_fd, &last_error, sizeof(last_error));
  _exit(127);
}

static int process_platform_terminate(turbo_process_t *process) {
  pid_t pid;
  int rc;
  turbo_mutex_lock(&process->mutex);
  if (process_state_terminal(process->state) || !process->native_active) {
    turbo_mutex_unlock(&process->mutex);
    return TURBO_OK;
  }
  process->terminate_requested = 1;
  pid = process->pid;
  if (kill(-pid, SIGKILL) == 0 || errno == ESRCH) {
    turbo_mutex_unlock(&process->mutex);
    return TURBO_OK;
  }
  rc = kill(pid, SIGKILL) == 0 || errno == ESRCH ? TURBO_OK : -errno;
  turbo_mutex_unlock(&process->mutex);
  return rc;
}

static int drain_posix_fd(turbo_process_t *process, int fd, int is_stdout, atomic_int *open) {
  char buffer[4096];
  while (*open) {
    ssize_t count = read(fd, buffer, sizeof(buffer));
    if (count > 0) {
      int rc = process_append_output(process, is_stdout, buffer, (size_t)count);
      if (rc != TURBO_OK) return rc;
      continue;
    }
    if (count == 0) {
      *open = 0;
      return TURBO_OK;
    }
    if (errno == EINTR) continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return TURBO_OK;
    return -errno;
  }
  return TURBO_OK;
}

static void process_monitor(void *arg) {
  turbo_process_t *process = (turbo_process_t *)arg;
  uint64_t started = turbo_monotonic_ms();
  turbo_process_state_t terminal = TURBO_PROCESS_EXITED;
  int error_code = TURBO_OK;
  int status = 0;

  for (;;) {
    int out_rc = process->stdout_open
                     ? drain_posix_fd(process, process->stdout_fd, 1, &process->stdout_open)
                     : TURBO_OK;
    int err_rc = process->stderr_open
                     ? drain_posix_fd(process, process->stderr_fd, 0, &process->stderr_open)
                     : TURBO_OK;
    pid_t waited;
    if (out_rc == TURBO_ERANGE || err_rc == TURBO_ERANGE) {
      terminal = TURBO_PROCESS_OUTPUT_LIMIT_EXCEEDED;
      process_platform_terminate(process);
    } else if (out_rc != TURBO_OK || err_rc != TURBO_OK) {
      terminal = TURBO_PROCESS_WAIT_FAILED;
      error_code = out_rc != TURBO_OK ? out_rc : err_rc;
      process_platform_terminate(process);
    }

    turbo_mutex_lock(&process->mutex);
    do {
      waited = waitpid(process->pid, &status, WNOHANG);
    } while (waited < 0 && errno == EINTR);
    if (waited == process->pid || waited < 0) process->native_active = 0;
    turbo_mutex_unlock(&process->mutex);
    if (waited == process->pid) break;
    if (waited < 0) {
      terminal = TURBO_PROCESS_WAIT_FAILED;
      error_code = -errno;
      process_platform_terminate(process);
      do {
        waited = waitpid(process->pid, &status, 0);
      } while (waited < 0 && errno == EINTR);
      break;
    }
    if (terminal == TURBO_PROCESS_EXITED) {
      turbo_mutex_lock(&process->mutex);
      if (process->terminate_requested) terminal = TURBO_PROCESS_TERMINATED;
      turbo_mutex_unlock(&process->mutex);
      if (terminal == TURBO_PROCESS_EXITED && process->timeout_ms != 0 &&
          turbo_monotonic_ms() - started >= process->timeout_ms) {
        terminal = TURBO_PROCESS_TIMED_OUT;
        process_platform_terminate(process);
      }
    }
    turbo_sleep_ms(TURBO_PROCESS_POLL_INTERVAL_MS);
  }

  if (process->stdout_open) {
    int final_rc = drain_posix_fd(process, process->stdout_fd, 1, &process->stdout_open);
    if (terminal == TURBO_PROCESS_EXITED && final_rc == TURBO_ERANGE)
      terminal = TURBO_PROCESS_OUTPUT_LIMIT_EXCEEDED;
    else if (terminal == TURBO_PROCESS_EXITED && final_rc != TURBO_OK) {
      terminal = TURBO_PROCESS_WAIT_FAILED;
      error_code = final_rc;
    }
  }
  if (process->stderr_open) {
    int final_rc = drain_posix_fd(process, process->stderr_fd, 0, &process->stderr_open);
    if (terminal == TURBO_PROCESS_EXITED && final_rc == TURBO_ERANGE)
      terminal = TURBO_PROCESS_OUTPUT_LIMIT_EXCEEDED;
    else if (terminal == TURBO_PROCESS_EXITED && final_rc != TURBO_OK) {
      terminal = TURBO_PROCESS_WAIT_FAILED;
      error_code = final_rc;
    }
  }
  close_fd(&process->stdout_fd);
  close_fd(&process->stderr_fd);
  if (terminal == TURBO_PROCESS_EXITED) {
    turbo_mutex_lock(&process->mutex);
    if (process->terminate_requested) terminal = TURBO_PROCESS_TERMINATED;
    turbo_mutex_unlock(&process->mutex);
  }
  if (terminal == TURBO_PROCESS_EXITED && WIFSIGNALED(status)) terminal = TURBO_PROCESS_SIGNALED;
  process_finish(process, terminal,
                 terminal == TURBO_PROCESS_EXITED && WIFEXITED(status) ? WEXITSTATUS(status)
                 : terminal == TURBO_PROCESS_SIGNALED                  ? 128 + WTERMSIG(status)
                                                                       : -1,
                 WIFSIGNALED(status) ? WTERMSIG(status) : 0, error_code);
}

static int process_platform_spawn(turbo_process_t *process,
                                  const turbo_process_options_t *options) {
  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  int exec_pipe[2] = {-1, -1};
  char **argv = NULL;
  char **environment = NULL;
  char **candidates = NULL;
  size_t candidate_count = 0;
  pid_t pid;
  int child_error = 0;
  ssize_t error_size;
  int rc = TURBO_OK;

  if ((options->flags & TURBO_PROCESS_PIPE_STDIN) != 0U &&
      (rc = create_pipe(stdin_pipe)) != TURBO_OK)
    goto cleanup;
  if ((options->flags & TURBO_PROCESS_CAPTURE_STDOUT) != 0U &&
      (rc = create_pipe(stdout_pipe)) != TURBO_OK)
    goto cleanup;
  if ((options->flags & TURBO_PROCESS_CAPTURE_STDERR) != 0U &&
      (rc = create_pipe(stderr_pipe)) != TURBO_OK)
    goto cleanup;
  if ((rc = create_pipe(exec_pipe)) != TURBO_OK) goto cleanup;
  if (fcntl(exec_pipe[1], F_SETFD, FD_CLOEXEC) != 0) {
    rc = -errno;
    goto cleanup;
  }
  argv = build_argv(options);
  rc = build_posix_environment(options->env, &environment);
  if (rc != TURBO_OK) goto cleanup;
  candidates = build_program_candidates(options, &candidate_count);
  if (!argv || !candidates) {
    rc = TURBO_ENOMEM;
    goto cleanup;
  }

  pid = fork();
  if (pid < 0) {
    rc = -errno;
    goto cleanup;
  }
  if (pid == 0) {
    close_fd(&exec_pipe[0]);
    if (setpgid(0, 0) != 0 ||
        ((options->flags & TURBO_PROCESS_PIPE_STDIN) != 0U &&
         dup2(stdin_pipe[0], STDIN_FILENO) < 0) ||
        ((options->flags & TURBO_PROCESS_CAPTURE_STDOUT) != 0U &&
         dup2(stdout_pipe[1], STDOUT_FILENO) < 0) ||
        ((options->flags & TURBO_PROCESS_CAPTURE_STDERR) != 0U &&
         dup2(stderr_pipe[1], STDERR_FILENO) < 0) ||
        (options->cwd && chdir(options->cwd) != 0)) {
      child_error = errno;
      (void)write(exec_pipe[1], &child_error, sizeof(child_error));
      _exit(127);
    }
    close_pipe(stdin_pipe);
    close_pipe(stdout_pipe);
    close_pipe(stderr_pipe);
    child_exec(candidates, candidate_count, argv, environment ? environment : environ,
               exec_pipe[1]);
  }

  process->pid = pid;
  setpgid(pid, pid);
  close_fd(&exec_pipe[1]);
  close_fd(&stdin_pipe[0]);
  close_fd(&stdout_pipe[1]);
  close_fd(&stderr_pipe[1]);
  do {
    error_size = read(exec_pipe[0], &child_error, sizeof(child_error));
  } while (error_size < 0 && errno == EINTR);
  close_fd(&exec_pipe[0]);
  if (error_size > 0) {
    int status;
    do {
      rc = waitpid(pid, &status, 0);
    } while (rc < 0 && errno == EINTR);
    rc = -child_error;
    goto cleanup;
  }
  if (error_size < 0) {
    rc = -errno;
    kill(-pid, SIGKILL);
    waitpid(pid, NULL, 0);
    goto cleanup;
  }

  process->stdin_fd = stdin_pipe[1];
  stdin_pipe[1] = -1;
  process->stdout_fd = stdout_pipe[0];
  stdout_pipe[0] = -1;
  process->stderr_fd = stderr_pipe[0];
  stderr_pipe[0] = -1;
  if (process->stdout_fd >= 0) {
    fcntl(process->stdout_fd, F_SETFL, fcntl(process->stdout_fd, F_GETFL, 0) | O_NONBLOCK);
    process->stdout_open = 1;
  }
  if (process->stderr_fd >= 0) {
    fcntl(process->stderr_fd, F_SETFL, fcntl(process->stderr_fd, F_GETFL, 0) | O_NONBLOCK);
    process->stderr_open = 1;
  }
  process->result.pid = (int)pid;
  process->native_active = 1;
  process->state = TURBO_PROCESS_RUNNING;
  process->result.state = TURBO_PROCESS_RUNNING;
  rc = TURBO_OK;

cleanup:
  close_pipe(stdin_pipe);
  close_pipe(stdout_pipe);
  close_pipe(stderr_pipe);
  close_pipe(exec_pipe);
  free(argv);
  free(environment);
  free_candidates(candidates, candidate_count);
  return rc;
}

int turbo_process_write_stdin(turbo_process_t *process, const void *data, size_t size,
                              size_t *out_written) {
  const char *cursor = (const char *)data;
  size_t total = 0;
  int rc = TURBO_OK;
  if (!process || (size > 0 && !data)) return TURBO_EINVAL;
  if (out_written) *out_written = 0;
  turbo_mutex_lock(&process->stdin_mutex);
  if (process->stdin_fd < 0) {
    turbo_mutex_unlock(&process->stdin_mutex);
    return TURBO_EPIPE;
  }
  while (total < size) {
    ssize_t written = write_without_sigpipe(process->stdin_fd, cursor + total, size - total);
    if (written > 0) {
      total += (size_t)written;
      continue;
    }
    if (written < 0 && errno == EINTR) continue;
    rc = written < 0 ? -errno : TURBO_EPIPE;
    break;
  }
  if (out_written) *out_written = total;
  turbo_mutex_unlock(&process->stdin_mutex);
  return rc;
}

int turbo_process_close_stdin(turbo_process_t *process) {
  if (!process) return TURBO_EINVAL;
  turbo_mutex_lock(&process->stdin_mutex);
  close_fd(&process->stdin_fd);
  turbo_mutex_unlock(&process->stdin_mutex);
  return TURBO_OK;
}

int turbo_process_is_pid_alive(int pid, bool *out_alive) {
  if (pid <= 0 || !out_alive) return TURBO_EINVAL;
  if (kill((pid_t)pid, 0) == 0 || errno == EPERM) {
    *out_alive = true;
    return TURBO_OK;
  }
  if (errno == ESRCH) {
    *out_alive = false;
    return TURBO_OK;
  }
  return -errno;
}

#endif

int turbo_process_spawn(const turbo_process_options_t *options, turbo_process_t **out_process) {
  turbo_process_t *process;
  int rc;
  if (!out_process) return TURBO_EINVAL;
  *out_process = NULL;
  rc = process_validate_options(options);
  if (rc != TURBO_OK) return rc;
  process = (turbo_process_t *)calloc(1, sizeof(*process));
  if (!process) return TURBO_ENOMEM;
  rc = process_init(process, options);
  if (rc != TURBO_OK) goto error;
  rc = process_platform_spawn(process, options);
  if (rc != TURBO_OK) goto error;
  rc = turbo_thread_create(&process->monitor_thread, process_monitor, process);
  if (rc != TURBO_OK) {
    process_platform_terminate(process);
#ifdef _WIN32
    WaitForSingleObject(process->process_handle, INFINITE);
#else
    waitpid(process->pid, NULL, 0);
#endif
    goto error;
  }
  process->monitor_started = 1;
  *out_process = process;
  return TURBO_OK;

error:
  turbo_process_close_stdin(process);
#ifdef _WIN32
  close_win_handle(&process->stdout_read);
  close_win_handle(&process->stderr_read);
  close_win_handle(&process->job_handle);
  close_win_handle(&process->process_handle);
#else
  close_fd(&process->stdout_fd);
  close_fd(&process->stderr_fd);
#endif
  tstr_free(process->stdout_data);
  tstr_free(process->stderr_data);
  turbo_mutex_destroy(&process->stdin_mutex);
  turbo_cond_destroy(&process->changed);
  turbo_mutex_destroy(&process->mutex);
  free(process);
  return rc;
}

int turbo_process_terminate(turbo_process_t *process) {
  if (!process) return TURBO_EINVAL;
  return process_platform_terminate(process);
}

void turbo_process_destroy(turbo_process_t *process) {
  if (!process) return;
  turbo_process_terminate(process);
  turbo_process_close_stdin(process);
  process_join_monitor(process);
#ifdef _WIN32
  close_win_handle(&process->job_handle);
  close_win_handle(&process->process_handle);
#else
  close_fd(&process->stdout_fd);
  close_fd(&process->stderr_fd);
#endif
  tstr_free(process->stdout_data);
  tstr_free(process->stderr_data);
  turbo_mutex_destroy(&process->stdin_mutex);
  turbo_cond_destroy(&process->changed);
  turbo_mutex_destroy(&process->mutex);
  free(process);
}
