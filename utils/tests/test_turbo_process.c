#include "tinytest.h"
#include "turbo_error.h"
#include "turbo_fs.h"
#include "turbo_process.h"
#include "turbo_thread.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #define TEST_SHELL "cmd.exe"
#else
  #define TEST_SHELL "/bin/sh"
#endif

static void init_shell_options(turbo_process_options_t *options, const char *command) {
#ifdef _WIN32
  static const char *args[5];
  args[0] = "/d";
  args[1] = "/s";
  args[2] = "/c";
  args[3] = command;
  args[4] = NULL;
#else
  static const char *args[3];
  args[0] = "-c";
  args[1] = command;
  args[2] = NULL;
#endif
  turbo_process_options_init(options);
  options->program = TEST_SHELL;
  options->args = args;
}

static int read_all(turbo_process_t *process, int stdout_stream, char *buffer, size_t capacity,
                    size_t *out_size) {
  size_t total = 0;
  int rc = TURBO_OK;
  while (total < capacity - 1) {
    size_t count = 0;
    rc = stdout_stream
             ? turbo_process_read_stdout(process, buffer + total, capacity - 1 - total, &count)
             : turbo_process_read_stderr(process, buffer + total, capacity - 1 - total, &count);
    total += count;
    if (rc == TURBO_EOF) break;
    if (rc != TURBO_OK || count == 0) break;
  }
  buffer[total] = '\0';
  *out_size = total;
  return rc;
}

static int path_exists(const char *path) {
  return turbo_fs_access(path, TURBO_FS_ACCESS_EXISTS) == TURBO_OK;
}

spec("turbo_process") {
  group("configuration") {
    it("initializes safe defaults and rejects an empty program") {
      turbo_process_options_t options;
      turbo_process_t *process = (turbo_process_t *)1;

      turbo_process_options_init(&options);
      check_int_eq((int)options.flags, TURBO_PROCESS_CAPTURE_STDOUT | TURBO_PROCESS_CAPTURE_STDERR);
      check_size_eq(options.max_output_bytes, TURBO_PROCESS_DEFAULT_MAX_OUTPUT_BYTES);
      check_int_eq(turbo_process_spawn(&options, &process), TURBO_EINVAL);
      check_null(process);
    }

    it("reports a missing executable during spawn") {
      turbo_process_options_t options;
      turbo_process_t *process = NULL;

      turbo_process_options_init(&options);
      options.program = "turbo_process_missing_executable_97531";
      check_int_lt(turbo_process_spawn(&options, &process), 0);
      check_null(process);
    }

    it("rejects malformed and duplicate environment overrides") {
      turbo_process_options_t options;
      turbo_process_t *process = NULL;
      const char *malformed[] = {"MISSING_EQUALS", NULL};
      const char *duplicate[] = {"TURBO_DUPLICATE=first", "TURBO_DUPLICATE=second", NULL};

      turbo_process_options_init(&options);
      options.program = TEST_SHELL;
      options.env = malformed;
      check_int_eq(turbo_process_spawn(&options, &process), TURBO_EINVAL);
      check_null(process);
      options.env = duplicate;
      check_int_eq(turbo_process_spawn(&options, &process), TURBO_EINVAL);
      check_null(process);
    }
  }

  group("execution") {
    it("captures stdout stderr and the exit code") {
      turbo_process_options_t options;
      turbo_process_t *process = NULL;
      turbo_process_result_t result;
      char stdout_data[128];
      char stderr_data[128];
      size_t stdout_size = 0;
      size_t stderr_size = 0;
#ifdef _WIN32
      init_shell_options(&options, "echo stdout-line & echo stderr-line 1>&2 & exit /b 7");
#else
      init_shell_options(&options, "printf stdout-line; printf stderr-line >&2; exit 7");
#endif

      check_int_eq(turbo_process_spawn(&options, &process), TURBO_OK);
      check_not_null(process);
      check_int_gt(turbo_process_pid(process), 0);
      check_int_eq(turbo_process_wait(process, &result), TURBO_OK);
      check_int_eq(result.state, TURBO_PROCESS_EXITED);
      check_int_eq(result.exit_code, 7);
      check_int_eq(read_all(process, 1, stdout_data, sizeof(stdout_data), &stdout_size), TURBO_EOF);
      check_int_eq(read_all(process, 0, stderr_data, sizeof(stderr_data), &stderr_size), TURBO_EOF);
      check_str_contains(stdout_data, "stdout-line");
      check_str_contains(stderr_data, "stderr-line");
      turbo_process_destroy(process);
    }

    it("passes argv environment and working directory without a shell wrapper") {
      turbo_process_options_t options;
      turbo_process_t *process = NULL;
      turbo_process_result_t result;
      char *directory = tt_make_temp_dir("turbo-process-cwd");
      char output[1024];
      size_t output_size = 0;
#ifdef _WIN32
      char script[512];
#endif
      const char *env[] = {"TURBO_PROCESS_VALUE=env-value",
#ifdef _WIN32
                           "PATH=C:\\Windows\\System32;C:\\Windows",
#else
                           "PATH=/usr/local/bin:/usr/bin:/bin",
#endif
                           NULL};
#ifdef _WIN32
      const char *args[] = {"-NoProfile", "-File", script, "alpha beta", NULL};
      turbo_process_options_init(&options);
      options.program = "powershell.exe";
#else
      const char *args[] = {"-c", "printf '%s|%s|%s' \"$1\" \"$TURBO_PROCESS_VALUE\" \"$PWD\"",
                            "probe", "alpha beta", NULL};
      turbo_process_options_init(&options);
      options.program = "/bin/sh";
#endif
      options.args = args;
      options.env = env;
      options.cwd = directory;

      check_not_null(directory);
#ifdef _WIN32
      check_int_eq(turbo_fs_path_join(script, sizeof(script), directory, "probe.ps1"), TURBO_OK);
      {
        const char *script_data = "param([string]$value)\nWrite-Output "
                                  "\"$value|$env:TURBO_PROCESS_VALUE|$(Get-Location)\"\n";
        turbo_fs_buf_t script_buffer = turbo_fs_buf_init((char *)script_data, strlen(script_data));
        check_int_eq(turbo_fs_write_file(script, &script_buffer), TURBO_OK);
      }
#endif
      check_int_eq(turbo_process_spawn(&options, &process), TURBO_OK);
      check_int_eq(turbo_process_wait(process, &result), TURBO_OK);
      check_int_eq(result.exit_code, 0);
      read_all(process, 1, output, sizeof(output), &output_size);
      check_str_contains(output, "alpha beta|env-value|");
      check_str_contains(output, directory);

      turbo_process_destroy(process);
      check_int_eq(tt_remove_tree(directory), 0);
      free(directory);
    }

    it("pumps large stdin and stdout without pipe deadlock") {
      turbo_process_options_t options;
      turbo_process_t *process = NULL;
      turbo_process_result_t result;
      const size_t payload_size = 256U * 1024U;
      char *payload = (char *)malloc(payload_size);
      char *output = (char *)malloc(payload_size + 32U);
      size_t written = 0;
      size_t output_size = 0;
#ifdef _WIN32
      turbo_process_options_init(&options);
      {
        static const char *copy_args[] = {
            "-NoProfile", "-Command",
            "[Console]::OpenStandardInput().CopyTo([Console]::OpenStandardOutput())", NULL};
        options.program = "powershell.exe";
        options.args = copy_args;
      }
#else
      init_shell_options(&options, "cat");
#endif
      options.flags |= TURBO_PROCESS_PIPE_STDIN;
      options.max_output_bytes = payload_size + 32U;
      memset(payload, 'I', payload_size);

      check_not_null(payload);
      check_not_null(output);
      check_int_eq(turbo_process_spawn(&options, &process), TURBO_OK);
      check_int_eq(turbo_process_write_stdin(process, payload, payload_size, &written), TURBO_OK);
      check_size_eq(written, payload_size);
      check_int_eq(turbo_process_close_stdin(process), TURBO_OK);
      check_int_eq(turbo_process_wait(process, &result), TURBO_OK);
      check_int_eq(result.exit_code, 0);
      read_all(process, 1, output, payload_size + 32U, &output_size);
      check_size_eq(output_size, payload_size);
      check_mem_eq(output, payload, payload_size);

      turbo_process_destroy(process);
      free(output);
      free(payload);
    }
  }

  group("lifecycle") {
    it("times out a bounded wait without terminating the child") {
      turbo_process_options_t options;
      turbo_process_t *process = NULL;
      turbo_process_result_t result;
#ifdef _WIN32
      init_shell_options(&options, "ping -n 4 127.0.0.1 >nul");
#else
      init_shell_options(&options, "sleep 2");
#endif
      check_int_eq(turbo_process_spawn(&options, &process), TURBO_OK);
      check_int_eq(turbo_process_wait_for(process, 25, &result), TURBO_ETIMEDOUT);
      check_true(turbo_process_is_running(process));
      check_int_eq(turbo_process_terminate(process), TURBO_OK);
      check_int_eq(turbo_process_wait(process, &result), TURBO_OK);
      check_int_eq(result.state, TURBO_PROCESS_TERMINATED);
      turbo_process_destroy(process);
    }

    it("enforces the configured execution deadline") {
      turbo_process_options_t options;
      turbo_process_t *process = NULL;
      turbo_process_result_t result;
#ifdef _WIN32
      init_shell_options(&options, "ping -n 4 127.0.0.1 >nul");
#else
      init_shell_options(&options, "sleep 2");
#endif
      options.timeout_ms = 100;
      check_int_eq(turbo_process_spawn(&options, &process), TURBO_OK);
      check_int_eq(turbo_process_wait(process, &result), TURBO_OK);
      check_int_eq(result.state, TURBO_PROCESS_TIMED_OUT);
      check_int_eq(result.exit_code, -1);
      turbo_process_destroy(process);
    }

    it("terminates descendants when the owner is destroyed") {
      turbo_process_options_t options;
      turbo_process_t *process = NULL;
      char *directory = tt_make_temp_dir("turbo-process-tree");
      char marker[512];
#ifdef _WIN32
      char *script;
      script = (char *)malloc(512);
      check_not_null(script);
      check_int_eq(turbo_fs_path_join(script, 512, directory, "tree.cmd"), TURBO_OK);
      snprintf(marker, sizeof(marker), "%s\\marker.txt", directory);
      check_int_eq(
          tt_write_file(
              script,
              "@echo off\r\nstart \"\" /b cmd.exe /d /s /c \"ping -n 3 127.0.0.1 >nul & echo "
              "bad>\\\"%~dp0marker.txt\\\"\"\r\nping -n 20 127.0.0.1 >nul\r\n",
              strlen("@echo off\r\nstart \"\" /b cmd.exe /d /s /c \"ping -n 3 127.0.0.1 >nul & "
                     "echo bad>\\\"%~dp0marker.txt\\\"\"\r\nping -n 20 127.0.0.1 >nul\r\n")),
          0);
      {
        const char *args[] = {"/d", "/s", "/c", script, NULL};
        turbo_process_options_init(&options);
        options.program = "cmd.exe";
        options.args = args;
        check_int_eq(turbo_process_spawn(&options, &process), TURBO_OK);
      }
#else
      const char *script_data = "(sleep 1; echo bad > \"$1\") & sleep 5";
      const char *args[] = {"-c", script_data, "tree-test", marker, NULL};
      snprintf(marker, sizeof(marker), "%s/marker.txt", directory);
      turbo_process_options_init(&options);
      options.program = "/bin/sh";
      options.args = args;
      check_int_eq(turbo_process_spawn(&options, &process), TURBO_OK);
#endif
      check_not_null(directory);
      turbo_sleep_ms(100);
      turbo_process_destroy(process);
      turbo_sleep_ms(1500);
      check_false(path_exists(marker));

#ifdef _WIN32
      free(script);
#endif
      check_int_eq(tt_remove_tree(directory), 0);
      free(directory);
    }

    it("bounds combined captured output") {
      turbo_process_options_t options;
      turbo_process_t *process = NULL;
      turbo_process_result_t result;
      char output[1024];
      size_t output_size = 0;
#ifdef _WIN32
      init_shell_options(&options, "for /L %i in (1,1,1000) do @echo 1234567890");
#else
      init_shell_options(&options,
                         "i=0; while [ $i -lt 1000 ]; do echo 1234567890; i=$((i+1)); done");
#endif
      options.max_output_bytes = 512;
      check_int_eq(turbo_process_spawn(&options, &process), TURBO_OK);
      check_int_eq(turbo_process_wait(process, &result), TURBO_OK);
      check_int_eq(result.state, TURBO_PROCESS_OUTPUT_LIMIT_EXCEEDED);
      read_all(process, 1, output, sizeof(output), &output_size);
      check_size_le(output_size, 512);
      turbo_process_destroy(process);
    }
  }

  group("monitoring") {
    it("reports current and completed process liveness") {
      turbo_process_options_t options;
      turbo_process_t *process = NULL;
      turbo_process_result_t result;
      bool alive = false;
      int child_pid;

      check_int_eq(turbo_process_is_pid_alive(turbo_getpid(), &alive), TURBO_OK);
      check_true(alive);
#ifdef _WIN32
      init_shell_options(&options, "exit /b 0");
#else
      init_shell_options(&options, "exit 0");
#endif
      check_int_eq(turbo_process_spawn(&options, &process), TURBO_OK);
      child_pid = turbo_process_pid(process);
      check_int_eq(turbo_process_wait(process, &result), TURBO_OK);
      check_int_eq(turbo_process_is_pid_alive(child_pid, &alive), TURBO_OK);
      check_false(alive);
      turbo_process_destroy(process);
    }
  }
}
