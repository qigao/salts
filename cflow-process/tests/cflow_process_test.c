#include <cflow/process.h>

#include <turbo/clock.h>
#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include "tinytest.h"

#include <string.h>

#if defined(_WIN32)
  #include <windows.h>
#elif defined(__linux__)
  #include <dirent.h>
#endif

enum { PROCESS_TEST_COMPLETION_CAPACITY = 16 };

typedef struct process_completion_probe {
  cflow_io_request_id request_ids[PROCESS_TEST_COMPLETION_CAPACITY];
  cflow_process_stream streams[PROCESS_TEST_COMPLETION_CAPACITY];
  cflow_io_completion completions[PROCESS_TEST_COMPLETION_CAPACITY];
  size_t count;
} process_completion_probe;

static void process_completion(void *user, cflow_io_request_id request_id,
                               cflow_io_lease_id lease_id, cflow_process_stream stream,
                               const cflow_io_completion *completion) {
  process_completion_probe *probe = (process_completion_probe *)user;
  (void)lease_id;
  if (probe->count < PROCESS_TEST_COMPLETION_CAPACITY) {
    probe->request_ids[probe->count] = request_id;
    probe->streams[probe->count] = stream;
    probe->completions[probe->count] = *completion;
    ++probe->count;
  }
}

static void init_output_options(turbo_process_options_t *options) {
#ifdef _WIN32
  static const char *args[] = {"-NoProfile", "-Command",
                               "$o=[Text.Encoding]::ASCII.GetBytes('partial-output');"
                               "[Console]::OpenStandardOutput().Write($o,0,$o.Length);"
                               "$e=[Text.Encoding]::ASCII.GetBytes('stderr-output');"
                               "[Console]::OpenStandardError().Write($e,0,$e.Length)",
                               NULL};
  turbo_process_options_init(options);
  options->program = "powershell.exe";
  options->args = args;
#else
  static const char *args[] = {"-c", "printf partial-output; printf stderr-output >&2", NULL};
  turbo_process_options_init(options);
  options->program = "/bin/sh";
  options->args = args;
#endif
  options->flags = 0u;
}

#if defined(_WIN32)
static bool process_resource_count(size_t *out) {
  DWORD count = 0u;
  if (out == NULL || !GetProcessHandleCount(GetCurrentProcess(), &count)) return false;
  *out = (size_t)count;
  return true;
}
#elif defined(__linux__)
static bool process_resource_count(size_t *out) {
  DIR *directory;
  struct dirent *entry;
  size_t count = 0u;
  if (out == NULL) return false;
  directory = opendir("/proc/self/fd");
  if (directory == NULL) return false;
  while ((entry = readdir(directory)) != NULL) {
    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) ++count;
  }
  (void)closedir(directory);
  *out = count;
  return true;
}
#endif

static int drive_until(cflow_process *process, process_completion_probe *probe, size_t expected) {
  const uint64_t started = turbo_hrtime();
  while (probe->count < expected) {
    size_t progressed = 0u;
    int status = cflow_process_run_ready(process, 64u, &progressed);
    if (status != TURBO_OK) return status;
    if (turbo_hrtime() - started > UINT64_C(5000000000)) return TURBO_ETIMEDOUT;
    if (progressed == 0u) turbo_thread_yield();
  }
  return TURBO_OK;
}

static const cflow_io_completion *completion_for_stream(const process_completion_probe *probe,
                                                        cflow_process_stream stream) {
  size_t index;
  for (index = 0u; index < probe->count; ++index) {
    if (probe->streams[index] == stream) return &probe->completions[index];
  }
  return NULL;
}

static void init_process_options(turbo_process_options_t *options, bool echo_stdin) {
#ifdef _WIN32
  static const char *echo_args[] = {
      "-NoProfile", "-Command",
      "[Console]::OpenStandardInput().CopyTo([Console]::OpenStandardOutput())", NULL};
  static const char *exit_args[] = {"-NoProfile", "-Command", "exit 0", NULL};
  turbo_process_options_init(options);
  options->program = "powershell.exe";
  options->args = echo_stdin ? echo_args : exit_args;
#else
  static const char *args[] = {NULL};
  turbo_process_options_init(options);
  options->program = echo_stdin ? "/bin/cat" : "/usr/bin/true";
  options->args = args;
#endif
  options->flags = 0u;
}

static cflow_process_config process_test_config(process_completion_probe *probe) {
  cflow_process_config config = {0};
#ifdef _WIN32
  config.backend_kind = CFLOW_IO_NATIVE_IOCP;
#else
  config.backend_kind = CFLOW_IO_NATIVE_POLL;
#endif
  config.request_capacity = 4u;
  config.command_capacity = 4u;
  config.completion_batch_capacity = 4u;
  config.completion = process_completion;
  config.completion_user = probe;
  return config;
}

static int close_and_drain(cflow_process *process) {
  const uint64_t started = turbo_hrtime();
  int status = cflow_process_close(process);
  if (status != TURBO_OK) return status;
  while (!cflow_process_is_quiescent(process)) {
    size_t progressed = 0u;
    status = cflow_process_run_ready(process, 64u, &progressed);
    if (status != TURBO_OK) return status;
    if (turbo_hrtime() - started > UINT64_C(5000000000)) return TURBO_ETIMEDOUT;
    if (progressed == 0u) turbo_sleep_ms(1u);
  }
  return cflow_process_destroy(process);
}

spec("CFlow subprocess adapter") {
  it("moves bytes through bounded asynchronous standard streams") {
    static const char payload[] = "cflow-process-payload";
    turbo_process_options_t options;
    cflow_process_config config = {0};
    cflow_process process = {0};
    cflow_process_submit_result read_submitted;
    cflow_process_submit_result write_submitted;
    cflow_process_stats stats;
    process_completion_probe probe = {0};
    turbo_process_result_t result;
    char output[sizeof(payload)] = {0};
    const uint64_t started = turbo_hrtime();
    int drive_status;
    int poll_status;
    int run_status = TURBO_OK;
    const cflow_io_completion *read_completion;
    const cflow_io_completion *write_completion;

    init_process_options(&options, true);
    config = process_test_config(&probe);

    check_equal(cflow_process_start(&process, &options, &config), TURBO_OK);
    check_true(cflow_process_get_stats(&process, &stats));
    check_equal(stats.io.request_capacity, config.request_capacity);
    check_true(stats.stdin_open);
    check_true(stats.stdout_open);
    check_true(stats.stderr_open);
    check_false(stats.close_requested);
    read_submitted = cflow_process_try_read_stdout(&process, 1u, output, sizeof(payload) - 1u);
    write_submitted = cflow_process_try_write_stdin(&process, 2u, payload, sizeof(payload) - 1u);
    check_equal(read_submitted.status, CFLOW_PROCESS_SUBMIT_ACCEPTED);
    check_equal(write_submitted.status, CFLOW_PROCESS_SUBMIT_ACCEPTED);
    check_true(cflow_process_get_stats(&process, &stats));
    check_equal(stats.io.active_requests, (size_t)2u);
    drive_status = drive_until(&process, &probe, 2u);
    if (drive_status != TURBO_OK) {
      int diagnostic_poll_status = cflow_process_poll(&process, &result);
      info("drive status=%d completion_count=%zu first_stream=%d first_kind=%d first_error=%d "
           "poll=%d process_state=%d exit=%d process_error=%d",
           drive_status, probe.count, probe.count > 0u ? (int)probe.streams[0] : -1,
           probe.count > 0u ? (int)probe.completions[0].kind : -1,
           probe.count > 0u ? probe.completions[0].error : 0, diagnostic_poll_status,
           diagnostic_poll_status == TURBO_OK ? (int)result.state : -1,
           diagnostic_poll_status == TURBO_OK ? result.exit_code : -1,
           diagnostic_poll_status == TURBO_OK ? result.error_code : 0);
    }
    check_equal(drive_status, TURBO_OK);
    read_completion = completion_for_stream(&probe, CFLOW_PROCESS_STDOUT);
    write_completion = completion_for_stream(&probe, CFLOW_PROCESS_STDIN);
    check_not_null(read_completion);
    check_not_null(write_completion);
    check_equal(read_completion->kind, CFLOW_IO_COMPLETION_OK);
    check_equal(read_completion->bytes, sizeof(payload) - 1u);
    check_equal(write_completion->kind, CFLOW_IO_COMPLETION_OK);
    check_equal(write_completion->bytes, sizeof(payload) - 1u);
    check_equal(cflow_process_close_stdin(&process), TURBO_OK);
    while ((poll_status = cflow_process_poll(&process, &result)) == TURBO_EBUSY &&
           turbo_hrtime() - started <= UINT64_C(5000000000)) {
      size_t progressed = 0u;
      run_status = cflow_process_run_ready(&process, 64u, &progressed);
      if (run_status != TURBO_OK) break;
      if (progressed == 0u) turbo_sleep_ms(1u);
    }
    check_equal(run_status, TURBO_OK);
    check_equal(poll_status, TURBO_OK);
    check_equal(result.state, TURBO_PROCESS_EXITED);
    check_equal(result.exit_code, 0);
    check_equal(output, payload, sizeof(payload) - 1u);
    check_equal(close_and_drain(&process), TURBO_OK);
  }

  it("publishes stdout EOF after a child exits without output") {
    turbo_process_options_t options;
    cflow_process process = {0};
    process_completion_probe probe = {0};
    cflow_process_config config = process_test_config(&probe);
    cflow_process_submit_result submitted;
    const cflow_io_completion *completion;
    char byte = 0;

    init_process_options(&options, false);
    check_equal(cflow_process_start(&process, &options, &config), TURBO_OK);
    submitted = cflow_process_try_read_stdout(&process, 1u, &byte, sizeof(byte));
    check_equal(submitted.status, CFLOW_PROCESS_SUBMIT_ACCEPTED);
    check_equal(drive_until(&process, &probe, 1u), TURBO_OK);
    completion = completion_for_stream(&probe, CFLOW_PROCESS_STDOUT);
    check_not_null(completion);
    check_equal(completion->kind, CFLOW_IO_COMPLETION_EOF);
    check_equal(completion->bytes, (size_t)0u);
    check_equal(close_and_drain(&process), TURBO_OK);
  }

  it("preserves partial stdout and stderr bytes before independent EOF") {
    static const char stdout_payload[] = "partial-output";
    static const char stderr_payload[] = "stderr-output";
    turbo_process_options_t options;
    cflow_process process = {0};
    process_completion_probe probe = {0};
    cflow_process_config config = process_test_config(&probe);
    cflow_process_submit_result stdout_submitted;
    cflow_process_submit_result stderr_submitted;
    const cflow_io_completion *stdout_completion;
    const cflow_io_completion *stderr_completion;
    char stdout_buffer[64] = {0};
    char stderr_buffer[64] = {0};
    char eof_bytes[2] = {0};

    init_output_options(&options);
    check_equal(cflow_process_start(&process, &options, &config), TURBO_OK);
    stdout_submitted =
        cflow_process_try_read_stdout(&process, 1u, stdout_buffer, sizeof(stdout_buffer));
    stderr_submitted =
        cflow_process_try_read_stderr(&process, 2u, stderr_buffer, sizeof(stderr_buffer));
    check_equal(stdout_submitted.status, CFLOW_PROCESS_SUBMIT_ACCEPTED);
    check_equal(stderr_submitted.status, CFLOW_PROCESS_SUBMIT_ACCEPTED);
    check_equal(drive_until(&process, &probe, 2u), TURBO_OK);
    stdout_completion = completion_for_stream(&probe, CFLOW_PROCESS_STDOUT);
    stderr_completion = completion_for_stream(&probe, CFLOW_PROCESS_STDERR);
    check_not_null(stdout_completion);
    check_not_null(stderr_completion);
    check_equal(stdout_completion->kind, CFLOW_IO_COMPLETION_OK);
    check_equal(stdout_completion->bytes, sizeof(stdout_payload) - 1u);
    check_less(stdout_completion->bytes, sizeof(stdout_buffer));
    check_equal(stderr_completion->kind, CFLOW_IO_COMPLETION_OK);
    check_equal(stderr_completion->bytes, sizeof(stderr_payload) - 1u);
    check_equal(stdout_buffer, stdout_payload, sizeof(stdout_payload) - 1u);
    check_equal(stderr_buffer, stderr_payload, sizeof(stderr_payload) - 1u);

    memset(&probe, 0, sizeof(probe));
    stdout_submitted = cflow_process_try_read_stdout(&process, 3u, &eof_bytes[0], 1u);
    stderr_submitted = cflow_process_try_read_stderr(&process, 4u, &eof_bytes[1], 1u);
    check_equal(stdout_submitted.status, CFLOW_PROCESS_SUBMIT_ACCEPTED);
    check_equal(stderr_submitted.status, CFLOW_PROCESS_SUBMIT_ACCEPTED);
    check_equal(drive_until(&process, &probe, 2u), TURBO_OK);
    stdout_completion = completion_for_stream(&probe, CFLOW_PROCESS_STDOUT);
    stderr_completion = completion_for_stream(&probe, CFLOW_PROCESS_STDERR);
    check_not_null(stdout_completion);
    check_not_null(stderr_completion);
    check_equal(stdout_completion->kind, CFLOW_IO_COMPLETION_EOF);
    check_equal(stderr_completion->kind, CFLOW_IO_COMPLETION_EOF);
    check_equal(close_and_drain(&process), TURBO_OK);
  }

  it("waits for authoritative cancellation of a pending stdout read") {
    turbo_process_options_t options;
    cflow_process process = {0};
    process_completion_probe probe = {0};
    cflow_process_config config = process_test_config(&probe);
    cflow_process_submit_result submitted;
    const cflow_io_completion *completion;
    size_t progressed = 0u;
    char byte = 0;

    init_process_options(&options, true);
    check_equal(cflow_process_start(&process, &options, &config), TURBO_OK);
    submitted = cflow_process_try_read_stdout(&process, 1u, &byte, sizeof(byte));
    check_equal(submitted.status, CFLOW_PROCESS_SUBMIT_ACCEPTED);
    check_equal(cflow_process_run_ready(&process, 64u, &progressed), TURBO_OK);
    check_greater(progressed, (size_t)0u);
    check_equal(cflow_process_try_cancel(&process, submitted.request_id), CFLOW_IO_CANCEL_ACCEPTED);
    check_equal(drive_until(&process, &probe, 1u), TURBO_OK);
    completion = completion_for_stream(&probe, CFLOW_PROCESS_STDOUT);
    check_not_null(completion);
    check_equal(completion->kind, CFLOW_IO_COMPLETION_CANCELLED);
    check_equal(completion->bytes, (size_t)0u);
    check_equal(close_and_drain(&process), TURBO_OK);
  }

  it("settles queued cancellation for all three standard streams exactly once") {
    static const char payload[] = "cancelled-input";
    turbo_process_options_t options;
    cflow_process process = {0};
    process_completion_probe probe = {0};
    cflow_process_config config = process_test_config(&probe);
    cflow_process_submit_result submitted[3];
    char stdout_byte = 0;
    char stderr_byte = 0;
    size_t index;

    init_process_options(&options, true);
    config.command_capacity = 8u;
    check_equal(cflow_process_start(&process, &options, &config), TURBO_OK);
    submitted[0] = cflow_process_try_write_stdin(&process, 1u, payload, sizeof(payload) - 1u);
    submitted[1] = cflow_process_try_read_stdout(&process, 2u, &stdout_byte, sizeof(stdout_byte));
    submitted[2] = cflow_process_try_read_stderr(&process, 3u, &stderr_byte, sizeof(stderr_byte));
    for (index = 0u; index < 3u; ++index) {
      check_equal(submitted[index].status, CFLOW_PROCESS_SUBMIT_ACCEPTED);
      check_equal(cflow_process_try_cancel(&process, submitted[index].request_id),
                  CFLOW_IO_CANCEL_ACCEPTED);
    }
    check_equal(drive_until(&process, &probe, 3u), TURBO_OK);
    check_equal(probe.count, (size_t)3u);
    for (index = 0u; index < probe.count; ++index)
      check_equal(probe.completions[index].kind, CFLOW_IO_COMPLETION_CANCELLED);
    check_equal(close_and_drain(&process), TURBO_OK);
  }

#if defined(_WIN32) || defined(__linux__)
  it("releases every adapter-owned process pipe and runtime resource") {
    turbo_process_options_t options;
    cflow_process process = {0};
    process_completion_probe probe = {0};
    cflow_process_config config = process_test_config(&probe);
    size_t before = 0u;
    size_t after = 0u;
    size_t iteration;

    check_true(process_resource_count(&before));
    turbo_process_options_init(&options);
    options.program = "cflow-process-missing-executable-97531";
    options.flags = 0u;
    check_less(cflow_process_start(&process, &options, &config), TURBO_OK);
    check_null(process.impl);
    check_true(process_resource_count(&after));
    check_equal(after, before);
    for (iteration = 0u; iteration < 3u; ++iteration) {
      memset(&probe, 0, sizeof(probe));
      init_process_options(&options, false);
      check_equal(cflow_process_start(&process, &options, &config), TURBO_OK);
      check_equal(close_and_drain(&process), TURBO_OK);
    }
    check_true(process_resource_count(&after));
    check_equal(after, before);
  }
#endif
}
