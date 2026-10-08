#include <salts/native_io.h>
#include <salts/native_ipc.h>

#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/thread.h>

#include "tinytest.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <unistd.h>
#endif

typedef struct ipc_completion_probe {
  cmeta_ipc_completion completions[4];
  cmeta_ipc_pipe_endpoint endpoints[4];
  size_t count;
} ipc_completion_probe;

static void ipc_accept_completion(void *user, const cmeta_ipc_completion *completion,
                                  cmeta_ipc_pipe_endpoint endpoint) {
  ipc_completion_probe *probe = (ipc_completion_probe *)user;
  if (probe->count < 4u) {
    probe->completions[probe->count] = *completion;
    probe->endpoints[probe->count] = endpoint;
    ++probe->count;
  }
}

#if defined(_WIN32)
typedef struct ipc_client_thread_probe {
  const char *name;
  cmeta_ipc_pipe_endpoint endpoint;
  volatile LONG started;
  int status;
} ipc_client_thread_probe;

static void ipc_client_connect_until_available(void *user) {
  ipc_client_thread_probe *probe = (ipc_client_thread_probe *)user;
  const uint64_t started = cmeta_hrtime();
  InterlockedExchange(&probe->started, 1);
  do {
    probe->status =
        cmeta_ipc_named_pipe_connect(probe->name, SALTS_IPC_PIPE_DUPLEX, &probe->endpoint);
    if (probe->status != SALTS_ENOENT && probe->status != SALTS_EBUSY) return;
    cmeta_thread_yield();
  } while (cmeta_hrtime() - started <= UINT64_C(2000000000));
  probe->status = SALTS_ETIMEDOUT;
}

static int ipc_wait(cmeta_ipc_pipe_server *server, ipc_completion_probe *probe, size_t expected) {
  const uint64_t started = cmeta_hrtime();
  while (probe->count < expected) {
    size_t progressed = 0u;
    int status = cmeta_ipc_pipe_server_observe(server, 8u, &progressed);
    if (status != SALTS_OK) return status;
    if (cmeta_hrtime() - started > UINT64_C(5000000000)) return SALTS_ETIMEDOUT;
    if (progressed == 0u) cmeta_thread_yield();
  }
  return SALTS_OK;
}
#endif

spec("NativeIPC pipe control plane") {
  it("reports platform capabilities without fallback") {
#if defined(_WIN32)
    check_true(cmeta_ipc_pipe_capability_supported(SALTS_IPC_WINDOWS_SERVER_ACCEPT));
    check_true(cmeta_ipc_pipe_capability_supported(SALTS_IPC_WINDOWS_CLIENT_CONNECT));
    check_false(cmeta_ipc_pipe_capability_supported(SALTS_IPC_POSIX_FIFO_OPEN));
#else
    check_false(cmeta_ipc_pipe_capability_supported(SALTS_IPC_WINDOWS_SERVER_ACCEPT));
    check_false(cmeta_ipc_pipe_capability_supported(SALTS_IPC_WINDOWS_CLIENT_CONNECT));
    check_true(cmeta_ipc_pipe_capability_supported(SALTS_IPC_POSIX_FIFO_OPEN));
#endif
    check_false(cmeta_ipc_pipe_capability_supported((cmeta_ipc_pipe_capability)-1));
  }

  it("invalidates endpoint ownership before close") {
    cmeta_ipc_pipe_endpoint endpoint = {0};
    cmeta_ipc_pipe_endpoint_init(&endpoint);
    check_false(cmeta_ipc_pipe_endpoint_valid(&endpoint));
    check_equal(endpoint.handle, UINTPTR_MAX);
    check_equal(endpoint.native_io_flags, (uint32_t)0u);
    check_equal(cmeta_ipc_pipe_endpoint_close(&endpoint), SALTS_OK);
    check_false(cmeta_ipc_pipe_endpoint_valid(&endpoint));
    check_equal(cmeta_ipc_pipe_endpoint_close(NULL), SALTS_EINVAL);
  }

  it("rejects malformed server configuration before platform dispatch") {
    char long_name[257];
    cmeta_ipc_pipe_server server = {0};
    cmeta_ipc_pipe_server_config config = {0};
    cmeta_ipc_pipe_endpoint endpoint;
    ipc_completion_probe probe = {0};
    memset(long_name, 'x', sizeof(long_name) - 1u);
    long_name[sizeof(long_name) - 1u] = '\0';
    cmeta_ipc_pipe_endpoint_init(&endpoint);
    config.name = "native-ipc-invalid";
    config.direction = SALTS_IPC_PIPE_DUPLEX;
    config.request_capacity = 1u;
    config.input_buffer_size = 4096u;
    config.output_buffer_size = 4096u;
    config.completion = ipc_accept_completion;
    config.completion_user = &probe;
    check_equal(cmeta_ipc_pipe_server_init(&server, NULL), SALTS_EINVAL);
    config.request_capacity = 0u;
    check_equal(cmeta_ipc_pipe_server_init(&server, &config), SALTS_EINVAL);
    config.request_capacity = 1u;
    config.direction = (cmeta_ipc_pipe_direction)0;
    check_equal(cmeta_ipc_pipe_server_init(&server, &config), SALTS_EINVAL);
    check_equal(cmeta_ipc_named_pipe_connect(long_name, SALTS_IPC_PIPE_DUPLEX, &endpoint),
                SALTS_EINVAL);
    check_null(server.impl);
  }

#if defined(_WIN32)
  describe("Windows client endpoint type validation") {
    static char *file_path;
    static cmeta_ipc_pipe_endpoint endpoint;

    before_each() {
      cmeta_ipc_pipe_endpoint_init(&endpoint);
      file_path = tt_make_temp_file("native-ipc-not-pipe-", ".bin");
      check_not_null(file_path);
    }

    after_each() {
      check_equal(cmeta_ipc_pipe_endpoint_close(&endpoint), SALTS_OK);
      if (file_path != NULL) {
        check_equal(tt_remove_file(file_path), 0);
        free(file_path);
        file_path = NULL;
      }
    }

    it("rejects regular files for every pipe direction without leaking handles") {
      static const cmeta_ipc_pipe_direction directions[] = {
          SALTS_IPC_PIPE_READ, SALTS_IPC_PIPE_WRITE, SALTS_IPC_PIPE_DUPLEX};
      DWORD handles_before = 0u;
      DWORD handles_after = 0u;

      check_true(GetProcessHandleCount(GetCurrentProcess(), &handles_before));
      for (size_t index = 0u; index < sizeof(directions) / sizeof(directions[0]); ++index) {
        check_equal(cmeta_ipc_named_pipe_connect(file_path, directions[index], &endpoint),
                    SALTS_ENOTSUP);
        check_false(cmeta_ipc_pipe_endpoint_valid(&endpoint));
        check_equal(endpoint.native_io_flags, (uint32_t)0u);
        check_true(GetProcessHandleCount(GetCurrentProcess(), &handles_after));
        check_equal(handles_after, handles_before);
        check_equal(cmeta_ipc_pipe_endpoint_close(&endpoint), SALTS_OK);
      }
    }
  }

  it("transfers one overlapped endpoint after a client connects") {
    char name[160];
    cmeta_ipc_pipe_server server = {0};
    cmeta_ipc_pipe_server_config config = {0};
    cmeta_ipc_pipe_endpoint client;
    cmeta_ipc_request_id request_id = 0u;
    ipc_completion_probe probe = {0};

    snprintf(name, sizeof(name), "\\\\.\\pipe\\native-ipc-rendezvous-%lu-%llu",
             (unsigned long)GetCurrentProcessId(), (unsigned long long)cmeta_hrtime());
    cmeta_ipc_pipe_endpoint_init(&client);
    config.name = name;
    config.direction = SALTS_IPC_PIPE_DUPLEX;
    config.request_capacity = 1u;
    config.input_buffer_size = 4096u;
    config.output_buffer_size = 4096u;
    config.completion = ipc_accept_completion;
    config.completion_user = &probe;

    check_equal(cmeta_ipc_pipe_server_init(&server, &config), SALTS_OK);
    check_equal(cmeta_ipc_pipe_server_try_accept(&server, &request_id), SALTS_OK);
    check_not_equal(request_id, (cmeta_ipc_request_id)0u);
    check_equal(cmeta_ipc_named_pipe_connect(name, SALTS_IPC_PIPE_DUPLEX, &client), SALTS_OK);
    check_true(cmeta_ipc_pipe_endpoint_valid(&client));
    check_equal(ipc_wait(&server, &probe, 1u), SALTS_OK);
    check_equal(probe.completions[0].request_id, request_id);
    check_equal(probe.completions[0].kind, SALTS_IPC_COMPLETION_OK);
    check_equal(probe.completions[0].status, SALTS_OK);
    check_true(cmeta_ipc_pipe_endpoint_valid(&probe.endpoints[0]));
    check_equal(probe.endpoints[0].native_io_flags,
                (uint32_t)NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE);

    check_equal(cmeta_ipc_pipe_endpoint_close(&client), SALTS_OK);
    check_equal(cmeta_ipc_pipe_endpoint_close(&probe.endpoints[0]), SALTS_OK);
    check_equal(cmeta_ipc_pipe_server_close(&server), SALTS_OK);
    check_true(cmeta_ipc_pipe_server_is_quiescent(&server));
    check_equal(cmeta_ipc_pipe_server_destroy(&server), SALTS_OK);
  }

  it("reuses a freed accept slot while transferred instances remain open") {
    char name[160];
    cmeta_ipc_pipe_server server = {0};
    cmeta_ipc_pipe_server_config config = {0};
    cmeta_ipc_pipe_endpoint first_client;
    cmeta_ipc_pipe_endpoint second_client;
    cmeta_ipc_request_id first_request = 0u;
    cmeta_ipc_request_id second_request = 0u;
    ipc_completion_probe probe = {0};
    int second_status;

    snprintf(name, sizeof(name), "\\\\.\\pipe\\native-ipc-instance-%lu-%llu",
             (unsigned long)GetCurrentProcessId(), (unsigned long long)cmeta_hrtime());
    cmeta_ipc_pipe_endpoint_init(&first_client);
    cmeta_ipc_pipe_endpoint_init(&second_client);
    config.name = name;
    config.direction = SALTS_IPC_PIPE_DUPLEX;
    config.request_capacity = 1u;
    config.input_buffer_size = 4096u;
    config.output_buffer_size = 4096u;
    config.completion = ipc_accept_completion;
    config.completion_user = &probe;

    check_equal(cmeta_ipc_pipe_server_init(&server, &config), SALTS_OK);
    check_equal(cmeta_ipc_pipe_server_try_accept(&server, &first_request), SALTS_OK);
    check_equal(cmeta_ipc_named_pipe_connect(name, SALTS_IPC_PIPE_DUPLEX, &first_client), SALTS_OK);
    check_equal(ipc_wait(&server, &probe, 1u), SALTS_OK);
    check_true(cmeta_ipc_pipe_endpoint_valid(&probe.endpoints[0]));

    second_status = cmeta_ipc_pipe_server_try_accept(&server, &second_request);
    check_equal(second_status, SALTS_OK);
    if (second_status == SALTS_OK) {
      check_equal(cmeta_ipc_named_pipe_connect(name, SALTS_IPC_PIPE_DUPLEX, &second_client),
                  SALTS_OK);
      check_equal(ipc_wait(&server, &probe, 2u), SALTS_OK);
      check_equal(probe.completions[1].request_id, second_request);
      check_equal(probe.completions[1].kind, SALTS_IPC_COMPLETION_OK);
      check_equal(cmeta_ipc_pipe_endpoint_close(&second_client), SALTS_OK);
      check_equal(cmeta_ipc_pipe_endpoint_close(&probe.endpoints[1]), SALTS_OK);
    }
    check_equal(cmeta_ipc_pipe_endpoint_close(&first_client), SALTS_OK);
    check_equal(cmeta_ipc_pipe_endpoint_close(&probe.endpoints[0]), SALTS_OK);
    check_equal(cmeta_ipc_pipe_server_close(&server), SALTS_OK);
    check_true(cmeta_ipc_pipe_server_is_quiescent(&server));
    check_equal(cmeta_ipc_pipe_server_destroy(&server), SALTS_OK);
  }

  it("bounds pending accepts and reclaims cancelled slots") {
    char name[160];
    cmeta_ipc_pipe_server server = {0};
    cmeta_ipc_pipe_server_config config = {0};
    cmeta_ipc_pipe_server_stats stats = {0};
    cmeta_ipc_request_id first = 0u;
    cmeta_ipc_request_id second = 0u;
    cmeta_ipc_request_id rejected = 99u;
    ipc_completion_probe probe = {0};

    snprintf(name, sizeof(name), "\\\\.\\pipe\\native-ipc-capacity-%lu-%llu",
             (unsigned long)GetCurrentProcessId(), (unsigned long long)cmeta_hrtime());
    config.name = name;
    config.direction = SALTS_IPC_PIPE_DUPLEX;
    config.request_capacity = 2u;
    config.input_buffer_size = 4096u;
    config.output_buffer_size = 4096u;
    config.completion = ipc_accept_completion;
    config.completion_user = &probe;

    check_equal(cmeta_ipc_pipe_server_init(&server, &config), SALTS_OK);
    check_equal(cmeta_ipc_pipe_server_try_accept(&server, &first), SALTS_OK);
    check_equal(cmeta_ipc_pipe_server_try_accept(&server, &second), SALTS_OK);
    check_equal(cmeta_ipc_pipe_server_try_accept(&server, &rejected), SALTS_ENOBUFS);
    check_equal(rejected, (cmeta_ipc_request_id)0u);
    check_true(cmeta_ipc_pipe_server_get_stats(&server, &stats));
    check_equal(stats.request_capacity, (size_t)2u);
    check_equal(stats.active_requests, (size_t)2u);
    check_equal(stats.submitted, (uint64_t)2u);
    check_equal(stats.rejected_full, (uint64_t)1u);
    check_true(stats.admission_open);
    {
      uintptr_t sources[2] = {UINTPTR_MAX, UINTPTR_MAX};
      size_t count = 0u;
      bool ready = true;
      check_equal(cmeta_ipc_pipe_server_wait_sources(&server, sources, 1u, &count, &ready), SALTS_ENOBUFS);
      check_equal(count, 2u);
      check_equal(sources[0], UINTPTR_MAX);
      check_false(ready);
      check_equal(cmeta_ipc_pipe_server_wait_sources(&server, sources, 2u, &count, &ready), SALTS_OK);
      check_equal(count, 2u);
      check(sources[0] != 0u && sources[1] != 0u && sources[0] != sources[1]);
    }
    check_equal(cmeta_ipc_pipe_server_cancel(&server, first), SALTS_OK);
    check_equal(cmeta_ipc_pipe_server_cancel(&server, second), SALTS_OK);
    check_equal(ipc_wait(&server, &probe, 2u), SALTS_OK);
    check_equal(probe.completions[0].kind, SALTS_IPC_COMPLETION_CANCELLED);
    check_equal(probe.completions[1].kind, SALTS_IPC_COMPLETION_CANCELLED);
    check_equal(cmeta_ipc_pipe_server_close(&server), SALTS_OK);
    check_true(cmeta_ipc_pipe_server_get_stats(&server, &stats));
    check_equal(stats.active_requests, (size_t)0u);
    check_equal(stats.cancelled, (uint64_t)2u);
    check_equal(stats.failed, (uint64_t)0u);
    check_false(stats.admission_open);
    check_true(cmeta_ipc_pipe_server_is_quiescent(&server));
    check_equal(cmeta_ipc_pipe_server_destroy(&server), SALTS_OK);
  }

  it("maps missing and busy named pipe clients without waiting") {
    char name[160];
    cmeta_ipc_pipe_server server = {0};
    cmeta_ipc_pipe_server_config config = {0};
    cmeta_ipc_pipe_endpoint first_client;
    cmeta_ipc_pipe_endpoint second_client;
    cmeta_ipc_request_id request_id = 0u;
    ipc_completion_probe probe = {0};

    snprintf(name, sizeof(name), "\\\\.\\pipe\\native-ipc-busy-%lu-%llu",
             (unsigned long)GetCurrentProcessId(), (unsigned long long)cmeta_hrtime());
    cmeta_ipc_pipe_endpoint_init(&first_client);
    cmeta_ipc_pipe_endpoint_init(&second_client);
    check_equal(cmeta_ipc_named_pipe_connect(name, SALTS_IPC_PIPE_DUPLEX, &first_client),
                SALTS_ENOENT);
    config.name = name;
    config.direction = SALTS_IPC_PIPE_DUPLEX;
    config.request_capacity = 1u;
    config.input_buffer_size = 4096u;
    config.output_buffer_size = 4096u;
    config.completion = ipc_accept_completion;
    config.completion_user = &probe;
    check_equal(cmeta_ipc_pipe_server_init(&server, &config), SALTS_OK);
    check_equal(cmeta_ipc_pipe_server_try_accept(&server, &request_id), SALTS_OK);
    check_equal(cmeta_ipc_named_pipe_connect(name, SALTS_IPC_PIPE_DUPLEX, &first_client), SALTS_OK);
    check_equal(cmeta_ipc_named_pipe_connect(name, SALTS_IPC_PIPE_DUPLEX, &second_client),
                SALTS_EBUSY);
    check_equal(ipc_wait(&server, &probe, 1u), SALTS_OK);
    check_equal(cmeta_ipc_pipe_endpoint_close(&first_client), SALTS_OK);
    check_equal(cmeta_ipc_pipe_endpoint_close(&probe.endpoints[0]), SALTS_OK);
    check_equal(cmeta_ipc_pipe_server_close(&server), SALTS_OK);
    check_equal(cmeta_ipc_pipe_server_destroy(&server), SALTS_OK);
  }

  it("turns close of a pending accept into one cancellation without leaking handles") {
    char name[160];
    cmeta_ipc_pipe_server server = {0};
    cmeta_ipc_pipe_server_config config = {0};
    cmeta_ipc_request_id request_id = 0u;
    cmeta_ipc_request_id rejected = 99u;
    ipc_completion_probe probe = {0};
    DWORD handles_before = 0u;
    DWORD handles_after = 0u;

    check_true(GetProcessHandleCount(GetCurrentProcess(), &handles_before));
    snprintf(name, sizeof(name), "\\\\.\\pipe\\native-ipc-close-%lu-%llu",
             (unsigned long)GetCurrentProcessId(), (unsigned long long)cmeta_hrtime());
    config.name = name;
    config.direction = SALTS_IPC_PIPE_DUPLEX;
    config.request_capacity = 1u;
    config.input_buffer_size = 4096u;
    config.output_buffer_size = 4096u;
    config.completion = ipc_accept_completion;
    config.completion_user = &probe;
    check_equal(cmeta_ipc_pipe_server_init(&server, &config), SALTS_OK);
    check_equal(cmeta_ipc_pipe_server_try_accept(&server, &request_id), SALTS_OK);
    check_equal(cmeta_ipc_pipe_server_close(&server), SALTS_OK);
    check_equal(cmeta_ipc_pipe_server_try_accept(&server, &rejected), SALTS_ESHUTDOWN);
    check_equal(rejected, (cmeta_ipc_request_id)0u);
    check_equal(ipc_wait(&server, &probe, 1u), SALTS_OK);
    check_equal(probe.completions[0].request_id, request_id);
    check_equal(probe.completions[0].kind, SALTS_IPC_COMPLETION_CANCELLED);
    check_true(cmeta_ipc_pipe_server_is_quiescent(&server));
    check_equal(cmeta_ipc_pipe_server_destroy(&server), SALTS_OK);
    check_true(GetProcessHandleCount(GetCurrentProcess(), &handles_after));
    check_equal(handles_after, handles_before);
  }

  it("settles a create-connect race exactly once") {
    char name[160];
    cmeta_ipc_pipe_server server = {0};
    cmeta_ipc_pipe_server_config config = {0};
    ipc_client_thread_probe client = {0};
    ipc_completion_probe probe = {0};
    cmeta_ipc_request_id request_id = 0u;
    cmeta_thread_t thread = NULL;

    snprintf(name, sizeof(name), "\\\\.\\pipe\\native-ipc-race-%lu-%llu",
             (unsigned long)GetCurrentProcessId(), (unsigned long long)cmeta_hrtime());
    cmeta_ipc_pipe_endpoint_init(&client.endpoint);
    client.name = name;
    client.status = SALTS_EBUSY;
    config.name = name;
    config.direction = SALTS_IPC_PIPE_DUPLEX;
    config.request_capacity = 1u;
    config.input_buffer_size = 4096u;
    config.output_buffer_size = 4096u;
    config.completion = ipc_accept_completion;
    config.completion_user = &probe;
    check_equal(cmeta_ipc_pipe_server_init(&server, &config), SALTS_OK);
    check_equal(cmeta_thread_create(&thread, ipc_client_connect_until_available, &client),
                SALTS_OK);
    while (InterlockedCompareExchange(&client.started, 1, 1) == 0)
      cmeta_thread_yield();
    check_equal(cmeta_ipc_pipe_server_try_accept(&server, &request_id), SALTS_OK);
    check_equal(cmeta_thread_join(&thread), SALTS_OK);
    check_equal(client.status, SALTS_OK);
    check_equal(ipc_wait(&server, &probe, 1u), SALTS_OK);
    check_equal(probe.count, (size_t)1u);
    check_equal(probe.completions[0].request_id, request_id);
    check_equal(probe.completions[0].kind, SALTS_IPC_COMPLETION_OK);
    check_equal(cmeta_ipc_pipe_endpoint_close(&client.endpoint), SALTS_OK);
    check_equal(cmeta_ipc_pipe_endpoint_close(&probe.endpoints[0]), SALTS_OK);
    check_equal(cmeta_ipc_pipe_server_close(&server), SALTS_OK);
    check_equal(cmeta_ipc_pipe_server_destroy(&server), SALTS_OK);
  }

  it("reports POSIX FIFO open as unsupported") {
    cmeta_ipc_pipe_endpoint endpoint;
    cmeta_ipc_pipe_endpoint_init(&endpoint);
    check_equal(cmeta_ipc_fifo_open("not-a-posix-fifo", SALTS_IPC_PIPE_READ, &endpoint),
                SALTS_ENOTSUP);
    check_false(cmeta_ipc_pipe_endpoint_valid(&endpoint));
  }
#else
  it("reports Windows rendezvous as unsupported") {
    cmeta_ipc_pipe_server server = {0};
    cmeta_ipc_pipe_server_config config = {0};
    cmeta_ipc_pipe_endpoint endpoint;
    ipc_completion_probe probe = {0};
    config.name = "not-a-windows-pipe";
    config.direction = SALTS_IPC_PIPE_DUPLEX;
    config.request_capacity = 1u;
    config.input_buffer_size = 4096u;
    config.output_buffer_size = 4096u;
    config.completion = ipc_accept_completion;
    config.completion_user = &probe;
    cmeta_ipc_pipe_endpoint_init(&endpoint);
    check_equal(cmeta_ipc_pipe_server_init(&server, &config), SALTS_ENOTSUP);
    check_equal(cmeta_ipc_named_pipe_connect(config.name, config.direction, &endpoint),
                SALTS_ENOTSUP);
    check_false(cmeta_ipc_pipe_endpoint_valid(&endpoint));
  }

  it("opens FIFO endpoints without hiding pathname rendezvous") {
    char *directory = tt_make_temp_dir("native-ipc-fifo-");
    char path[512];
    cmeta_ipc_pipe_endpoint reader;
    cmeta_ipc_pipe_endpoint writer;
    static const char payload[] = "fifo-data";
    char received[sizeof(payload)] = {0};

    check_not_null(directory);
    snprintf(path, sizeof(path), "%s/channel", directory);
    check_equal(mkfifo(path, 0600), 0);
    cmeta_ipc_pipe_endpoint_init(&reader);
    cmeta_ipc_pipe_endpoint_init(&writer);
    check_equal(cmeta_ipc_fifo_open(path, SALTS_IPC_PIPE_WRITE, &writer), SALTS_EPIPE);
    check_false(cmeta_ipc_pipe_endpoint_valid(&writer));
    check_equal(cmeta_ipc_fifo_open(path, SALTS_IPC_PIPE_READ, &reader), SALTS_OK);
    check_equal(cmeta_ipc_fifo_open(path, SALTS_IPC_PIPE_WRITE, &writer), SALTS_OK);
    check_equal(reader.native_io_flags, (uint32_t)NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE);
    check_equal(writer.native_io_flags, (uint32_t)NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE);
    check_equal(write((int)writer.handle, payload, sizeof(payload) - 1u),
                (ssize_t)(sizeof(payload) - 1u));
    check_equal(read((int)reader.handle, received, sizeof(received)),
                (ssize_t)(sizeof(payload) - 1u));
    check_equal(received, payload, sizeof(payload) - 1u);
    check_equal(cmeta_ipc_pipe_endpoint_close(&writer), SALTS_OK);
    check_equal(cmeta_ipc_pipe_endpoint_close(&reader), SALTS_OK);
    check_equal(tt_remove_tree(directory), 0);
    free(directory);
  }

  it("rejects duplex and regular-file FIFO endpoints") {
    char *path = tt_make_temp_file("native-ipc-not-fifo-", ".bin");
    cmeta_ipc_pipe_endpoint endpoint;
    check_not_null(path);
    cmeta_ipc_pipe_endpoint_init(&endpoint);
    check_equal(cmeta_ipc_fifo_open(path, SALTS_IPC_PIPE_DUPLEX, &endpoint), SALTS_EINVAL);
    errno = EACCES;
    check_equal(cmeta_ipc_fifo_open(path, SALTS_IPC_PIPE_READ, &endpoint), SALTS_ENOTSUP);
    check_false(cmeta_ipc_pipe_endpoint_valid(&endpoint));
    check_equal(tt_remove_file(path), 0);
    free(path);
  }

  it("releases every FIFO descriptor across repeated open-close cycles") {
    char *directory = tt_make_temp_dir("native-ipc-fifo-cycle-");
    char path[512];
    int descriptor_before;
    int descriptor_after;
    check_not_null(directory);
    snprintf(path, sizeof(path), "%s/channel", directory);
    check_equal(mkfifo(path, 0600), 0);
  #if defined(F_DUPFD_CLOEXEC)
    descriptor_before = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 0);
  #else
    descriptor_before = fcntl(STDIN_FILENO, F_DUPFD, 0);
  #endif
    check_greater(descriptor_before, -1);
    check_equal(close(descriptor_before), 0);
    for (size_t index = 0u; index < 32u; ++index) {
      cmeta_ipc_pipe_endpoint reader;
      cmeta_ipc_pipe_endpoint writer;
      cmeta_ipc_pipe_endpoint_init(&reader);
      cmeta_ipc_pipe_endpoint_init(&writer);
      check_equal(cmeta_ipc_fifo_open(path, SALTS_IPC_PIPE_READ, &reader), SALTS_OK);
      check_equal(cmeta_ipc_fifo_open(path, SALTS_IPC_PIPE_WRITE, &writer), SALTS_OK);
      check_equal(cmeta_ipc_pipe_endpoint_close(&writer), SALTS_OK);
      check_equal(cmeta_ipc_pipe_endpoint_close(&reader), SALTS_OK);
    }
  #if defined(F_DUPFD_CLOEXEC)
    descriptor_after = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 0);
  #else
    descriptor_after = fcntl(STDIN_FILENO, F_DUPFD, 0);
  #endif
    check_equal(descriptor_after, descriptor_before);
    check_equal(close(descriptor_after), 0);
    check_equal(tt_remove_tree(directory), 0);
    free(directory);
  }
#endif
}
