#if !defined(_WIN32) && defined(__linux__) && !defined(_POSIX_C_SOURCE)
  #define _POSIX_C_SOURCE 200809L
#endif

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#endif

#include <turbo/clock.h>
#include <turbo/error_codes.h>
#include <turbo/native_io.h>

#include "tinytest.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
  #include <fcntl.h>
  #include <unistd.h>
#endif

#if defined(_WIN32)
  #define NATIVE_PIPE_BACKEND_KIND TURBO_IO_BACKEND_IOCP
  #define NATIVE_PIPE_BACKEND_NAME "IOCP"
  #define NATIVE_PIPE_RAW_NAME "raw IOCP"
typedef HANDLE native_pipe_handle;
  #define NATIVE_PIPE_INVALID_HANDLE INVALID_HANDLE_VALUE
#elif defined(__linux__)
  #define NATIVE_PIPE_BACKEND_KIND TURBO_IO_BACKEND_EPOLL
  #define NATIVE_PIPE_BACKEND_NAME "epoll"
  #define NATIVE_PIPE_RAW_NAME "raw POSIX"
typedef int native_pipe_handle;
  #define NATIVE_PIPE_INVALID_HANDLE (-1)
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
    defined(__DragonFly__)
  #define NATIVE_PIPE_BACKEND_KIND TURBO_IO_BACKEND_KQUEUE
  #define NATIVE_PIPE_BACKEND_NAME "kqueue"
  #define NATIVE_PIPE_RAW_NAME "raw POSIX"
typedef int native_pipe_handle;
  #define NATIVE_PIPE_INVALID_HANDLE (-1)
#else
  #error "NativeIO pipe benchmark requires IOCP, epoll, or kqueue"
#endif

typedef enum native_pipe_driver { NATIVE_PIPE_RAW = 0, NATIVE_PIPE_NATIVE_IO } native_pipe_driver;

enum {
  NATIVE_PIPE_SAMPLES = 20,
  NATIVE_PIPE_TRANSFERS_PER_SAMPLE = 256,
  NATIVE_PIPE_WARMUP_TRANSFERS = 64,
  NATIVE_PIPE_TIMEOUT_MS = 5000,
  NATIVE_PIPE_ENDPOINT_CAPACITY = 2,
  NATIVE_PIPE_REQUEST_CAPACITY = 4,
  NATIVE_PIPE_BATCH_CAPACITY = 4,
  NATIVE_PIPE_BUFFER_CAPACITY = 65536,
  NATIVE_PIPE_READ_USER_DATA = 1,
  NATIVE_PIPE_WRITE_USER_DATA = 2
};

static const size_t NATIVE_PIPE_PAYLOADS[] = {1024u, 4096u, 8192u, 16384u, 32768u, 65536u};

typedef struct native_pipe_stages {
  uint64_t submit_ns;
  uint64_t observe_ns;
  uint64_t submits;
  uint64_t observes;
} native_pipe_stages;

typedef struct native_pipe_result {
  size_t payload_size;
  uint64_t wall_ns;
  uint64_t p50_ns;
  uint64_t p95_ns;
  native_pipe_stages stages;
  uint64_t *latencies;
  size_t latency_count;
  size_t latency_capacity;
} native_pipe_result;

typedef struct native_pipe_fixture {
  native_pipe_driver driver;
  native_pipe_handle handles[2];
#if defined(_WIN32)
  HANDLE port;
#endif
  turbo_io_backend backend;
  turbo_io_endpoint endpoints[2];
  unsigned char *sent;
  unsigned char *received;
  size_t payload_size;
} native_pipe_fixture;

static void native_pipe_counter_add(uint64_t *counter, uint64_t value) {
  *counter = UINT64_MAX - *counter < value ? UINT64_MAX : *counter + value;
}

#if defined(_WIN32)
static int native_pipe_windows_error(DWORD error) {
  return error == ERROR_SUCCESS ? TURBO_EIO : -(int)error;
}

static void native_pipe_close_handle(native_pipe_handle handle) {
  if (handle != NATIVE_PIPE_INVALID_HANDLE) (void)CloseHandle(handle);
}

static int native_pipe_make_pair(native_pipe_handle handles[2]) {
  static LONG sequence = 0;
  char name[128];
  OVERLAPPED connected = {0};
  HANDLE event = NULL;
  DWORD error = ERROR_SUCCESS;
  BOOL pending = FALSE;
  int name_length;

  handles[0] = NATIVE_PIPE_INVALID_HANDLE;
  handles[1] = NATIVE_PIPE_INVALID_HANDLE;
  name_length = snprintf(name, sizeof(name), "\\\\.\\pipe\\native-io-benchmark-%lu-%ld",
                         GetCurrentProcessId(), InterlockedIncrement(&sequence));
  if (name_length < 0 || (size_t)name_length >= sizeof(name)) return TURBO_ERANGE;
  handles[1] = CreateNamedPipeA(name, PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
                                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1u,
                                NATIVE_PIPE_BUFFER_CAPACITY, NATIVE_PIPE_BUFFER_CAPACITY, 0u, NULL);
  if (handles[1] == NATIVE_PIPE_INVALID_HANDLE) return native_pipe_windows_error(GetLastError());
  event = CreateEventA(NULL, TRUE, FALSE, NULL);
  if (event == NULL) {
    error = GetLastError();
    goto failed;
  }
  connected.hEvent = event;
  if (!ConnectNamedPipe(handles[1], &connected)) {
    error = GetLastError();
    if (error == ERROR_IO_PENDING) pending = TRUE;
    else if (error != ERROR_PIPE_CONNECTED) goto failed;
  }
  handles[0] = CreateFileA(name, GENERIC_READ, 0u, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
  if (handles[0] == NATIVE_PIPE_INVALID_HANDLE) {
    error = GetLastError();
    goto failed;
  }
  if (pending) {
    DWORD transferred = 0u;
    if (!GetOverlappedResult(handles[1], &connected, &transferred, TRUE)) {
      error = GetLastError();
      goto failed;
    }
  }
  (void)CloseHandle(event);
  return TURBO_OK;

failed:
  native_pipe_close_handle(handles[0]);
  native_pipe_close_handle(handles[1]);
  if (event != NULL) (void)CloseHandle(event);
  handles[0] = NATIVE_PIPE_INVALID_HANDLE;
  handles[1] = NATIVE_PIPE_INVALID_HANDLE;
  return native_pipe_windows_error(error);
}
#else
static void native_pipe_close_handle(native_pipe_handle handle) {
  if (handle >= 0) (void)close(handle);
}

static int native_pipe_set_nonblocking(int descriptor) {
  const int flags = fcntl(descriptor, F_GETFL, 0);
  if (flags < 0) return -errno;
  if ((flags & O_NONBLOCK) != 0) return TURBO_OK;
  return fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0 ? TURBO_OK : -errno;
}

static int native_pipe_make_pair(native_pipe_handle handles[2]) {
  int status;
  handles[0] = NATIVE_PIPE_INVALID_HANDLE;
  handles[1] = NATIVE_PIPE_INVALID_HANDLE;
  if (pipe(handles) != 0) return -errno;
  status = native_pipe_set_nonblocking(handles[0]);
  if (status == TURBO_OK) status = native_pipe_set_nonblocking(handles[1]);
  if (status != TURBO_OK) {
    native_pipe_close_handle(handles[0]);
    native_pipe_close_handle(handles[1]);
    handles[0] = NATIVE_PIPE_INVALID_HANDLE;
    handles[1] = NATIVE_PIPE_INVALID_HANDLE;
  }
  return status;
}
#endif

static int native_pipe_fixture_init(native_pipe_fixture *fixture, native_pipe_driver driver,
                                    size_t payload_size) {
  const turbo_io_backend_config config = {NATIVE_PIPE_BACKEND_KIND, NATIVE_PIPE_ENDPOINT_CAPACITY,
                                          NATIVE_PIPE_REQUEST_CAPACITY, NATIVE_PIPE_BATCH_CAPACITY};
  int status;
  memset(fixture, 0, sizeof(*fixture));
  fixture->driver = driver;
  fixture->handles[0] = NATIVE_PIPE_INVALID_HANDLE;
  fixture->handles[1] = NATIVE_PIPE_INVALID_HANDLE;
#if defined(_WIN32)
  fixture->port = NULL;
#endif
  fixture->payload_size = payload_size;
  fixture->sent = (unsigned char *)malloc(payload_size);
  fixture->received = (unsigned char *)malloc(payload_size);
  if (fixture->sent == NULL || fixture->received == NULL) return TURBO_ENOMEM;
  memset(fixture->sent, 0x5a, payload_size);
  memset(fixture->received, 0, payload_size);
  status = native_pipe_make_pair(fixture->handles);
  if (status != TURBO_OK) return status;
  if (driver == NATIVE_PIPE_RAW) {
#if defined(_WIN32)
    fixture->port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0u, 1u);
    if (fixture->port == NULL) return native_pipe_windows_error(GetLastError());
    for (size_t index = 0u; index < 2u; ++index) {
      if (CreateIoCompletionPort(fixture->handles[index], fixture->port, (ULONG_PTR)(index + 1u),
                                 0u) == NULL)
        return native_pipe_windows_error(GetLastError());
    }
#endif
    return TURBO_OK;
  }
  if (!turbo_io_backend_pipe_supported(NATIVE_PIPE_BACKEND_KIND)) return TURBO_ENOTSUP;
  status = turbo_io_backend_init(&fixture->backend, &config);
  if (status == TURBO_OK)
    status =
        turbo_io_backend_attach_pipe(&fixture->backend, (uintptr_t)fixture->handles[0],
                                     TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &fixture->endpoints[0]);
  if (status == TURBO_OK)
    status =
        turbo_io_backend_attach_pipe(&fixture->backend, (uintptr_t)fixture->handles[1],
                                     TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &fixture->endpoints[1]);
  return status;
}

static int native_pipe_fixture_destroy(native_pipe_fixture *fixture) {
  int status = TURBO_OK;
#if defined(_WIN32)
  int backend_close_status = TURBO_OK;
  if (fixture->driver == NATIVE_PIPE_NATIVE_IO && fixture->backend.impl != NULL) {
    backend_close_status = turbo_io_backend_close(&fixture->backend);
    if (backend_close_status != TURBO_OK) status = backend_close_status;
  }
#endif
  for (size_t index = 0u; index < 2u; ++index) {
    if (fixture->handles[index] != NATIVE_PIPE_INVALID_HANDLE) {
#if defined(_WIN32)
      if (!CloseHandle(fixture->handles[index]) && status == TURBO_OK)
        status = native_pipe_windows_error(GetLastError());
#else
      if (close(fixture->handles[index]) != 0 && status == TURBO_OK) status = -errno;
#endif
      fixture->handles[index] = NATIVE_PIPE_INVALID_HANDLE;
    }
  }
#if defined(_WIN32)
  if (fixture->port != NULL) {
    if (!CloseHandle(fixture->port) && status == TURBO_OK)
      status = native_pipe_windows_error(GetLastError());
    fixture->port = NULL;
  }
#endif
  if (fixture->driver == NATIVE_PIPE_NATIVE_IO && fixture->backend.impl != NULL) {
    for (size_t index = 0u; index < 2u; ++index) {
      if (turbo_io_endpoint_valid(fixture->endpoints[index])) {
        const int release_status =
            turbo_io_backend_release_pipe(&fixture->backend, fixture->endpoints[index]);
        if (status == TURBO_OK && release_status != TURBO_OK) status = release_status;
        fixture->endpoints[index] = (turbo_io_endpoint){0};
      }
    }
    {
#if defined(_WIN32)
      const int close_status = backend_close_status;
#else
      const int close_status = turbo_io_backend_close(&fixture->backend);
#endif
      const int destroy_status =
          close_status == TURBO_OK ? turbo_io_backend_destroy(&fixture->backend) : close_status;
      if (status == TURBO_OK && destroy_status != TURBO_OK) status = destroy_status;
    }
  }
  free(fixture->received);
  free(fixture->sent);
  fixture->received = NULL;
  fixture->sent = NULL;
  return status;
}

#if defined(_WIN32)
typedef struct native_pipe_raw_request {
  OVERLAPPED overlapped;
} native_pipe_raw_request;

static int native_pipe_raw_post(native_pipe_fixture *fixture, native_pipe_raw_request *request,
                                bool read, void *buffer, size_t length,
                                native_pipe_stages *stages) {
  DWORD immediate_bytes = 0u;
  BOOL submitted;
  DWORD error;
  uint64_t started;
  if (length > (size_t)MAXDWORD) return TURBO_ERANGE;
  memset(request, 0, sizeof(*request));
  started = turbo_hrtime();
  submitted = read ? ReadFile(fixture->handles[0], buffer, (DWORD)length, &immediate_bytes,
                              &request->overlapped)
                   : WriteFile(fixture->handles[1], buffer, (DWORD)length, &immediate_bytes,
                               &request->overlapped);
  native_pipe_counter_add(&stages->submit_ns, turbo_hrtime() - started);
  ++stages->submits;
  if (submitted) return TURBO_OK;
  error = GetLastError();
  return error == ERROR_IO_PENDING ? TURBO_OK : native_pipe_windows_error(error);
}

static int native_pipe_raw_transfer(native_pipe_fixture *fixture, native_pipe_stages *stages) {
  native_pipe_raw_request requests[2];
  size_t offsets[2] = {0u, 0u};
  bool pending[2] = {false, false};
  while (offsets[0] < fixture->payload_size || offsets[1] < fixture->payload_size) {
    for (size_t role = 0u; role < 2u; ++role) {
      int status;
      if (pending[role] || offsets[role] >= fixture->payload_size) continue;
      status =
          native_pipe_raw_post(fixture, &requests[role], role == 0u,
                               (role == 0u ? fixture->received : fixture->sent) + offsets[role],
                               fixture->payload_size - offsets[role], stages);
      if (status != TURBO_OK) return status;
      pending[role] = true;
    }
    {
      DWORD bytes = 0u;
      ULONG_PTR completion_key = 0u;
      OVERLAPPED *overlapped = NULL;
      DWORD error;
      const uint64_t started = turbo_hrtime();
      const BOOL completed = GetQueuedCompletionStatus(fixture->port, &bytes, &completion_key,
                                                       &overlapped, NATIVE_PIPE_TIMEOUT_MS);
      native_pipe_counter_add(&stages->observe_ns, turbo_hrtime() - started);
      ++stages->observes;
      error = completed ? ERROR_SUCCESS : GetLastError();
      (void)completion_key;
      if (overlapped == NULL)
        return error == WAIT_TIMEOUT ? TURBO_ETIMEDOUT : native_pipe_windows_error(error);
      if (!completed) return native_pipe_windows_error(error);
      for (size_t role = 0u; role < 2u; ++role) {
        if (overlapped != &requests[role].overlapped) continue;
        if (!pending[role] || bytes == 0u || (size_t)bytes > fixture->payload_size - offsets[role])
          return TURBO_EPROTO;
        offsets[role] += (size_t)bytes;
        pending[role] = false;
        overlapped = NULL;
        break;
      }
      if (overlapped != NULL) return TURBO_EPROTO;
    }
  }
  return TURBO_OK;
}
#else
static int native_pipe_raw_transfer(native_pipe_fixture *fixture, native_pipe_stages *stages) {
  size_t sent_offset = 0u;
  size_t received_offset = 0u;
  while (sent_offset < fixture->payload_size || received_offset < fixture->payload_size) {
    bool progressed = false;
    if (received_offset < fixture->payload_size) {
      const uint64_t started = turbo_hrtime();
      const ssize_t bytes = read(fixture->handles[0], fixture->received + received_offset,
                                 fixture->payload_size - received_offset);
      const int native_error = bytes < 0 ? errno : 0;
      native_pipe_counter_add(&stages->submit_ns, turbo_hrtime() - started);
      ++stages->submits;
      if (bytes > 0) {
        received_offset += (size_t)bytes;
        progressed = true;
      } else if (bytes == 0) {
        return TURBO_EOF;
      } else if (native_error != EAGAIN && native_error != EWOULDBLOCK && native_error != EINTR) {
        return -native_error;
      }
    }
    if (sent_offset < fixture->payload_size) {
      const uint64_t started = turbo_hrtime();
      const ssize_t bytes = write(fixture->handles[1], fixture->sent + sent_offset,
                                  fixture->payload_size - sent_offset);
      const int native_error = bytes < 0 ? errno : 0;
      native_pipe_counter_add(&stages->submit_ns, turbo_hrtime() - started);
      ++stages->submits;
      if (bytes > 0) {
        sent_offset += (size_t)bytes;
        progressed = true;
      } else if (bytes == 0) {
        return TURBO_EIO;
      } else if (native_error != EAGAIN && native_error != EWOULDBLOCK && native_error != EINTR) {
        return -native_error;
      }
    }
    if (!progressed) return TURBO_EIO;
  }
  return TURBO_OK;
}
#endif

static int native_pipe_submit(native_pipe_fixture *fixture, const turbo_io_operation *operation,
                              turbo_io_request *request, native_pipe_stages *stages) {
  const uint64_t started = turbo_hrtime();
  const int status = turbo_io_backend_submit(&fixture->backend, operation, request);
  native_pipe_counter_add(&stages->submit_ns, turbo_hrtime() - started);
  ++stages->submits;
  return status;
}

static int native_pipe_native_transfer(native_pipe_fixture *fixture, native_pipe_stages *stages) {
  size_t sent_offset = 0u;
  size_t received_offset = 0u;
  bool read_pending = false;
  bool write_pending = false;
  while (sent_offset < fixture->payload_size || received_offset < fixture->payload_size) {
    turbo_io_completion events[NATIVE_PIPE_BATCH_CAPACITY];
    size_t event_count = 0u;
    int status;
    if (!read_pending && received_offset < fixture->payload_size) {
      const turbo_io_operation operation = {.kind = TURBO_IO_PIPE_READ,
                                            .endpoint = fixture->endpoints[0],
                                            .buffer = fixture->received + received_offset,
                                            .length = fixture->payload_size - received_offset,
                                            .user_data = NATIVE_PIPE_READ_USER_DATA};
      turbo_io_request request;
      status = native_pipe_submit(fixture, &operation, &request, stages);
      if (status != TURBO_OK) return status;
      read_pending = true;
    }
    if (!write_pending && sent_offset < fixture->payload_size) {
      const turbo_io_operation operation = {.kind = TURBO_IO_PIPE_WRITE,
                                            .endpoint = fixture->endpoints[1],
                                            .buffer = fixture->sent + sent_offset,
                                            .length = fixture->payload_size - sent_offset,
                                            .user_data = NATIVE_PIPE_WRITE_USER_DATA};
      turbo_io_request request;
      status = native_pipe_submit(fixture, &operation, &request, stages);
      if (status != TURBO_OK) return status;
      write_pending = true;
    }
    {
      const uint64_t started = turbo_hrtime();
      status = turbo_io_backend_observe(&fixture->backend, events, NATIVE_PIPE_BATCH_CAPACITY,
                                        NATIVE_PIPE_TIMEOUT_MS, &event_count);
      native_pipe_counter_add(&stages->observe_ns, turbo_hrtime() - started);
      ++stages->observes;
    }
    if (status != TURBO_OK) return status;
    if (event_count == 0u) return TURBO_EIO;
    for (size_t index = 0u; index < event_count; ++index) {
      const turbo_io_completion *event = &events[index];
      if (event->kind != TURBO_IO_COMPLETION_OK)
        return event->status != TURBO_OK ? event->status : TURBO_EIO;
      if (event->bytes == 0u) return TURBO_EIO;
      if (event->user_data == NATIVE_PIPE_READ_USER_DATA) {
        if (!read_pending || event->bytes > fixture->payload_size - received_offset)
          return TURBO_EPROTO;
        received_offset += event->bytes;
        read_pending = false;
      } else if (event->user_data == NATIVE_PIPE_WRITE_USER_DATA) {
        if (!write_pending || event->bytes > fixture->payload_size - sent_offset)
          return TURBO_EPROTO;
        sent_offset += event->bytes;
        write_pending = false;
      } else {
        return TURBO_EPROTO;
      }
    }
  }
  return TURBO_OK;
}

static int native_pipe_transfer(native_pipe_fixture *fixture, native_pipe_stages *stages) {
  return fixture->driver == NATIVE_PIPE_RAW ? native_pipe_raw_transfer(fixture, stages)
                                            : native_pipe_native_transfer(fixture, stages);
}

static int native_pipe_warmup(native_pipe_fixture *fixture) {
  native_pipe_stages stages = {0};
  for (size_t index = 0u; index < NATIVE_PIPE_WARMUP_TRANSFERS; ++index) {
    const int status = native_pipe_transfer(fixture, &stages);
    if (status != TURBO_OK) return status;
  }
  return memcmp(fixture->sent, fixture->received, fixture->payload_size) == 0 ? TURBO_OK
                                                                              : TURBO_EIO;
}

static int native_pipe_result_prepare(native_pipe_result *result, size_t payload_size) {
  const size_t sample_count = (size_t)NATIVE_PIPE_SAMPLES;
  const size_t transfers_per_sample = (size_t)NATIVE_PIPE_TRANSFERS_PER_SAMPLE;
  size_t latency_capacity;
  memset(result, 0, sizeof(*result));
  if (sample_count > SIZE_MAX / transfers_per_sample) return TURBO_EINVAL;
  latency_capacity = sample_count * transfers_per_sample;
  if (latency_capacity > SIZE_MAX / sizeof(*result->latencies)) return TURBO_EINVAL;
  result->latencies = (uint64_t *)malloc(latency_capacity * sizeof(*result->latencies));
  if (result->latencies == NULL) return TURBO_ENOMEM;
  result->payload_size = payload_size;
  result->latency_capacity = latency_capacity;
  return TURBO_OK;
}

static int native_pipe_measure_batch(native_pipe_fixture *fixture, native_pipe_result *result) {
  for (size_t index = 0u; index < NATIVE_PIPE_TRANSFERS_PER_SAMPLE; ++index) {
    uint64_t started;
    uint64_t elapsed;
    int status;
    if (result->latency_count >= result->latency_capacity) return TURBO_ENOBUFS;
    started = turbo_hrtime();
    status = native_pipe_transfer(fixture, &result->stages);
    elapsed = turbo_hrtime() - started;
    if (status != TURBO_OK) return status;
    result->latencies[result->latency_count++] = elapsed;
    native_pipe_counter_add(&result->wall_ns, elapsed);
  }
  return TURBO_OK;
}

static int native_pipe_u64_compare(const void *left, const void *right) {
  const uint64_t lhs = *(const uint64_t *)left;
  const uint64_t rhs = *(const uint64_t *)right;
  return lhs < rhs ? -1 : lhs > rhs;
}

static int native_pipe_result_finalize(native_pipe_result *result) {
  if (result->latency_count != result->latency_capacity) return TURBO_EIO;
  qsort(result->latencies, result->latency_count, sizeof(result->latencies[0]),
        native_pipe_u64_compare);
  result->p50_ns = result->latencies[(result->latency_count - 1u) * 50u / 100u];
  result->p95_ns = result->latencies[(result->latency_count - 1u) * 95u / 100u];
  return TURBO_OK;
}

static void native_pipe_result_destroy(native_pipe_result *result) {
  free(result->latencies);
  result->latencies = NULL;
  result->latency_count = 0u;
  result->latency_capacity = 0u;
}

static double native_pipe_throughput(const native_pipe_result *result) {
  const double bytes = (double)result->payload_size * (double)result->latency_count;
  return result->wall_ns == 0u ? 0.0
                               : bytes * 1000000000.0 / (double)result->wall_ns / (1024.0 * 1024.0);
}

static double native_pipe_delta(double candidate, double baseline) {
  return baseline == 0.0 ? 0.0 : (candidate / baseline - 1.0) * 100.0;
}

static double native_pipe_stage_mean(uint64_t elapsed, uint64_t operations) {
  return operations == 0u ? 0.0 : (double)elapsed / (double)operations;
}

static void native_pipe_print_tables(const native_pipe_result *raw,
                                     const native_pipe_result *native, size_t count) {
  printf("\n%s byte pipe: raw direct vs NativeIO (%d x %d one-way transfers)\n",
         NATIVE_PIPE_BACKEND_NAME, NATIVE_PIPE_SAMPLES, NATIVE_PIPE_TRANSFERS_PER_SAMPLE);
  printf("Bytes count application payload once per one-way transfer.\n");
  printf("\nPipe one-way latency\n");
  printf("| payload | raw p50 us | NativeIO p50 us | p50 delta | raw p95 us | "
         "NativeIO p95 us | p95 delta |\n");
  printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    printf("| %zu KiB | %.3f | %.3f | %+.2f%% | %.3f | %.3f | %+.2f%% |\n",
           raw[index].payload_size / 1024u, (double)raw[index].p50_ns / 1000.0,
           (double)native[index].p50_ns / 1000.0,
           native_pipe_delta((double)native[index].p50_ns, (double)raw[index].p50_ns),
           (double)raw[index].p95_ns / 1000.0, (double)native[index].p95_ns / 1000.0,
           native_pipe_delta((double)native[index].p95_ns, (double)raw[index].p95_ns));
  }
  printf("\nPipe throughput\n");
  printf("| payload | raw MiB/s | NativeIO MiB/s | delta |\n");
  printf("| ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    const double raw_throughput = native_pipe_throughput(&raw[index]);
    const double native_throughput = native_pipe_throughput(&native[index]);
    printf("| %zu KiB | %.2f | %.2f | %+.2f%% |\n", raw[index].payload_size / 1024u, raw_throughput,
           native_throughput, native_pipe_delta(native_throughput, raw_throughput));
  }
  printf("\nPipe stage means\n");
  printf("| payload | raw submit ns/call | NativeIO submit ns/call | "
         "NativeIO observe ns/call |\n");
  printf("| ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    printf("| %zu KiB | %.2f | %.2f | %.2f |\n", raw[index].payload_size / 1024u,
           native_pipe_stage_mean(raw[index].stages.submit_ns, raw[index].stages.submits),
           native_pipe_stage_mean(native[index].stages.submit_ns, native[index].stages.submits),
           native_pipe_stage_mean(native[index].stages.observe_ns, native[index].stages.observes));
  }
}

spec("NativeIO pipe benchmark") {
  bench("compares bounded one-way pipe transfers with the native backend") {
    native_pipe_result raw_results[sizeof(NATIVE_PIPE_PAYLOADS) / sizeof(NATIVE_PIPE_PAYLOADS[0])] =
        {0};
    native_pipe_result
        native_results[sizeof(NATIVE_PIPE_PAYLOADS) / sizeof(NATIVE_PIPE_PAYLOADS[0])] = {0};
    const size_t payload_count = sizeof(NATIVE_PIPE_PAYLOADS) / sizeof(NATIVE_PIPE_PAYLOADS[0]);

    for (size_t index = 0u; index < payload_count; ++index) {
      native_pipe_fixture fixture = {0};
      native_pipe_result *result = &raw_results[index];
      char title[96];
      int status = native_pipe_result_prepare(result, NATIVE_PIPE_PAYLOADS[index]);
      if (status == TURBO_OK)
        status = native_pipe_fixture_init(&fixture, NATIVE_PIPE_RAW, NATIVE_PIPE_PAYLOADS[index]);
      if (status == TURBO_OK) status = native_pipe_warmup(&fixture);
      (void)snprintf(title, sizeof(title), "%s pipe %zu KiB", NATIVE_PIPE_RAW_NAME,
                     NATIVE_PIPE_PAYLOADS[index] / 1024u);
      benchmark_io(title, NATIVE_PIPE_SAMPLES, NATIVE_PIPE_TRANSFERS_PER_SAMPLE,
                   NATIVE_PIPE_TRANSFERS_PER_SAMPLE * NATIVE_PIPE_PAYLOADS[index]) {
        if (status == TURBO_OK) status = native_pipe_measure_batch(&fixture, result);
      }
      if (status == TURBO_OK && memcmp(fixture.sent, fixture.received, fixture.payload_size) != 0)
        status = TURBO_EIO;
      {
        const int cleanup_status = native_pipe_fixture_destroy(&fixture);
        if (status == TURBO_OK) status = cleanup_status;
      }
      if (status == TURBO_OK) status = native_pipe_result_finalize(result);
      if (status != TURBO_OK) native_pipe_result_destroy(result);
      check_equal(status, TURBO_OK);
    }

    for (size_t index = 0u; index < payload_count; ++index) {
      native_pipe_fixture fixture = {0};
      native_pipe_result *result = &native_results[index];
      char title[96];
      int status = native_pipe_result_prepare(result, NATIVE_PIPE_PAYLOADS[index]);
      if (status == TURBO_OK)
        status =
            native_pipe_fixture_init(&fixture, NATIVE_PIPE_NATIVE_IO, NATIVE_PIPE_PAYLOADS[index]);
      if (status == TURBO_OK) status = native_pipe_warmup(&fixture);
      (void)snprintf(title, sizeof(title), "NativeIO %s pipe %zu KiB", NATIVE_PIPE_BACKEND_NAME,
                     NATIVE_PIPE_PAYLOADS[index] / 1024u);
      benchmark_io(title, NATIVE_PIPE_SAMPLES, NATIVE_PIPE_TRANSFERS_PER_SAMPLE,
                   NATIVE_PIPE_TRANSFERS_PER_SAMPLE * NATIVE_PIPE_PAYLOADS[index]) {
        if (status == TURBO_OK) status = native_pipe_measure_batch(&fixture, result);
      }
      if (status == TURBO_OK && memcmp(fixture.sent, fixture.received, fixture.payload_size) != 0)
        status = TURBO_EIO;
      {
        const int cleanup_status = native_pipe_fixture_destroy(&fixture);
        if (status == TURBO_OK) status = cleanup_status;
      }
      if (status == TURBO_OK) status = native_pipe_result_finalize(result);
      if (status != TURBO_OK) native_pipe_result_destroy(result);
      check_equal(status, TURBO_OK);
    }

    native_pipe_print_tables(raw_results, native_results, payload_count);
    for (size_t index = 0u; index < payload_count; ++index) {
      native_pipe_result_destroy(&native_results[index]);
      native_pipe_result_destroy(&raw_results[index]);
    }
  }
}
