#include <cflow/cflow.h>

#include "tinytest.h"

#include <salts/clock.h>
#include <salts/thread.h>
#include <salts/thread_pool.h>

#include <inttypes.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #include <windows.h>
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET adapter_bench_socket;
typedef int adapter_bench_socklen;
typedef HANDLE adapter_bench_pipe;
  #define ADAPTER_BENCH_INVALID_SOCKET INVALID_SOCKET
  #define ADAPTER_BENCH_INVALID_PIPE INVALID_HANDLE_VALUE
#else
  #include <errno.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <poll.h>
  #include <sys/resource.h>
  #include <sys/socket.h>
  #include <unistd.h>
typedef int adapter_bench_socket;
typedef socklen_t adapter_bench_socklen;
typedef int adapter_bench_pipe;
  #define ADAPTER_BENCH_INVALID_SOCKET (-1)
  #define ADAPTER_BENCH_INVALID_PIPE (-1)
#endif

enum {
  ADAPTER_BENCH_SAMPLES = 20,
  ADAPTER_BENCH_TRANSFERS_PER_SAMPLE = 256,
  ADAPTER_BENCH_WARMUP_TRANSFERS = 8,
  ADAPTER_BENCH_STAGE_TRANSFERS = 256,
  ADAPTER_BENCH_CPU_MAX_PASSES = 8,
  ADAPTER_BENCH_CPU_TARGET_NS = 50 * 1000 * 1000,
  ADAPTER_BENCH_TIMEOUT_MS = 5000,
  ADAPTER_BENCH_OWNER_POLL_TIMEOUT_MS = 1000,
  ADAPTER_BENCH_DRAIN_POLL_TIMEOUT_MS = 1,
  ADAPTER_BENCH_PIPE_BUFFER_CAPACITY = 65536,
  ADAPTER_BENCH_MAX_COMPLETIONS = 2,
  ADAPTER_BENCH_REACTIVE_SCHEDULER_CAPACITY = 8,
  ADAPTER_BENCH_REACTIVE_PUBLISHER_QUEUE_CAPACITY = 16,
  ADAPTER_BENCH_REACTIVE_WAIT_SLICE_NS = 10000000,
  ADAPTER_BENCH_REACTIVE_WAIT_LIMIT = 500,
  ADAPTER_BENCH_TOTAL_TRANSFERS = ADAPTER_BENCH_SAMPLES * ADAPTER_BENCH_TRANSFERS_PER_SAMPLE
};

static const uint64_t ADAPTER_BENCH_TIMEOUT_NS = UINT64_C(5000000000);

enum {
  ADAPTER_BENCH_THREAD_ROLE_NONE = 0,
  ADAPTER_BENCH_THREAD_ROLE_MAIN,
  ADAPTER_BENCH_THREAD_ROLE_PUBLISHER,
  ADAPTER_BENCH_THREAD_ROLE_SUBSCRIBER
};

static SALTS_THREAD_LOCAL int adapter_bench_thread_role;

static const size_t ADAPTER_BENCH_PAYLOADS[] = {1024u,       4u * 1024u,  8u * 1024u,
                                                16u * 1024u, 32u * 1024u, 64u * 1024u};

typedef enum adapter_bench_mode {
  ADAPTER_BENCH_DIRECT = 0,
  ADAPTER_BENCH_ACTOR,
  ADAPTER_BENCH_REACTIVE,
  ADAPTER_BENCH_MODE_COUNT
} adapter_bench_mode;

typedef enum adapter_bench_transport {
  ADAPTER_BENCH_TCP = 0,
  ADAPTER_BENCH_PIPE,
  ADAPTER_BENCH_TRANSPORT_COUNT
} adapter_bench_transport;

typedef enum adapter_bench_role { ADAPTER_BENCH_RECV = 0, ADAPTER_BENCH_SEND } adapter_bench_role;

enum { ADAPTER_BENCH_DIRECTION_COUNT = 2 };

typedef struct adapter_bench_stages {
  uint64_t admission_ns;
  uint64_t native_submit_ns;
  uint64_t observe_ns;
  uint64_t actor_transition_ns;
  uint64_t executor_delivery_ns;
  uint64_t acknowledge_ns;
  uint64_t reactive_subscription_ns;
  uint64_t reactive_owner_ns;
  uint64_t native_submit_calls;
  uint64_t observe_calls;
  uint64_t actor_operations;
} adapter_bench_stages;

typedef struct adapter_bench_result {
  size_t payload_size;
  uint64_t wall_ns;
  uint64_t cpu_ns;
  uint64_t cpu_transfers;
  uint64_t latencies[ADAPTER_BENCH_TOTAL_TRANSFERS];
  size_t latency_count;
  uint64_t p50_ns;
  uint64_t p95_ns;
  uint64_t p99_ns;
  uint64_t errors;
  uint64_t rejections;
  uint64_t stale_completions;
  adapter_bench_stages stages;
} adapter_bench_result;

typedef struct adapter_bench_actor_operation {
  native_io_operation native;
  adapter_bench_role role;
  size_t *release_count;
  salts_mutex_t *release_gate;
  salts_cond_t *release_changed;
} adapter_bench_actor_operation;

typedef struct adapter_bench_delivery {
  cflow_io_request_id request_id;
  adapter_bench_actor_operation *operation;
  cflow_io_completion completion;
} adapter_bench_delivery;

typedef struct adapter_bench_fixture {
  adapter_bench_mode mode;
  adapter_bench_transport transport;
  adapter_bench_role direction;
  adapter_bench_socket sockets[2];
  adapter_bench_pipe pipes[2];
#if defined(_WIN32)
  HANDLE peer_event;
#endif
  native_io_endpoint endpoints[2];
  native_io_backend direct;
  native_io_request direct_requests[2];
  bool direct_pending[2];
  cflow_io_native_adapter adapter;
  cflow_executor executor;
  cflow_io_actor actor;
  adapter_bench_actor_operation actor_operations[2];
  cflow_io_request_id actor_request_ids[2];
  bool actor_pending[2];
  cflow_graph surface;
  cflow_graph normalized;
  cflow_scheduler scheduler;
  cflow_publisher publisher;
  cflow_io_publisher_owner reactive_owner;
  cflow_subscription subscription;
  salts_threadpool_t *publisher_pool;
  salts_threadpool_t *peer_pool;
  salts_mutex_t reactive_gate;
  salts_cond_t reactive_changed;
  cflow_subscriber_callbacks subscriber_callbacks;
  cflow_subscriber subscriber;
  cflow_io_backend_ops adapter_ops;
  adapter_bench_stages *stages;
  adapter_bench_delivery deliveries[ADAPTER_BENCH_MAX_COMPLETIONS];
  size_t delivery_count;
  size_t release_count;
  cflow_io_lease_id next_lease;
  adapter_bench_actor_operation reactive_operations[2];
  size_t reactive_offsets[2];
  bool reactive_pending[2];
  size_t reactive_subscriber_values;
  uint64_t operation_count;
  const char *reactive_error;
  int reactive_setup_status;
  int reactive_wake_status;
  int reactive_drive_status;
  int reactive_cleanup_status;
  int peer_status;
  size_t reactive_observed;
  size_t reactive_publisher_callbacks;
  size_t reactive_subscriber_callbacks;
  size_t reactive_prepare_callbacks;
  size_t reactive_encode_callbacks;
  size_t reactive_role_collisions;
  _Atomic bool reactive_stage_enabled;
  _Atomic size_t reactive_stage_writers;
  adapter_bench_stages reactive_stages;
  bool reactive_validation_result;
  uint64_t validation_errors;
  uint64_t validation_rejections;
  uint64_t validation_stale_completions;
  unsigned char *sent;
  unsigned char *received;
  size_t payload_size;
  bool backend_initialized;
  bool sockets_created;
  bool pipes_created;
  bool actor_initialized;
  bool executor_initialized;
  bool surface_initialized;
  bool normalized_initialized;
  bool scheduler_initialized;
  bool reactive_mutex_initialized;
  bool reactive_cond_initialized;
  bool publisher_pool_initialized;
  bool peer_pool_initialized;
  bool reactive_drive_pending;
  bool reactive_stop_requested;
  bool reactive_loop_started;
  bool reactive_loop_stopped;
  bool reactive_owner_initialized;
  bool subscription_initialized;
} adapter_bench_fixture;

static const char *adapter_bench_transport_name(adapter_bench_transport transport) {
  return transport == ADAPTER_BENCH_TCP ? "TCP" : "Pipe";
}

static const char *adapter_bench_mode_name(adapter_bench_mode mode) {
  if (mode == ADAPTER_BENCH_DIRECT) return "NativeIO direct";
  return mode == ADAPTER_BENCH_ACTOR ? "Actor/NativeIO" : "Source(window=1)/NativeIO";
}

static const char *adapter_bench_direction_name(adapter_bench_role direction) {
  return direction == ADAPTER_BENCH_SEND ? "TX" : "RX";
}

static native_io_backend_kind adapter_bench_backend(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
  return NATIVE_IO_BACKEND_EPOLL;
#else
  return NATIVE_IO_BACKEND_KQUEUE;
#endif
}

static int adapter_bench_last_socket_error(void) {
#if defined(_WIN32)
  return -(int)WSAGetLastError();
#else
  return -errno;
#endif
}

static int adapter_bench_process_cpu_ns(uint64_t *cpu_ns) {
  if (cpu_ns == NULL) return SALTS_EINVAL;
#if defined(_WIN32)
  FILETIME created;
  FILETIME exited;
  FILETIME kernel;
  FILETIME user;
  ULARGE_INTEGER kernel_ticks;
  ULARGE_INTEGER user_ticks;

  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return SALTS_EIO;
  kernel_ticks.LowPart = kernel.dwLowDateTime;
  kernel_ticks.HighPart = kernel.dwHighDateTime;
  user_ticks.LowPart = user.dwLowDateTime;
  user_ticks.HighPart = user.dwHighDateTime;
  *cpu_ns = (kernel_ticks.QuadPart + user_ticks.QuadPart) * UINT64_C(100);
#else
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) != 0) return -errno;
  *cpu_ns =
      ((uint64_t)usage.ru_utime.tv_sec + (uint64_t)usage.ru_stime.tv_sec) * UINT64_C(1000000000) +
      ((uint64_t)usage.ru_utime.tv_usec + (uint64_t)usage.ru_stime.tv_usec) * UINT64_C(1000);
#endif
  return SALTS_OK;
}

static void adapter_bench_close_socket(adapter_bench_socket socket_value) {
  if (socket_value == ADAPTER_BENCH_INVALID_SOCKET) return;
#if defined(_WIN32)
  (void)closesocket(socket_value);
#else
  (void)close(socket_value);
#endif
}

static int adapter_bench_make_tcp_pair(adapter_bench_socket sockets[2]) {
  adapter_bench_socket listener = ADAPTER_BENCH_INVALID_SOCKET;
  struct sockaddr_in address;
  adapter_bench_socklen address_length = (adapter_bench_socklen)sizeof(address);
  int status = SALTS_OK;

  sockets[0] = ADAPTER_BENCH_INVALID_SOCKET;
  sockets[1] = ADAPTER_BENCH_INVALID_SOCKET;
  listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == ADAPTER_BENCH_INVALID_SOCKET) return adapter_bench_last_socket_error();
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (bind(listener, (const struct sockaddr *)&address, (adapter_bench_socklen)sizeof(address)) !=
          0 ||
      getsockname(listener, (struct sockaddr *)&address, &address_length) != 0 ||
      listen(listener, 1) != 0)
    status = adapter_bench_last_socket_error();
  if (status == SALTS_OK) {
    sockets[0] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockets[0] == ADAPTER_BENCH_INVALID_SOCKET) status = adapter_bench_last_socket_error();
  }
  if (status == SALTS_OK && connect(sockets[0], (const struct sockaddr *)&address,
                                    (adapter_bench_socklen)sizeof(address)) != 0)
    status = adapter_bench_last_socket_error();
  if (status == SALTS_OK) {
    sockets[1] = accept(listener, NULL, NULL);
    if (sockets[1] == ADAPTER_BENCH_INVALID_SOCKET) status = adapter_bench_last_socket_error();
  }
  adapter_bench_close_socket(listener);
  if (status != SALTS_OK) {
    adapter_bench_close_socket(sockets[0]);
    adapter_bench_close_socket(sockets[1]);
    sockets[0] = ADAPTER_BENCH_INVALID_SOCKET;
    sockets[1] = ADAPTER_BENCH_INVALID_SOCKET;
  }
  return status;
}

static int adapter_bench_set_socket_timeout(adapter_bench_socket socket_value) {
#if defined(_WIN32)
  const DWORD timeout_ms = ADAPTER_BENCH_TIMEOUT_MS;
  const char *timeout_value = (const char *)&timeout_ms;
  const adapter_bench_socklen timeout_size = (adapter_bench_socklen)sizeof(timeout_ms);
#else
  const struct timeval timeout = {ADAPTER_BENCH_TIMEOUT_MS / 1000,
                                  (ADAPTER_BENCH_TIMEOUT_MS % 1000) * 1000};
  const char *timeout_value = (const char *)&timeout;
  const adapter_bench_socklen timeout_size = (adapter_bench_socklen)sizeof(timeout);
#endif
  if (setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, timeout_value, timeout_size) != 0 ||
      setsockopt(socket_value, SOL_SOCKET, SO_SNDTIMEO, timeout_value, timeout_size) != 0)
    return adapter_bench_last_socket_error();
  return SALTS_OK;
}

static void adapter_bench_close_pipe(adapter_bench_pipe pipe_handle) {
  if (pipe_handle == ADAPTER_BENCH_INVALID_PIPE) return;
#if defined(_WIN32)
  (void)CloseHandle(pipe_handle);
#else
  (void)close(pipe_handle);
#endif
}

static int adapter_bench_make_pipe_pair(adapter_bench_pipe pipes[2]) {
#if defined(_WIN32)
  static LONG sequence = 0;
  char name[128];
  OVERLAPPED connected = {0};
  HANDLE event = NULL;
  DWORD error = ERROR_SUCCESS;
  BOOL pending = FALSE;
  int name_length;

  pipes[0] = ADAPTER_BENCH_INVALID_PIPE;
  pipes[1] = ADAPTER_BENCH_INVALID_PIPE;
  name_length = snprintf(name, sizeof(name), "\\\\.\\pipe\\cflow-adapter-bench-%lu-%ld",
                         GetCurrentProcessId(), InterlockedIncrement(&sequence));
  if (name_length < 0 || (size_t)name_length >= sizeof(name)) return SALTS_ERANGE;
  pipes[1] = CreateNamedPipeA(name, PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1u,
                              ADAPTER_BENCH_PIPE_BUFFER_CAPACITY,
                              ADAPTER_BENCH_PIPE_BUFFER_CAPACITY, 0u, NULL);
  if (pipes[1] == ADAPTER_BENCH_INVALID_PIPE) return -(int)GetLastError();
  event = CreateEventA(NULL, TRUE, FALSE, NULL);
  if (event == NULL) {
    error = GetLastError();
    goto failed;
  }
  connected.hEvent = event;
  if (!ConnectNamedPipe(pipes[1], &connected)) {
    error = GetLastError();
    if (error == ERROR_IO_PENDING) pending = TRUE;
    else if (error != ERROR_PIPE_CONNECTED) goto failed;
  }
  pipes[0] = CreateFileA(name, GENERIC_READ, 0u, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
  if (pipes[0] == ADAPTER_BENCH_INVALID_PIPE) {
    error = GetLastError();
    goto failed;
  }
  if (pending) {
    DWORD transferred = 0u;
    if (!GetOverlappedResult(pipes[1], &connected, &transferred, TRUE)) {
      error = GetLastError();
      goto failed;
    }
  }
  (void)CloseHandle(event);
  return SALTS_OK;

failed:
  adapter_bench_close_pipe(pipes[0]);
  adapter_bench_close_pipe(pipes[1]);
  if (event != NULL) (void)CloseHandle(event);
  pipes[0] = ADAPTER_BENCH_INVALID_PIPE;
  pipes[1] = ADAPTER_BENCH_INVALID_PIPE;
  return error == ERROR_SUCCESS ? SALTS_EIO : -(int)error;
#else
  int status = SALTS_OK;
  pipes[0] = ADAPTER_BENCH_INVALID_PIPE;
  pipes[1] = ADAPTER_BENCH_INVALID_PIPE;
  if (pipe(pipes) != 0) return -errno;
  for (size_t index = 0u; index < 2u; ++index) {
    const int flags = fcntl(pipes[index], F_GETFL, 0);
    if (flags < 0 || fcntl(pipes[index], F_SETFL, flags | O_NONBLOCK) != 0) {
      status = -errno;
      break;
    }
  }
  if (status != SALTS_OK) {
    adapter_bench_close_pipe(pipes[0]);
    adapter_bench_close_pipe(pipes[1]);
    pipes[0] = ADAPTER_BENCH_INVALID_PIPE;
    pipes[1] = ADAPTER_BENCH_INVALID_PIPE;
  }
  return status;
#endif
}

static bool adapter_bench_retryable_io_error(void) {
#if defined(_WIN32)
  return WSAGetLastError() == WSAEINTR;
#else
  return errno == EINTR;
#endif
}

static bool adapter_bench_timed_out_io_error(void) {
#if defined(_WIN32)
  const int error = WSAGetLastError();
  return error == WSAETIMEDOUT || error == WSAEWOULDBLOCK;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static int adapter_bench_peer_socket_transfer(adapter_bench_fixture *fixture) {
  const bool send_payload = fixture->direction == ADAPTER_BENCH_RECV;
  unsigned char *buffer = send_payload ? fixture->sent : fixture->received;
  size_t offset = 0u;

  while (offset < fixture->payload_size) {
    const size_t remaining = fixture->payload_size - offset;
    const int length = remaining > (size_t)INT_MAX ? INT_MAX : (int)remaining;
#if defined(_WIN32)
    const int transferred =
        send_payload ? send(fixture->sockets[1], (const char *)buffer + offset, length, 0)
                     : recv(fixture->sockets[1], (char *)buffer + offset, length, 0);
    if (transferred == SOCKET_ERROR) {
#else
    const ssize_t transferred =
        send_payload
  #if defined(MSG_NOSIGNAL)
            ? send(fixture->sockets[1], buffer + offset, (size_t)length, MSG_NOSIGNAL)
  #else
            ? send(fixture->sockets[1], buffer + offset, (size_t)length, 0)
  #endif
            : recv(fixture->sockets[1], buffer + offset, (size_t)length, 0);
    if (transferred < 0) {
#endif
      if (adapter_bench_timed_out_io_error()) return SALTS_ETIMEDOUT;
      if (!adapter_bench_retryable_io_error()) return adapter_bench_last_socket_error();
      continue;
    }
    if (transferred == 0) return SALTS_EOF;
    offset += (size_t)transferred;
  }
  return SALTS_OK;
}

static int adapter_bench_peer_pipe_transfer(adapter_bench_fixture *fixture) {
  const bool write_payload = fixture->direction == ADAPTER_BENCH_RECV;
  unsigned char *buffer = write_payload ? fixture->sent : fixture->received;
  const adapter_bench_pipe pipe_handle = fixture->pipes[write_payload ? 1u : 0u];
  size_t offset = 0u;

  while (offset < fixture->payload_size) {
    const size_t remaining = fixture->payload_size - offset;
#if defined(_WIN32)
    const DWORD length = remaining > (size_t)MAXDWORD ? MAXDWORD : (DWORD)remaining;
    OVERLAPPED operation = {0};
    DWORD transferred = 0u;
    DWORD error;
    DWORD wait_status;

    operation.hEvent = fixture->peer_event;
    if (!ResetEvent(operation.hEvent)) return -(int)GetLastError();
    if (!(write_payload
              ? WriteFile(pipe_handle, buffer + offset, length, &transferred, &operation)
              : ReadFile(pipe_handle, buffer + offset, length, &transferred, &operation))) {
      error = GetLastError();
      if (error != ERROR_IO_PENDING) return -(int)error;
      wait_status = WaitForSingleObject(operation.hEvent, ADAPTER_BENCH_TIMEOUT_MS);
      if (wait_status != WAIT_OBJECT_0) {
        (void)CancelIoEx(pipe_handle, &operation);
        (void)GetOverlappedResult(pipe_handle, &operation, &transferred, TRUE);
        return wait_status == WAIT_TIMEOUT ? SALTS_ETIMEDOUT : SALTS_EIO;
      }
      if (!GetOverlappedResult(pipe_handle, &operation, &transferred, FALSE)) {
        error = GetLastError();
        return -(int)error;
      }
    }
#else
    struct pollfd ready = {pipe_handle, write_payload ? POLLOUT : POLLIN, 0};
    int wait_status;
    ssize_t transferred;

    do {
      wait_status = poll(&ready, 1u, ADAPTER_BENCH_TIMEOUT_MS);
    } while (wait_status < 0 && errno == EINTR);
    if (wait_status == 0) return SALTS_ETIMEDOUT;
    if (wait_status < 0) return -errno;
    if ((ready.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) return SALTS_EIO;
    transferred = write_payload ? write(pipe_handle, buffer + offset, remaining)
                                : read(pipe_handle, buffer + offset, remaining);
    if (transferred < 0) {
      if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) return -errno;
      continue;
    }
#endif
    if (transferred == 0u) return SALTS_EOF;
    offset += (size_t)transferred;
  }
  return SALTS_OK;
}

static void adapter_bench_peer_task(void *user) {
  adapter_bench_fixture *fixture = (adapter_bench_fixture *)user;
  fixture->peer_status = fixture->transport == ADAPTER_BENCH_TCP
                             ? adapter_bench_peer_socket_transfer(fixture)
                             : adapter_bench_peer_pipe_transfer(fixture);
}

static void adapter_bench_counter_add(uint64_t *counter, uint64_t value) {
  *counter = UINT64_MAX - *counter < value ? UINT64_MAX : *counter + value;
}

static adapter_bench_stages *adapter_bench_stage_acquire(adapter_bench_fixture *fixture) {
  if (fixture->mode != ADAPTER_BENCH_REACTIVE) return fixture->stages;
  if (!atomic_load(&fixture->reactive_stage_enabled)) return NULL;
  (void)atomic_fetch_add(&fixture->reactive_stage_writers, 1u);
  if (!atomic_load(&fixture->reactive_stage_enabled)) {
    (void)atomic_fetch_sub(&fixture->reactive_stage_writers, 1u);
    return NULL;
  }
  return &fixture->reactive_stages;
}

static void adapter_bench_stage_release(adapter_bench_fixture *fixture,
                                        adapter_bench_stages *stages) {
  if (fixture->mode != ADAPTER_BENCH_REACTIVE || stages == NULL) return;
  (void)atomic_fetch_sub(&fixture->reactive_stage_writers, 1u);
}

static int adapter_bench_stage_begin(adapter_bench_fixture *fixture, adapter_bench_stages *stages) {
  if (fixture->mode != ADAPTER_BENCH_REACTIVE) {
    fixture->stages = stages;
    return SALTS_OK;
  }
  if (atomic_load(&fixture->reactive_stage_enabled) ||
      atomic_load(&fixture->reactive_stage_writers) != 0u)
    return SALTS_EBUSY;
  memset(&fixture->reactive_stages, 0, sizeof(fixture->reactive_stages));
  atomic_store(&fixture->reactive_stage_enabled, true);
  return SALTS_OK;
}

static int adapter_bench_stage_end(adapter_bench_fixture *fixture, adapter_bench_stages *stages) {
  const uint64_t started = salts_hrtime();

  if (fixture->mode != ADAPTER_BENCH_REACTIVE) {
    fixture->stages = NULL;
    return SALTS_OK;
  }
  atomic_store(&fixture->reactive_stage_enabled, false);
  while (atomic_load(&fixture->reactive_stage_writers) != 0u) {
    if (salts_hrtime() - started >= ADAPTER_BENCH_TIMEOUT_NS) return SALTS_ETIMEDOUT;
    salts_thread_yield();
  }
  *stages = fixture->reactive_stages;
  return SALTS_OK;
}

static native_io_operation adapter_bench_operation(adapter_bench_fixture *fixture, size_t role,
                                                   size_t offset) {
  const bool send = role == ADAPTER_BENCH_SEND;
  const size_t endpoint_index =
      fixture->transport == ADAPTER_BENCH_TCP ? 0u : (size_t)fixture->direction;
  native_io_operation operation = {
      fixture->transport == ADAPTER_BENCH_TCP
          ? (send ? NATIVE_IO_OPERATION_TCP_SEND : NATIVE_IO_OPERATION_TCP_RECV)
          : (send ? NATIVE_IO_OPERATION_PIPE_WRITE : NATIVE_IO_OPERATION_PIPE_READ),
      fixture->endpoints[endpoint_index],
      (send ? fixture->sent : fixture->received) + offset,
      fixture->payload_size - offset,
      (uintptr_t)(role + 1u),
      NULL,
      0u,
      0u};
  return operation;
}

static int adapter_bench_compare_u64(const void *left, const void *right) {
  const uint64_t a = *(const uint64_t *)left;
  const uint64_t b = *(const uint64_t *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static void adapter_bench_release(void *operation_user) {
  adapter_bench_actor_operation *operation = (adapter_bench_actor_operation *)operation_user;
  if (operation->release_gate != NULL) salts_mutex_lock(operation->release_gate);
  ++*operation->release_count;
  if (operation->release_changed != NULL) salts_cond_broadcast(operation->release_changed);
  if (operation->release_gate != NULL) salts_mutex_unlock(operation->release_gate);
}

static void adapter_bench_complete(void *completion_user, cflow_io_request_id request_id,
                                   cflow_io_lease_id lease_id, void *operation_user,
                                   const cflow_io_completion *completion) {
  adapter_bench_fixture *fixture = (adapter_bench_fixture *)completion_user;
  adapter_bench_delivery *delivery;

  (void)lease_id;
  if (fixture->delivery_count >= ADAPTER_BENCH_MAX_COMPLETIONS) return;
  delivery = &fixture->deliveries[fixture->delivery_count++];
  delivery->request_id = request_id;
  delivery->operation = (adapter_bench_actor_operation *)operation_user;
  delivery->completion = *completion;
}

static cflow_io_publisher_prepare_status
adapter_bench_reactive_prepare(void *user, cflow_io_operation *operation, const char **error) {
  adapter_bench_fixture *fixture = (adapter_bench_fixture *)user;
  adapter_bench_stages *stages = adapter_bench_stage_acquire(fixture);
  cflow_io_publisher_prepare_status status = CFLOW_IO_PUBLISHER_PREPARE_DONE;

  (void)error;
  salts_mutex_lock(&fixture->reactive_gate);
  if (adapter_bench_thread_role != ADAPTER_BENCH_THREAD_ROLE_NONE &&
      adapter_bench_thread_role != ADAPTER_BENCH_THREAD_ROLE_SUBSCRIBER)
    ++fixture->reactive_role_collisions;
  adapter_bench_thread_role = ADAPTER_BENCH_THREAD_ROLE_SUBSCRIBER;
  ++fixture->reactive_prepare_callbacks;
  {
    const size_t role = (size_t)fixture->direction;
    adapter_bench_actor_operation *prepared;
    if (fixture->reactive_pending[role] ||
        fixture->reactive_offsets[role] >= fixture->payload_size) {
      salts_mutex_unlock(&fixture->reactive_gate);
      adapter_bench_stage_release(fixture, stages);
      return status;
    }
    prepared = &fixture->reactive_operations[role];
    prepared->native = adapter_bench_operation(fixture, role, fixture->reactive_offsets[role]);
    prepared->role = fixture->direction;
    prepared->release_count = &fixture->release_count;
    prepared->release_gate = &fixture->reactive_gate;
    prepared->release_changed = &fixture->reactive_changed;
    fixture->reactive_pending[role] = true;
    operation->user = prepared;
    operation->release = adapter_bench_release;
    ++fixture->operation_count;
    if (stages != NULL) ++stages->actor_operations;
    status = CFLOW_IO_PUBLISHER_PREPARE_OPERATION;
  }
  salts_mutex_unlock(&fixture->reactive_gate);
  adapter_bench_stage_release(fixture, stages);
  return status;
}

static cflow_read_status adapter_bench_reactive_encode(void *user, cflow_io_request_id request_id,
                                                       cflow_io_lease_id lease_id,
                                                       void *operation_user,
                                                       const cflow_io_completion *completion,
                                                       void *out_value, const char **error) {
  static const char completion_error[] = "Reactive benchmark received a non-success completion";
  adapter_bench_fixture *fixture = (adapter_bench_fixture *)user;
  adapter_bench_actor_operation *operation = (adapter_bench_actor_operation *)operation_user;
  const size_t role = (size_t)operation->role;

  (void)request_id;
  (void)lease_id;
  salts_mutex_lock(&fixture->reactive_gate);
  if (adapter_bench_thread_role != ADAPTER_BENCH_THREAD_ROLE_NONE &&
      adapter_bench_thread_role != ADAPTER_BENCH_THREAD_ROLE_PUBLISHER)
    ++fixture->reactive_role_collisions;
  adapter_bench_thread_role = ADAPTER_BENCH_THREAD_ROLE_PUBLISHER;
  ++fixture->reactive_encode_callbacks;
  if (role >= 2u || !fixture->reactive_pending[role] ||
      completion->kind != CFLOW_IO_COMPLETION_OK || completion->bytes == 0u ||
      completion->bytes > fixture->payload_size - fixture->reactive_offsets[role]) {
    *error = completion_error;
    salts_mutex_unlock(&fixture->reactive_gate);
    return CFLOW_READ_ERROR;
  }
  fixture->reactive_offsets[role] += completion->bytes;
  fixture->reactive_pending[role] = false;
  *(int *)out_value = (int)completion->bytes;
  salts_cond_broadcast(&fixture->reactive_changed);
  salts_mutex_unlock(&fixture->reactive_gate);
  return CFLOW_READ_VALUE;
}

static bool adapter_bench_reactive_subscriber_value(void *user, const cmeta_type_desc *type,
                                                    const void *value) {
  adapter_bench_fixture *fixture = (adapter_bench_fixture *)user;
  if (!cmeta_type_equal(type, &cmeta_type_int) || value == NULL || *(const int *)value <= 0)
    return false;
  salts_mutex_lock(&fixture->reactive_gate);
  if (adapter_bench_thread_role != ADAPTER_BENCH_THREAD_ROLE_NONE &&
      adapter_bench_thread_role != ADAPTER_BENCH_THREAD_ROLE_SUBSCRIBER)
    ++fixture->reactive_role_collisions;
  adapter_bench_thread_role = ADAPTER_BENCH_THREAD_ROLE_SUBSCRIBER;
  ++fixture->reactive_subscriber_callbacks;
  ++fixture->reactive_subscriber_values;
  salts_cond_broadcast(&fixture->reactive_changed);
  salts_mutex_unlock(&fixture->reactive_gate);
  return true;
}

static void adapter_bench_reactive_subscriber_error(void *user, const char *message) {
  adapter_bench_fixture *fixture = (adapter_bench_fixture *)user;
  salts_mutex_lock(&fixture->reactive_gate);
  fixture->reactive_error = message;
  salts_cond_broadcast(&fixture->reactive_changed);
  salts_mutex_unlock(&fixture->reactive_gate);
}

static void adapter_bench_reactive_subscriber_done(void *user) {
  adapter_bench_fixture *fixture = (adapter_bench_fixture *)user;
  salts_mutex_lock(&fixture->reactive_gate);
  if (fixture->reactive_error == NULL)
    fixture->reactive_error = "Reactive benchmark terminated unexpectedly";
  salts_cond_broadcast(&fixture->reactive_changed);
  salts_mutex_unlock(&fixture->reactive_gate);
}

static void adapter_bench_reactive_drive_task(void *user) {
  adapter_bench_fixture *fixture = (adapter_bench_fixture *)user;

  salts_mutex_lock(&fixture->reactive_gate);
  if (adapter_bench_thread_role != ADAPTER_BENCH_THREAD_ROLE_NONE &&
      adapter_bench_thread_role != ADAPTER_BENCH_THREAD_ROLE_PUBLISHER)
    ++fixture->reactive_role_collisions;
  adapter_bench_thread_role = ADAPTER_BENCH_THREAD_ROLE_PUBLISHER;
  ++fixture->reactive_publisher_callbacks;
  salts_mutex_unlock(&fixture->reactive_gate);

  for (;;) {
    adapter_bench_stages *stages;
    uint64_t started;
    size_t observed = 0u;
    int status;

    salts_mutex_lock(&fixture->reactive_gate);
    while (!fixture->reactive_drive_pending && !fixture->reactive_stop_requested)
      salts_cond_wait(&fixture->reactive_changed, &fixture->reactive_gate);
    if (fixture->reactive_stop_requested) {
      salts_mutex_unlock(&fixture->reactive_gate);
      break;
    }
    fixture->reactive_drive_pending = false;
    salts_mutex_unlock(&fixture->reactive_gate);

    stages = adapter_bench_stage_acquire(fixture);
    started = stages != NULL ? salts_hrtime() : 0u;
    status = cflow_io_native_adapter_drive_publisher(&fixture->adapter, &fixture->reactive_owner,
                                                     ADAPTER_BENCH_OWNER_POLL_TIMEOUT_MS, 64u,
                                                     &observed);

    if (stages != NULL)
      adapter_bench_counter_add(&stages->reactive_owner_ns, salts_hrtime() - started);
    adapter_bench_stage_release(fixture, stages);

    salts_mutex_lock(&fixture->reactive_gate);
    if (status != SALTS_OK && fixture->reactive_drive_status == SALTS_OK)
      fixture->reactive_drive_status = status;
    fixture->reactive_observed += observed;
    salts_cond_broadcast(&fixture->reactive_changed);
    salts_mutex_unlock(&fixture->reactive_gate);
    if (status != SALTS_OK) break;
  }
  salts_mutex_lock(&fixture->reactive_gate);
  fixture->reactive_loop_stopped = true;
  salts_cond_broadcast(&fixture->reactive_changed);
  salts_mutex_unlock(&fixture->reactive_gate);
}

static void adapter_bench_reactive_drive(void *user) {
  adapter_bench_fixture *fixture = (adapter_bench_fixture *)user;
  const int status = cflow_io_native_adapter_wake(&fixture->adapter);

  salts_mutex_lock(&fixture->reactive_gate);
  fixture->reactive_drive_pending = true;
  if (status != SALTS_OK && fixture->reactive_wake_status == SALTS_OK)
    fixture->reactive_wake_status = status;
  salts_cond_broadcast(&fixture->reactive_changed);
  salts_mutex_unlock(&fixture->reactive_gate);
}

static int adapter_bench_reactive_stop_owner(adapter_bench_fixture *fixture) {
  bool loop_started;
  bool loop_stopped;
  int status;
  int wait_status;

  salts_mutex_lock(&fixture->reactive_gate);
  loop_started = fixture->reactive_loop_started;
  loop_stopped = fixture->reactive_loop_stopped;
  salts_mutex_unlock(&fixture->reactive_gate);
  if (!loop_started || loop_stopped) return SALTS_OK;
  status = cflow_io_native_adapter_wake(&fixture->adapter);
  salts_mutex_lock(&fixture->reactive_gate);
  fixture->reactive_stop_requested = true;
  salts_cond_broadcast(&fixture->reactive_changed);
  salts_mutex_unlock(&fixture->reactive_gate);
  wait_status = salts_threadpool_wait_status(fixture->publisher_pool);
  if (status == SALTS_OK) status = wait_status;
  salts_mutex_lock(&fixture->reactive_gate);
  loop_stopped = fixture->reactive_loop_stopped;
  salts_mutex_unlock(&fixture->reactive_gate);
  if (!loop_stopped && status == SALTS_OK) status = SALTS_EPROTO;
  return status;
}

static int adapter_bench_timed_submit(void *backend_user, cflow_io_actor *actor,
                                      cflow_io_request_id request_id, cflow_io_lease_id lease_id,
                                      void *operation_user) {
  adapter_bench_fixture *fixture = (adapter_bench_fixture *)backend_user;
  adapter_bench_stages *stages;
  uint64_t started = 0u;
  int status;

  stages = adapter_bench_stage_acquire(fixture);
  if (stages == NULL)
    return fixture->adapter_ops.submit(&fixture->adapter, actor, request_id, lease_id,
                                       operation_user);
  started = salts_hrtime();
  status =
      fixture->adapter_ops.submit(&fixture->adapter, actor, request_id, lease_id, operation_user);
  adapter_bench_counter_add(&stages->native_submit_ns, salts_hrtime() - started);
  ++stages->native_submit_calls;
  adapter_bench_stage_release(fixture, stages);
  return status;
}

static int adapter_bench_timed_cancel(void *backend_user, cflow_io_request_id request_id) {
  adapter_bench_fixture *fixture = (adapter_bench_fixture *)backend_user;
  return fixture->adapter_ops.cancel(&fixture->adapter, request_id);
}

static void adapter_bench_keep_first_status(int *first_status, int status) {
  if (*first_status == SALTS_OK && status != SALTS_OK) *first_status = status;
}

static int adapter_bench_backend_init(adapter_bench_fixture *fixture,
                                      const native_io_backend_config *backend_config) {
  int status;

  if (fixture->mode == ADAPTER_BENCH_DIRECT) {
    status = native_io_backend_init(&fixture->direct, backend_config);
  } else {
    const cflow_io_native_adapter_config adapter_config = {*backend_config};
    status = cflow_io_native_adapter_init(&fixture->adapter, &adapter_config);
  }
  if (status != SALTS_OK) return status;
  fixture->backend_initialized = true;
  return SALTS_OK;
}

static int adapter_bench_backend_attach(adapter_bench_fixture *fixture) {
  const size_t index = fixture->transport == ADAPTER_BENCH_TCP ? 0u : (size_t)fixture->direction;
  const uintptr_t native_handle = fixture->transport == ADAPTER_BENCH_TCP
                                      ? (uintptr_t)fixture->sockets[index]
                                      : (uintptr_t)fixture->pipes[index];
  int status;

  if (fixture->transport == ADAPTER_BENCH_TCP) {
    status = fixture->mode == ADAPTER_BENCH_DIRECT
                 ? native_io_backend_attach_socket(&fixture->direct, native_handle,
                                                   &fixture->endpoints[index])
                 : cflow_io_native_adapter_attach_socket(&fixture->adapter, native_handle,
                                                         &fixture->endpoints[index]);
  } else {
    status = fixture->mode == ADAPTER_BENCH_DIRECT
                 ? native_io_backend_attach_pipe(&fixture->direct, native_handle,
                                                 NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE,
                                                 &fixture->endpoints[index])
                 : cflow_io_native_adapter_attach_pipe(&fixture->adapter, native_handle,
                                                       NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE,
                                                       &fixture->endpoints[index]);
  }
  return status;
}

typedef struct adapter_bench_reactive_setup {
  adapter_bench_fixture *fixture;
  native_io_backend_config backend_config;
  bool attach;
  int status;
} adapter_bench_reactive_setup;

static void adapter_bench_reactive_setup_task(void *user) {
  adapter_bench_reactive_setup *setup = (adapter_bench_reactive_setup *)user;

  adapter_bench_thread_role = ADAPTER_BENCH_THREAD_ROLE_PUBLISHER;
  setup->status = setup->attach
                      ? adapter_bench_backend_attach(setup->fixture)
                      : adapter_bench_backend_init(setup->fixture, &setup->backend_config);
}

static int adapter_bench_fixture_init(adapter_bench_fixture *fixture,
                                      adapter_bench_transport transport, adapter_bench_mode mode,
                                      adapter_bench_role direction, size_t payload_size,
                                      adapter_bench_stages *stages) {
  const native_io_backend_config backend_config = {adapter_bench_backend(), 1u, 1u, 1u};
  int status;

  if (fixture == NULL || payload_size == 0u) return SALTS_EINVAL;
  memset(fixture, 0, sizeof(*fixture));
  atomic_init(&fixture->reactive_stage_enabled, false);
  atomic_init(&fixture->reactive_stage_writers, 0u);
  fixture->mode = mode;
  fixture->transport = transport;
  fixture->direction = direction;
  fixture->sockets[0] = ADAPTER_BENCH_INVALID_SOCKET;
  fixture->sockets[1] = ADAPTER_BENCH_INVALID_SOCKET;
  fixture->pipes[0] = ADAPTER_BENCH_INVALID_PIPE;
  fixture->pipes[1] = ADAPTER_BENCH_INVALID_PIPE;
  fixture->payload_size = payload_size;
  fixture->stages = stages;
  fixture->next_lease = 1u;
  fixture->reactive_setup_status = SALTS_OK;
  fixture->reactive_wake_status = SALTS_OK;
  fixture->reactive_drive_status = SALTS_OK;
  adapter_bench_thread_role = ADAPTER_BENCH_THREAD_ROLE_MAIN;
  fixture->sent = (unsigned char *)malloc(payload_size);
  fixture->received = (unsigned char *)malloc(payload_size);
  if (fixture->sent == NULL || fixture->received == NULL) return SALTS_ENOMEM;
  memset(fixture->sent, 0x5au, payload_size);

  if (mode == ADAPTER_BENCH_REACTIVE) {
    const salts_threadpool_config_t pool_config = {1,
                                                   ADAPTER_BENCH_REACTIVE_PUBLISHER_QUEUE_CAPACITY};
    adapter_bench_reactive_setup setup = {fixture, backend_config, false, SALTS_EBUSY};

    salts_mutex_init(&fixture->reactive_gate);
    if (fixture->reactive_gate == NULL) return SALTS_ENOMEM;
    fixture->reactive_mutex_initialized = true;
    salts_cond_init(&fixture->reactive_changed);
    if (fixture->reactive_changed == NULL) return SALTS_ENOMEM;
    fixture->reactive_cond_initialized = true;
    fixture->publisher_pool = salts_threadpool_create_with_config(&pool_config);
    if (fixture->publisher_pool == NULL) return SALTS_ENOMEM;
    fixture->publisher_pool_initialized = true;
    status =
        salts_threadpool_submit(fixture->publisher_pool, adapter_bench_reactive_setup_task, &setup);
    if (status == SALTS_OK) status = salts_threadpool_wait_status(fixture->publisher_pool);
    if (status == SALTS_OK) status = setup.status;
  } else {
    status = adapter_bench_backend_init(fixture, &backend_config);
  }
  if (status != SALTS_OK) return status;

  status = transport == ADAPTER_BENCH_TCP ? adapter_bench_make_tcp_pair(fixture->sockets)
                                          : adapter_bench_make_pipe_pair(fixture->pipes);
  if (status != SALTS_OK) return status;
  fixture->sockets_created = transport == ADAPTER_BENCH_TCP;
  fixture->pipes_created = transport == ADAPTER_BENCH_PIPE;
#if defined(_WIN32)
  if (transport == ADAPTER_BENCH_PIPE) {
    fixture->peer_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (fixture->peer_event == NULL) return -(int)GetLastError();
  }
#endif
  if (transport == ADAPTER_BENCH_TCP) {
    status = adapter_bench_set_socket_timeout(fixture->sockets[1]);
    if (status != SALTS_OK) return status;
  }

  if (mode == ADAPTER_BENCH_REACTIVE) {
    adapter_bench_reactive_setup setup = {fixture, backend_config, true, SALTS_EBUSY};

    status =
        salts_threadpool_submit(fixture->publisher_pool, adapter_bench_reactive_setup_task, &setup);
    if (status == SALTS_OK) status = salts_threadpool_wait_status(fixture->publisher_pool);
    if (status == SALTS_OK) status = setup.status;
    fixture->reactive_setup_status = status;
  } else {
    status = adapter_bench_backend_attach(fixture);
  }
  if (status != SALTS_OK) return status;

  if (mode == ADAPTER_BENCH_ACTOR) {
    cflow_io_actor_config actor_config = {0};
    fixture->adapter_ops = cflow_io_native_adapter_actor_ops();
    if (!cflow_executor_manual_init_with_capacity(&fixture->executor, 2u)) return SALTS_ENOMEM;
    fixture->executor_initialized = true;
    actor_config.request_capacity = 2u;
    actor_config.command_capacity = 2u;
    actor_config.executor = &fixture->executor;
    actor_config.backend.submit = adapter_bench_timed_submit;
    actor_config.backend.cancel = adapter_bench_timed_cancel;
    actor_config.backend_user = fixture;
    actor_config.completion = adapter_bench_complete;
    actor_config.completion_user = fixture;
    status = cflow_io_actor_init(&fixture->actor, &actor_config);
    if (status != SALTS_OK) return status;
    fixture->actor_initialized = true;
  } else if (mode == ADAPTER_BENCH_REACTIVE) {
    cflow_io_publisher_config reactive_config = {0};
    fixture->adapter_ops = cflow_io_native_adapter_actor_ops();
    fixture->normalized.root = CMETA_INVALID_ID;
    cflow_graph_init(&fixture->surface, &cmeta_type_int);
    fixture->surface_initialized = true;
    if (!cflow_graph_normalize(&fixture->normalized, &fixture->surface)) return SALTS_ENOMEM;
    fixture->normalized_initialized = true;
    if (!cflow_scheduler_worker_init_with_capacity(&fixture->scheduler, 1u,
                                                   ADAPTER_BENCH_REACTIVE_SCHEDULER_CAPACITY, 1u))
      return SALTS_ENOMEM;
    fixture->scheduler_initialized = true;
    reactive_config.name = "native-io-adapter-benchmark-publisher";
    reactive_config.type = &cmeta_type_int;
    reactive_config.backend.submit = adapter_bench_timed_submit;
    reactive_config.backend.cancel = adapter_bench_timed_cancel;
    reactive_config.backend_user = fixture;
    reactive_config.prepare = adapter_bench_reactive_prepare;
    reactive_config.encode = adapter_bench_reactive_encode;
    reactive_config.user = fixture;
    reactive_config.drive = adapter_bench_reactive_drive;
    reactive_config.drive_user = fixture;
    status = cflow_publisher_from_io_actor_windowed(&fixture->publisher, &fixture->reactive_owner,
                                                    &reactive_config, 1u);
    if (status != SALTS_OK) return status;
    fixture->reactive_owner_initialized = true;
    fixture->subscriber_callbacks = (cflow_subscriber_callbacks){
        adapter_bench_reactive_subscriber_value, adapter_bench_reactive_subscriber_error,
        adapter_bench_reactive_subscriber_done, fixture};
    fixture->subscriber = cflow_subscriber_from_callbacks(&fixture->subscriber_callbacks);
    if (!cflow_subscribe(&fixture->subscription, &fixture->normalized, &fixture->publisher,
                         &fixture->scheduler, &fixture->subscriber))
      return SALTS_EIO;
    fixture->subscription_initialized = true;
    status = salts_threadpool_submit(fixture->publisher_pool, adapter_bench_reactive_drive_task,
                                     fixture);
    if (status != SALTS_OK) return status;
    salts_mutex_lock(&fixture->reactive_gate);
    fixture->reactive_loop_started = true;
    salts_mutex_unlock(&fixture->reactive_gate);
  }
  {
    const salts_threadpool_config_t peer_pool_config = {1, 1};
    fixture->peer_pool = salts_threadpool_create_with_config(&peer_pool_config);
    if (fixture->peer_pool == NULL) return SALTS_ENOMEM;
    fixture->peer_pool_initialized = true;
  }
  return SALTS_OK;
}

static void adapter_bench_reactive_cleanup_task(void *user) {
  adapter_bench_fixture *fixture = (adapter_bench_fixture *)user;
  int status = SALTS_OK;
  int current;

  adapter_bench_thread_role = ADAPTER_BENCH_THREAD_ROLE_PUBLISHER;
  if (fixture->reactive_owner_initialized) {
    const uint64_t started = salts_hrtime();

    while (!cflow_io_publisher_owner_is_quiescent(&fixture->reactive_owner)) {
      size_t observed = 0u;

      current = cflow_io_native_adapter_drive_publisher(&fixture->adapter, &fixture->reactive_owner,
                                                        ADAPTER_BENCH_DRAIN_POLL_TIMEOUT_MS, 64u,
                                                        &observed);
      fixture->reactive_observed += observed;
      if (current != SALTS_OK && current != SALTS_ETIMEDOUT) {
        adapter_bench_keep_first_status(&status, current);
        break;
      }
      if (salts_hrtime() - started >= ADAPTER_BENCH_TIMEOUT_NS) {
        adapter_bench_keep_first_status(&status, SALTS_ETIMEDOUT);
        break;
      }
    }
    current = cflow_io_publisher_owner_close(&fixture->reactive_owner);
    adapter_bench_keep_first_status(&status, current);
    if (current == SALTS_OK) fixture->reactive_owner_initialized = false;
  }
  if (fixture->backend_initialized && !fixture->reactive_owner_initialized) {
    current = cflow_io_native_adapter_close(&fixture->adapter);
    adapter_bench_keep_first_status(&status, current);
  }
  if (fixture->sockets_created && !fixture->reactive_owner_initialized) {
    adapter_bench_close_socket(fixture->sockets[0]);
    adapter_bench_close_socket(fixture->sockets[1]);
    fixture->sockets[0] = ADAPTER_BENCH_INVALID_SOCKET;
    fixture->sockets[1] = ADAPTER_BENCH_INVALID_SOCKET;
    fixture->sockets_created = false;
  }
  if (fixture->pipes_created && !fixture->reactive_owner_initialized) {
    adapter_bench_close_pipe(fixture->pipes[0]);
    adapter_bench_close_pipe(fixture->pipes[1]);
    fixture->pipes[0] = ADAPTER_BENCH_INVALID_PIPE;
    fixture->pipes[1] = ADAPTER_BENCH_INVALID_PIPE;
    fixture->pipes_created = false;
  }
  if (fixture->backend_initialized && !fixture->reactive_owner_initialized) {
    for (size_t index = 0u; index < 2u; ++index) {
      if (!native_io_endpoint_valid(fixture->endpoints[index])) continue;
      current =
          fixture->transport == ADAPTER_BENCH_TCP
              ? cflow_io_native_adapter_release_socket(&fixture->adapter, fixture->endpoints[index])
              : cflow_io_native_adapter_release_pipe(&fixture->adapter, fixture->endpoints[index]);
      adapter_bench_keep_first_status(&status, current);
      if (current == SALTS_OK) fixture->endpoints[index] = (native_io_endpoint){0};
    }
  }
  if (fixture->backend_initialized && !fixture->reactive_owner_initialized) {
    current = cflow_io_native_adapter_destroy(&fixture->adapter);
    adapter_bench_keep_first_status(&status, current);
    if (current == SALTS_OK) fixture->backend_initialized = false;
  }
  fixture->reactive_cleanup_status = status;
}

static int adapter_bench_direct_drain(adapter_bench_fixture *fixture) {
  const uint64_t started = salts_hrtime();
  int status = native_io_backend_close(&fixture->direct);

  for (size_t role = 0u; role < 2u; ++role) {
    int current;

    if (!fixture->direct_pending[role]) continue;
    current = native_io_backend_cancel(&fixture->direct, fixture->direct_requests[role]);
    if (current != SALTS_OK && current != SALTS_EALREADY)
      adapter_bench_keep_first_status(&status, current);
  }
  while (fixture->direct_pending[0] || fixture->direct_pending[1]) {
    native_io_completion completions[ADAPTER_BENCH_MAX_COMPLETIONS];
    size_t completion_count = 0u;
    int current =
        native_io_backend_observe(&fixture->direct, completions, ADAPTER_BENCH_MAX_COMPLETIONS,
                                  ADAPTER_BENCH_DRAIN_POLL_TIMEOUT_MS, &completion_count);

    if (current != SALTS_OK && current != SALTS_ETIMEDOUT)
      adapter_bench_keep_first_status(&status, current);
    for (size_t index = 0u; index < completion_count; ++index) {
      for (size_t role = 0u; role < 2u; ++role) {
        if (fixture->direct_pending[role] &&
            completions[index].request.slot == fixture->direct_requests[role].slot &&
            completions[index].request.generation == fixture->direct_requests[role].generation) {
          fixture->direct_pending[role] = false;
          fixture->direct_requests[role] = (native_io_request){0};
        }
      }
    }
    if (salts_hrtime() - started >= ADAPTER_BENCH_TIMEOUT_NS) {
      adapter_bench_keep_first_status(&status, SALTS_ETIMEDOUT);
      break;
    }
  }
  return status;
}

static int adapter_bench_actor_drain(adapter_bench_fixture *fixture) {
  const uint64_t started = salts_hrtime();
  int status = cflow_io_actor_close(&fixture->actor);

  while (!cflow_io_actor_is_quiescent(&fixture->actor)) {
    cflow_io_native_adapter_stats adapter_stats = {0};
    cflow_io_run_result run_result = cflow_io_actor_run_ready(&fixture->actor, 64u);
    int current;

    if (run_result.status == CFLOW_IO_RUN_BUSY ||
        run_result.status == CFLOW_IO_RUN_INVALID_ARGUMENT)
      adapter_bench_keep_first_status(&status, SALTS_EPROTO);
    if (cflow_io_native_adapter_get_stats(&fixture->adapter, &adapter_stats) &&
        adapter_stats.active_bridges != 0u) {
      size_t observed = 0u;

      current = cflow_io_native_adapter_observe(&fixture->adapter,
                                                ADAPTER_BENCH_DRAIN_POLL_TIMEOUT_MS, &observed);
      if (current != SALTS_OK && current != SALTS_ETIMEDOUT)
        adapter_bench_keep_first_status(&status, current);
    }
    run_result = cflow_io_actor_run_ready(&fixture->actor, 64u);
    if (run_result.status == CFLOW_IO_RUN_BUSY ||
        run_result.status == CFLOW_IO_RUN_INVALID_ARGUMENT)
      adapter_bench_keep_first_status(&status, SALTS_EPROTO);
    (void)cflow_executor_run_ready(&fixture->executor);
    for (size_t role = 0u; role < 2u; ++role) {
      cflow_io_ack_status acknowledged;

      if (!fixture->actor_pending[role]) continue;
      acknowledged = cflow_io_actor_acknowledge(&fixture->actor, fixture->actor_request_ids[role]);
      if (acknowledged == CFLOW_IO_ACK_RELEASED) {
        fixture->actor_pending[role] = false;
        fixture->actor_request_ids[role] = 0u;
      }
    }
    fixture->delivery_count = 0u;
    if (salts_hrtime() - started >= ADAPTER_BENCH_TIMEOUT_NS) {
      adapter_bench_keep_first_status(&status, SALTS_ETIMEDOUT);
      break;
    }
  }
  if (!cflow_io_actor_is_quiescent(&fixture->actor))
    adapter_bench_keep_first_status(&status, SALTS_EBUSY);
  return status;
}

static int adapter_bench_fixture_destroy(adapter_bench_fixture *fixture) {
  int status = SALTS_OK;

  if (fixture == NULL) return SALTS_EINVAL;
  if (fixture->peer_pool_initialized) {
    adapter_bench_keep_first_status(&status, salts_threadpool_wait_status(fixture->peer_pool));
    salts_threadpool_shutdown(fixture->peer_pool);
    salts_threadpool_destroy(fixture->peer_pool);
    fixture->peer_pool = NULL;
    fixture->peer_pool_initialized = false;
  }
#if defined(_WIN32)
  if (fixture->peer_event != NULL) {
    (void)CloseHandle(fixture->peer_event);
    fixture->peer_event = NULL;
  }
#endif
  if (fixture->subscription_initialized) {
    cflow_subscription_close(&fixture->subscription);
    fixture->subscription_initialized = false;
  } else if (cflow_publisher_valid(&fixture->publisher)) {
    cflow_publisher_destroy(&fixture->publisher);
  }
  if (fixture->mode == ADAPTER_BENCH_REACTIVE && fixture->publisher_pool_initialized) {
    bool owner_task_quiescent;
    int current;

    if (fixture->scheduler_initialized && !cflow_scheduler_wait_idle(&fixture->scheduler) &&
        status == SALTS_OK)
      status = SALTS_EBUSY;
    current = adapter_bench_reactive_stop_owner(fixture);
    if (current != SALTS_OK && status == SALTS_OK) status = current;
    salts_mutex_lock(&fixture->reactive_gate);
    owner_task_quiescent = !fixture->reactive_loop_started || fixture->reactive_loop_stopped;
    salts_mutex_unlock(&fixture->reactive_gate);
    if (!owner_task_quiescent) {
      if (status == SALTS_OK) status = SALTS_EPROTO;
    } else {
      fixture->reactive_cleanup_status = SALTS_EBUSY;
      current = salts_threadpool_submit(fixture->publisher_pool,
                                        adapter_bench_reactive_cleanup_task, fixture);
      if (current != SALTS_OK && status == SALTS_OK) status = current;
      if (current == SALTS_OK) {
        current = salts_threadpool_wait_status(fixture->publisher_pool);
        if (current != SALTS_OK && status == SALTS_OK) status = current;
        if (fixture->reactive_cleanup_status != SALTS_OK && status == SALTS_OK)
          status = fixture->reactive_cleanup_status;
      }
    }
    salts_threadpool_shutdown(fixture->publisher_pool);
    salts_threadpool_destroy(fixture->publisher_pool);
    fixture->publisher_pool = NULL;
    fixture->publisher_pool_initialized = false;
  } else if (fixture->reactive_owner_initialized) {
    int current = cflow_io_publisher_owner_close(&fixture->reactive_owner);
    if (current != SALTS_OK && status == SALTS_OK) status = current;
    if (current == SALTS_OK) fixture->reactive_owner_initialized = false;
  }
  if (fixture->scheduler_initialized) {
    cflow_scheduler_destroy(&fixture->scheduler);
    fixture->scheduler_initialized = false;
  }
  if (fixture->reactive_cond_initialized) {
    salts_cond_destroy(&fixture->reactive_changed);
    fixture->reactive_cond_initialized = false;
  }
  if (fixture->reactive_mutex_initialized) {
    salts_mutex_destroy(&fixture->reactive_gate);
    fixture->reactive_mutex_initialized = false;
  }
  if (fixture->normalized_initialized) {
    cflow_graph_destroy(&fixture->normalized);
    fixture->normalized_initialized = false;
  }
  if (fixture->surface_initialized) {
    cflow_graph_destroy(&fixture->surface);
    fixture->surface_initialized = false;
  }
  if (fixture->actor_initialized) {
    int current = adapter_bench_actor_drain(fixture);
    adapter_bench_keep_first_status(&status, current);
    current = cflow_io_actor_destroy(&fixture->actor);
    if (current != SALTS_OK && status == SALTS_OK) status = current;
    if (current == SALTS_OK) fixture->actor_initialized = false;
  }
  if (fixture->backend_initialized && fixture->mode != ADAPTER_BENCH_REACTIVE &&
      !fixture->actor_initialized) {
    int current = fixture->mode == ADAPTER_BENCH_DIRECT
                      ? adapter_bench_direct_drain(fixture)
                      : cflow_io_native_adapter_close(&fixture->adapter);
    if (current != SALTS_OK && status == SALTS_OK) status = current;
  }
  if (fixture->sockets_created && fixture->mode != ADAPTER_BENCH_REACTIVE &&
      !fixture->actor_initialized && !fixture->direct_pending[0] && !fixture->direct_pending[1]) {
    adapter_bench_close_socket(fixture->sockets[0]);
    adapter_bench_close_socket(fixture->sockets[1]);
    fixture->sockets[0] = ADAPTER_BENCH_INVALID_SOCKET;
    fixture->sockets[1] = ADAPTER_BENCH_INVALID_SOCKET;
    fixture->sockets_created = false;
  }
  if (fixture->pipes_created && fixture->mode != ADAPTER_BENCH_REACTIVE &&
      !fixture->actor_initialized && !fixture->direct_pending[0] && !fixture->direct_pending[1]) {
    adapter_bench_close_pipe(fixture->pipes[0]);
    adapter_bench_close_pipe(fixture->pipes[1]);
    fixture->pipes[0] = ADAPTER_BENCH_INVALID_PIPE;
    fixture->pipes[1] = ADAPTER_BENCH_INVALID_PIPE;
    fixture->pipes_created = false;
  }
  if (fixture->backend_initialized && fixture->mode != ADAPTER_BENCH_REACTIVE &&
      !fixture->actor_initialized && !fixture->direct_pending[0] && !fixture->direct_pending[1]) {
    for (size_t index = 0u; index < 2u; ++index) {
      int current;
      if (!native_io_endpoint_valid(fixture->endpoints[index])) continue;
      if (fixture->transport == ADAPTER_BENCH_TCP) {
        current =
            fixture->mode == ADAPTER_BENCH_DIRECT
                ? native_io_backend_release_socket(&fixture->direct, fixture->endpoints[index])
                : cflow_io_native_adapter_release_socket(&fixture->adapter,
                                                         fixture->endpoints[index]);
      } else {
        current = fixture->mode == ADAPTER_BENCH_DIRECT
                      ? native_io_backend_release_pipe(&fixture->direct, fixture->endpoints[index])
                      : cflow_io_native_adapter_release_pipe(&fixture->adapter,
                                                             fixture->endpoints[index]);
      }
      if (current != SALTS_OK && status == SALTS_OK) status = current;
      if (current == SALTS_OK) fixture->endpoints[index] = (native_io_endpoint){0};
    }
    {
      int current = fixture->mode == ADAPTER_BENCH_DIRECT
                        ? native_io_backend_destroy(&fixture->direct)
                        : cflow_io_native_adapter_destroy(&fixture->adapter);
      if (current != SALTS_OK && status == SALTS_OK) status = current;
      if (current == SALTS_OK) fixture->backend_initialized = false;
    }
  }
  if (fixture->executor_initialized && !fixture->actor_initialized) {
    if (!cflow_executor_shutdown(&fixture->executor) && status == SALTS_OK) status = SALTS_EBUSY;
    cflow_executor_destroy(&fixture->executor);
    fixture->executor_initialized = false;
  }
  if (!fixture->backend_initialized && !fixture->actor_initialized &&
      !fixture->reactive_owner_initialized) {
    free(fixture->received);
    free(fixture->sent);
    fixture->received = NULL;
    fixture->sent = NULL;
  }
  return status;
}

static int adapter_bench_direct_exchange(adapter_bench_fixture *fixture) {
  size_t offsets[2] = {0u, 0u};
  const size_t role = (size_t)fixture->direction;

  while (offsets[role] < fixture->payload_size) {
    native_io_completion completions[1];
    size_t completion_count = 0u;
    uint64_t started = 0u;
    int status;

    {
      native_io_operation operation;
      if (fixture->direct_pending[role]) return SALTS_EPROTO;
      operation = adapter_bench_operation(fixture, role, offsets[role]);
      if (fixture->stages != NULL) started = salts_hrtime();
      status =
          native_io_backend_submit(&fixture->direct, &operation, &fixture->direct_requests[role]);
      if (fixture->stages != NULL) {
        adapter_bench_counter_add(&fixture->stages->native_submit_ns, salts_hrtime() - started);
        ++fixture->stages->native_submit_calls;
      }
      if (status != SALTS_OK) return status;
      fixture->direct_pending[role] = true;
    }

    if (fixture->stages != NULL) started = salts_hrtime();
    status = native_io_backend_observe(&fixture->direct, completions, 1u, ADAPTER_BENCH_TIMEOUT_MS,
                                       &completion_count);
    if (fixture->stages != NULL) {
      adapter_bench_counter_add(&fixture->stages->observe_ns, salts_hrtime() - started);
      ++fixture->stages->observe_calls;
    }
    if (status != SALTS_OK) return status;
    for (size_t index = 0u; index < completion_count; ++index) {
      const native_io_completion *completion = &completions[index];
      if (!fixture->direct_pending[role] ||
          completion->request.slot != fixture->direct_requests[role].slot ||
          completion->request.generation != fixture->direct_requests[role].generation)
        return SALTS_EPROTO;
      fixture->direct_pending[role] = false;
      fixture->direct_requests[role] = (native_io_request){0};
      if (completion->kind != NATIVE_IO_COMPLETION_OK || completion->bytes == 0u)
        return completion->status != SALTS_OK ? completion->status : SALTS_EPROTO;
      if (completion->user_data != (uintptr_t)(role + 1u) ||
          completion->bytes > fixture->payload_size - offsets[role])
        return SALTS_EPROTO;
      offsets[role] += completion->bytes;
    }
  }
  return SALTS_OK;
}

static int adapter_bench_actor_run_transition(adapter_bench_fixture *fixture, size_t max_steps) {
  cflow_io_run_result result;
  uint64_t nested_before;
  uint64_t started = 0u;
  uint64_t elapsed;
  uint64_t nested;

  if (fixture->stages == NULL) {
    result = cflow_io_actor_run_ready(&fixture->actor, max_steps);
    return result.status == CFLOW_IO_RUN_INVALID_ARGUMENT || result.status == CFLOW_IO_RUN_BUSY
               ? SALTS_EPROTO
               : SALTS_OK;
  }
  nested_before = fixture->stages->native_submit_ns;
  started = salts_hrtime();
  result = cflow_io_actor_run_ready(&fixture->actor, max_steps);
  elapsed = salts_hrtime() - started;
  nested = fixture->stages->native_submit_ns - nested_before;

  adapter_bench_counter_add(&fixture->stages->actor_transition_ns,
                            elapsed > nested ? elapsed - nested : 0u);
  return result.status == CFLOW_IO_RUN_INVALID_ARGUMENT || result.status == CFLOW_IO_RUN_BUSY
             ? SALTS_EPROTO
             : SALTS_OK;
}

static int adapter_bench_actor_exchange(adapter_bench_fixture *fixture) {
  size_t offsets[2] = {0u, 0u};
  const size_t role = (size_t)fixture->direction;
  const size_t release_before = fixture->release_count;
  const uint64_t operation_before = fixture->operation_count;

  while (offsets[role] < fixture->payload_size) {
    {
      cflow_io_operation actor_operation;
      cflow_io_submit_result submitted;
      uint64_t started = 0u;

      if (fixture->actor_pending[role]) return SALTS_EPROTO;
      fixture->actor_operations[role].native =
          adapter_bench_operation(fixture, role, offsets[role]);
      fixture->actor_operations[role].role = fixture->direction;
      fixture->actor_operations[role].release_count = &fixture->release_count;
      actor_operation =
          (cflow_io_operation){&fixture->actor_operations[role], adapter_bench_release};
      if (fixture->stages != NULL) started = salts_hrtime();
      submitted =
          cflow_io_actor_try_submit(&fixture->actor, fixture->next_lease++, &actor_operation);
      if (fixture->stages != NULL)
        adapter_bench_counter_add(&fixture->stages->admission_ns, salts_hrtime() - started);
      if (submitted.status != CFLOW_IO_SUBMIT_ACCEPTED) {
        cflow_io_actor_stats stats = {0};
        (void)cflow_io_actor_get_stats(&fixture->actor, &stats);
        fprintf(stderr,
                "Actor admission failed: status=%d active=%zu "
                "queued=%zu ready=%zu pending=%zu delivered=%zu\n",
                (int)submitted.status, stats.active_requests, stats.queued_commands, stats.ready,
                stats.backend_pending, stats.delivered_unacknowledged);
        switch (submitted.status) {
        case CFLOW_IO_SUBMIT_INVALID_ARGUMENT:
          return SALTS_EINVAL;
        case CFLOW_IO_SUBMIT_FULL:
          return SALTS_ENOBUFS;
        case CFLOW_IO_SUBMIT_CLOSED:
          return SALTS_ESHUTDOWN;
        case CFLOW_IO_SUBMIT_LEASE_IN_USE:
          return SALTS_EALREADY;
        case CFLOW_IO_SUBMIT_ID_EXHAUSTED:
          return SALTS_ERANGE;
        case CFLOW_IO_SUBMIT_ACCEPTED:
          break;
        }
        return SALTS_EPROTO;
      }
      fixture->actor_pending[role] = true;
      fixture->actor_request_ids[role] = submitted.request_id;
      ++fixture->operation_count;
      if (fixture->stages != NULL) ++fixture->stages->actor_operations;
    }
    {
      int status = adapter_bench_actor_run_transition(fixture, 8u);
      if (status != SALTS_OK) return status;
    }
    while (fixture->delivery_count == 0u) {
      size_t observed = 0u;
      uint64_t started = 0u;
      if (fixture->stages != NULL) started = salts_hrtime();
      int status =
          cflow_io_native_adapter_observe(&fixture->adapter, ADAPTER_BENCH_TIMEOUT_MS, &observed);
      if (fixture->stages != NULL) {
        adapter_bench_counter_add(&fixture->stages->observe_ns, salts_hrtime() - started);
        ++fixture->stages->observe_calls;
      }
      if (status != SALTS_OK) return status;
      status = adapter_bench_actor_run_transition(fixture, 8u);
      if (status != SALTS_OK) return status;
      if (fixture->stages != NULL) started = salts_hrtime();
      (void)cflow_executor_run_ready(&fixture->executor);
      if (fixture->stages != NULL)
        adapter_bench_counter_add(&fixture->stages->executor_delivery_ns, salts_hrtime() - started);
    }
    for (size_t index = 0u; index < fixture->delivery_count; ++index) {
      adapter_bench_delivery *delivery = &fixture->deliveries[index];
      const size_t completion_role = (size_t)delivery->operation->role;
      uint64_t started = 0u;

      if (completion_role >= 2u || !fixture->actor_pending[completion_role] ||
          delivery->request_id != fixture->actor_request_ids[completion_role] ||
          delivery->completion.kind != CFLOW_IO_COMPLETION_OK || delivery->completion.bytes == 0u ||
          delivery->completion.bytes > fixture->payload_size - offsets[completion_role])
        return delivery->completion.error != SALTS_OK ? delivery->completion.error : SALTS_EPROTO;
      offsets[completion_role] += delivery->completion.bytes;
      if (fixture->stages != NULL) started = salts_hrtime();
      if (cflow_io_actor_acknowledge(&fixture->actor, delivery->request_id) !=
          CFLOW_IO_ACK_RELEASED)
        return SALTS_EPROTO;
      if (fixture->stages != NULL)
        adapter_bench_counter_add(&fixture->stages->acknowledge_ns, salts_hrtime() - started);
      fixture->actor_pending[completion_role] = false;
      fixture->actor_request_ids[completion_role] = 0u;
    }
    fixture->delivery_count = 0u;
  }
  if (fixture->release_count - release_before != fixture->operation_count - operation_before) {
    return SALTS_EPROTO;
  }
  return SALTS_OK;
}

static int adapter_bench_reactive_wait_values(adapter_bench_fixture *fixture,
                                              size_t target_values) {
  size_t waits = 0u;
  int status = SALTS_OK;

  salts_mutex_lock(&fixture->reactive_gate);
  while (fixture->reactive_subscriber_values < target_values && fixture->reactive_error == NULL &&
         fixture->reactive_wake_status == SALTS_OK && fixture->reactive_drive_status == SALTS_OK &&
         waits < ADAPTER_BENCH_REACTIVE_WAIT_LIMIT) {
    (void)salts_cond_timedwait(&fixture->reactive_changed, &fixture->reactive_gate,
                               ADAPTER_BENCH_REACTIVE_WAIT_SLICE_NS);
    ++waits;
  }
  if (fixture->reactive_wake_status != SALTS_OK) status = fixture->reactive_wake_status;
  else if (fixture->reactive_drive_status != SALTS_OK) status = fixture->reactive_drive_status;
  else if (fixture->reactive_error != NULL) status = SALTS_EIO;
  else if (fixture->reactive_subscriber_values < target_values) status = SALTS_ETIMEDOUT;
  salts_mutex_unlock(&fixture->reactive_gate);
  return status;
}

static int adapter_bench_reactive_exchange(adapter_bench_fixture *fixture) {
  const size_t role = (size_t)fixture->direction;
  size_t release_before;
  size_t subscriber_before;
  uint64_t operation_before;

  salts_mutex_lock(&fixture->reactive_gate);
  release_before = fixture->release_count;
  subscriber_before = fixture->reactive_subscriber_values;
  operation_before = fixture->operation_count;
  memset(fixture->reactive_offsets, 0, sizeof(fixture->reactive_offsets));
  memset(fixture->reactive_pending, 0, sizeof(fixture->reactive_pending));
  salts_mutex_unlock(&fixture->reactive_gate);
  for (;;) {
    adapter_bench_stages *stages;
    size_t target_values;
    uint64_t started = 0u;
    int status;

    salts_mutex_lock(&fixture->reactive_gate);
    target_values = fixture->reactive_subscriber_values + 1u;
    if (fixture->reactive_offsets[role] >= fixture->payload_size) {
      salts_mutex_unlock(&fixture->reactive_gate);
      break;
    }
    salts_mutex_unlock(&fixture->reactive_gate);
    stages = adapter_bench_stage_acquire(fixture);
    if (stages != NULL) started = salts_hrtime();
    if (!cflow_subscription_request(&fixture->subscription, 1u)) {
      adapter_bench_stage_release(fixture, stages);
      return SALTS_EPROTO;
    }
    status = adapter_bench_reactive_wait_values(fixture, target_values);
    if (status == SALTS_OK && !cflow_scheduler_wait_idle(&fixture->scheduler)) status = SALTS_EBUSY;
    if (stages != NULL)
      adapter_bench_counter_add(&stages->reactive_subscription_ns, salts_hrtime() - started);
    adapter_bench_stage_release(fixture, stages);
    if (status != SALTS_OK) {
      return status;
    }
  }
  salts_mutex_lock(&fixture->reactive_gate);
  {
    size_t waits = 0u;
    const uint64_t operations = fixture->operation_count - operation_before;
    while (fixture->release_count - release_before < operations &&
           fixture->reactive_wake_status == SALTS_OK &&
           fixture->reactive_drive_status == SALTS_OK &&
           waits < ADAPTER_BENCH_REACTIVE_WAIT_LIMIT) {
      (void)salts_cond_timedwait(&fixture->reactive_changed, &fixture->reactive_gate,
                                 ADAPTER_BENCH_REACTIVE_WAIT_SLICE_NS);
      ++waits;
    }
  }
  if (fixture->reactive_offsets[role] != fixture->payload_size ||
      fixture->release_count - release_before != fixture->operation_count - operation_before ||
      fixture->reactive_subscriber_values - subscriber_before !=
          fixture->operation_count - operation_before) {
    salts_mutex_unlock(&fixture->reactive_gate);
    return SALTS_EPROTO;
  }
  salts_mutex_unlock(&fixture->reactive_gate);
  return SALTS_OK;
}

static int adapter_bench_exchange(adapter_bench_fixture *fixture, uint64_t *out_latency_ns) {
  uint64_t started;
  int status;
  int peer_status;

  memset(fixture->received, 0, fixture->payload_size);
  fixture->peer_status = SALTS_EBUSY;
  status = salts_threadpool_submit(fixture->peer_pool, adapter_bench_peer_task, fixture);
  if (status != SALTS_OK) return status;
  started = salts_hrtime();
  status = fixture->mode == ADAPTER_BENCH_DIRECT  ? adapter_bench_direct_exchange(fixture)
           : fixture->mode == ADAPTER_BENCH_ACTOR ? adapter_bench_actor_exchange(fixture)
                                                  : adapter_bench_reactive_exchange(fixture);
  if (out_latency_ns != NULL) *out_latency_ns = salts_hrtime() - started;
  peer_status = salts_threadpool_wait_status(fixture->peer_pool);
  if (status == SALTS_OK) status = peer_status;
  if (status == SALTS_OK) status = fixture->peer_status;
  if (status == SALTS_OK && memcmp(fixture->sent, fixture->received, fixture->payload_size) != 0)
    status = SALTS_EPROTO;
  return status;
}

static void adapter_bench_reactive_validate_task(void *user) {
  adapter_bench_fixture *fixture = (adapter_bench_fixture *)user;
  cflow_io_native_adapter_stats adapter_stats = {0};
  cflow_io_publisher_stats reactive_stats = {0};
  cflow_io_publisher_window_stats window_stats = {0};
  bool stats_valid;

  adapter_bench_thread_role = ADAPTER_BENCH_THREAD_ROLE_PUBLISHER;
  stats_valid = cflow_io_native_adapter_get_stats(&fixture->adapter, &adapter_stats) &&
                cflow_io_publisher_owner_get_stats(&fixture->reactive_owner, &reactive_stats) &&
                cflow_io_publisher_owner_get_window_stats(&fixture->reactive_owner, &window_stats);
  if (stats_valid) {
    fixture->validation_errors = adapter_stats.native.failed;
    adapter_bench_counter_add(&fixture->validation_errors,
                              adapter_stats.native.native_submit_errors);
    adapter_bench_counter_add(&fixture->validation_errors,
                              adapter_stats.native.native_cancel_errors);
    adapter_bench_counter_add(&fixture->validation_errors,
                              reactive_stats.actor.backend_submit_errors);
    adapter_bench_counter_add(&fixture->validation_errors,
                              reactive_stats.actor.backend_cancel_errors);
    fixture->validation_rejections = adapter_stats.native.rejected_full;
    adapter_bench_counter_add(&fixture->validation_rejections,
                              reactive_stats.actor.rejected_request_full);
    adapter_bench_counter_add(&fixture->validation_rejections,
                              reactive_stats.actor.rejected_command_full);
    adapter_bench_counter_add(&fixture->validation_rejections,
                              reactive_stats.actor.rejected_closed);
    adapter_bench_counter_add(&fixture->validation_rejections,
                              reactive_stats.actor.rejected_lease_in_use);
    adapter_bench_counter_add(&fixture->validation_rejections,
                              reactive_stats.actor.executor_rejected_full);
    adapter_bench_counter_add(&fixture->validation_rejections,
                              reactive_stats.actor.executor_rejected_closed);
    adapter_bench_counter_add(&fixture->validation_rejections,
                              reactive_stats.actor.executor_rejected_invalid);
    fixture->validation_stale_completions = adapter_stats.stale_actor_completions;
    adapter_bench_counter_add(&fixture->validation_stale_completions,
                              reactive_stats.actor.stale_completions);
  }
  fixture->reactive_validation_result =
      stats_valid && adapter_stats.active_bridges == 0u &&
      adapter_stats.stale_actor_completions == 0u && adapter_stats.native.active_requests == 0u &&
      adapter_stats.native.submitted == adapter_stats.native.completed &&
      adapter_stats.native.cancelled == 0u && adapter_stats.native.failed == 0u &&
      adapter_stats.native.rejected_full == 0u && adapter_stats.native.native_submit_errors == 0u &&
      adapter_stats.native.native_cancel_errors == 0u &&
      reactive_stats.actor.active_requests == 0u &&
      reactive_stats.actor.rejected_request_full == 0u &&
      reactive_stats.actor.rejected_command_full == 0u &&
      reactive_stats.actor.stale_completions == 0u &&
      reactive_stats.actor.backend_submit_errors == 0u &&
      reactive_stats.actor.executor_rejected_full == 0u &&
      reactive_stats.actor.acknowledged == reactive_stats.actor.accepted &&
      fixture->reactive_subscriber_values == reactive_stats.actor.accepted &&
      fixture->reactive_observed == reactive_stats.actor.accepted &&
      fixture->release_count == reactive_stats.actor.accepted && window_stats.occupied == 0u &&
      window_stats.demand_reserved == 0u && window_stats.results_ready == 0u &&
      fixture->reactive_wake_status == SALTS_OK && fixture->reactive_drive_status == SALTS_OK &&
      fixture->reactive_publisher_callbacks != 0u && fixture->reactive_subscriber_callbacks != 0u &&
      fixture->reactive_prepare_callbacks != 0u && fixture->reactive_encode_callbacks != 0u &&
      fixture->reactive_role_collisions == 0u;
}

static bool adapter_bench_validate(adapter_bench_fixture *fixture, adapter_bench_result *result) {
  native_io_backend_stats direct_stats = {0};
  cflow_io_native_adapter_stats adapter_stats = {0};
  cflow_io_actor_stats actor_stats = {0};

  if (result->latency_count != ADAPTER_BENCH_TOTAL_TRANSFERS ||
      memcmp(fixture->sent, fixture->received, fixture->payload_size) != 0)
    return false;
  if (fixture->mode == ADAPTER_BENCH_DIRECT) {
    if (!native_io_backend_get_stats(&fixture->direct, &direct_stats)) return false;
    result->errors = direct_stats.failed;
    adapter_bench_counter_add(&result->errors, direct_stats.native_submit_errors);
    adapter_bench_counter_add(&result->errors, direct_stats.native_cancel_errors);
    result->rejections = direct_stats.rejected_full;
    result->stale_completions = 0u;
    return !fixture->direct_pending[0] && !fixture->direct_pending[1] &&
           direct_stats.active_requests == 0u && direct_stats.submitted == direct_stats.completed &&
           direct_stats.cancelled == 0u && direct_stats.rejected_full == 0u &&
           direct_stats.failed == 0u && direct_stats.native_submit_errors == 0u &&
           direct_stats.native_cancel_errors == 0u;
  }
  if (fixture->mode == ADAPTER_BENCH_REACTIVE) {
    if (adapter_bench_reactive_stop_owner(fixture) != SALTS_OK) return false;
    if (salts_threadpool_submit(fixture->publisher_pool, adapter_bench_reactive_validate_task,
                                fixture) != SALTS_OK ||
        salts_threadpool_wait_status(fixture->publisher_pool) != SALTS_OK)
      return false;
    result->errors = fixture->validation_errors;
    result->rejections = fixture->validation_rejections;
    result->stale_completions = fixture->validation_stale_completions;
    return fixture->reactive_validation_result && result->errors == 0u &&
           result->rejections == 0u && result->stale_completions == 0u &&
           !atomic_load(&fixture->reactive_stage_enabled) &&
           atomic_load(&fixture->reactive_stage_writers) == 0u;
  }
  if (!cflow_io_native_adapter_get_stats(&fixture->adapter, &adapter_stats) ||
      adapter_stats.active_bridges != 0u || adapter_stats.stale_actor_completions != 0u ||
      adapter_stats.native.active_requests != 0u ||
      adapter_stats.native.submitted != adapter_stats.native.completed ||
      adapter_stats.native.cancelled != 0u || adapter_stats.native.failed != 0u ||
      adapter_stats.native.rejected_full != 0u || adapter_stats.native.native_submit_errors != 0u ||
      adapter_stats.native.native_cancel_errors != 0u)
    return false;
  if (!cflow_io_actor_get_stats(&fixture->actor, &actor_stats)) return false;
  result->errors = adapter_stats.native.failed;
  adapter_bench_counter_add(&result->errors, adapter_stats.native.native_submit_errors);
  adapter_bench_counter_add(&result->errors, adapter_stats.native.native_cancel_errors);
  adapter_bench_counter_add(&result->errors, actor_stats.backend_submit_errors);
  adapter_bench_counter_add(&result->errors, actor_stats.backend_cancel_errors);
  result->rejections = adapter_stats.native.rejected_full;
  adapter_bench_counter_add(&result->rejections, actor_stats.rejected_request_full);
  adapter_bench_counter_add(&result->rejections, actor_stats.rejected_command_full);
  adapter_bench_counter_add(&result->rejections, actor_stats.rejected_closed);
  adapter_bench_counter_add(&result->rejections, actor_stats.rejected_lease_in_use);
  adapter_bench_counter_add(&result->rejections, actor_stats.executor_rejected_full);
  adapter_bench_counter_add(&result->rejections, actor_stats.executor_rejected_closed);
  adapter_bench_counter_add(&result->rejections, actor_stats.executor_rejected_invalid);
  result->stale_completions = adapter_stats.stale_actor_completions;
  adapter_bench_counter_add(&result->stale_completions, actor_stats.stale_completions);
  return result->errors == 0u && result->rejections == 0u && result->stale_completions == 0u &&
         !fixture->actor_pending[0] && !fixture->actor_pending[1] &&
         actor_stats.active_requests == 0u && actor_stats.rejected_request_full == 0u &&
         actor_stats.rejected_command_full == 0u && actor_stats.stale_completions == 0u &&
         actor_stats.backend_submit_errors == 0u && actor_stats.executor_rejected_full == 0u &&
         actor_stats.acknowledged == actor_stats.accepted &&
         fixture->release_count == actor_stats.accepted;
}

static double adapter_bench_throughput(const adapter_bench_result *result) {
  const double bytes = (double)result->payload_size * (double)ADAPTER_BENCH_TOTAL_TRANSFERS;
  return result->wall_ns == 0u ? 0.0
                               : bytes * 1000000000.0 / (double)result->wall_ns / (1024.0 * 1024.0);
}

static double adapter_bench_rate(const adapter_bench_result *result) {
  return result->wall_ns == 0u
             ? 0.0
             : (double)ADAPTER_BENCH_TOTAL_TRANSFERS * 1000000000.0 / (double)result->wall_ns;
}

static double adapter_bench_cpu_throughput(const adapter_bench_result *result) {
  const double bytes = (double)result->payload_size * (double)result->cpu_transfers;
  return result->cpu_ns == 0u ? 0.0
                              : bytes * 1000000000.0 / (double)result->cpu_ns / (1024.0 * 1024.0);
}

static double adapter_bench_delta(double value, double baseline) {
  return baseline == 0.0 ? 0.0 : (value / baseline - 1.0) * 100.0;
}

static double adapter_bench_mean(uint64_t total, uint64_t count) {
  return count == 0u ? 0.0 : (double)total / (double)count;
}

static void adapter_bench_finalize(adapter_bench_result *result) {
  qsort(result->latencies, result->latency_count, sizeof(result->latencies[0]),
        adapter_bench_compare_u64);
  result->p50_ns = result->latencies[(result->latency_count - 1u) * 50u / 100u];
  result->p95_ns = result->latencies[(result->latency_count - 1u) * 95u / 100u];
  result->p99_ns = result->latencies[(result->latency_count - 1u) * 99u / 100u];
}

static int adapter_bench_run_sample(adapter_bench_fixture *fixture, adapter_bench_result *result) {
  const uint64_t wall_started = salts_hrtime();
  int status = SALTS_OK;

  for (size_t transfer = 0u; transfer < ADAPTER_BENCH_TRANSFERS_PER_SAMPLE; ++transfer) {
    uint64_t latency_ns = 0u;
    status = adapter_bench_exchange(fixture, &latency_ns);
    if (status != SALTS_OK) break;
    result->latencies[result->latency_count++] = latency_ns;
  }
  adapter_bench_counter_add(&result->wall_ns, salts_hrtime() - wall_started);
  return status;
}

static int adapter_bench_measure_cpu(adapter_bench_fixture *fixture, adapter_bench_result *result) {
  uint64_t started = 0u;
  uint64_t finished = 0u;
  int status = adapter_bench_process_cpu_ns(&started);

  result->cpu_transfers = 0u;
  for (size_t pass = 0u; pass < ADAPTER_BENCH_CPU_MAX_PASSES && status == SALTS_OK; ++pass) {
    for (size_t transfer = 0u; transfer < ADAPTER_BENCH_TOTAL_TRANSFERS; ++transfer) {
      status = adapter_bench_exchange(fixture, NULL);
      if (status != SALTS_OK) break;
    }
    if (status != SALTS_OK) break;
    result->cpu_transfers += ADAPTER_BENCH_TOTAL_TRANSFERS;
    status = adapter_bench_process_cpu_ns(&finished);
    if (status == SALTS_OK && finished - started >= ADAPTER_BENCH_CPU_TARGET_NS) break;
  }
  if (status != SALTS_OK) return status;
  if (finished <= started) return SALTS_EIO;
  result->cpu_ns = finished - started;
  return SALTS_OK;
}

static int adapter_bench_measure_stages(adapter_bench_fixture *fixture,
                                        adapter_bench_result *result) {
  int status;
  int end_status;

  status = adapter_bench_stage_begin(fixture, &result->stages);
  if (status != SALTS_OK) return status;
  for (size_t transfer = 0u; transfer < ADAPTER_BENCH_STAGE_TRANSFERS; ++transfer) {
    status = adapter_bench_exchange(fixture, NULL);
    if (status != SALTS_OK) break;
  }
  end_status = adapter_bench_stage_end(fixture, &result->stages);
  if (status == SALTS_OK) status = end_status;
  return status;
}

static void adapter_bench_print_tables(adapter_bench_transport transport,
                                       adapter_bench_role direction,
                                       adapter_bench_result results[][ADAPTER_BENCH_MODE_COUNT],
                                       size_t payload_count) {
  const char *transport_name = adapter_bench_transport_name(transport);
  const char *direction_name = adapter_bench_direction_name(direction);
  printf("\nProtocol: loopback %s %s, %u samples x %u directional transfers, "
         "Source window=1, one fixed peer worker, rotating mode order per sample; CPU and stage "
         "passes are measured separately.\n",
         transport_name, direction_name, (unsigned)ADAPTER_BENCH_SAMPLES,
         (unsigned)ADAPTER_BENCH_TRANSFERS_PER_SAMPLE);
  for (size_t payload = 0u; payload < payload_count; ++payload) {
    const adapter_bench_result *direct = &results[payload][ADAPTER_BENCH_DIRECT];
    const double direct_throughput = adapter_bench_throughput(direct);

    printf("\nCFlow NativeIO %s %s / %zu KiB\n", transport_name, direction_name,
           direct->payload_size / 1024u);
    printf("| mode | p50 us | p50 vs direct | p95 us | p95 vs direct | "
           "p99 us | p99 vs direct | ops/s | MiB/s | throughput vs direct |\n");
    printf("| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
    for (size_t mode = 0u; mode < ADAPTER_BENCH_MODE_COUNT; ++mode) {
      const adapter_bench_result *current = &results[payload][mode];
      const double throughput = adapter_bench_throughput(current);
      printf("| %s | %.3f | %+.2f%% | %.3f | %+.2f%% | %.3f | "
             "%+.2f%% | %.0f | %.2f | %+.2f%% |\n",
             adapter_bench_mode_name((adapter_bench_mode)mode), (double)current->p50_ns / 1000.0,
             adapter_bench_delta((double)current->p50_ns, (double)direct->p50_ns),
             (double)current->p95_ns / 1000.0,
             adapter_bench_delta((double)current->p95_ns, (double)direct->p95_ns),
             (double)current->p99_ns / 1000.0,
             adapter_bench_delta((double)current->p99_ns, (double)direct->p99_ns),
             adapter_bench_rate(current), throughput,
             adapter_bench_delta(throughput, direct_throughput));
    }
  }
  printf("\nThroughput counts the directional application payload once.\n");
  printf("One ops/s unit is one completed full-payload %s operation; an operation may require "
         "multiple partial native requests.\n",
         direction_name);

  printf("\nCFlow NativeIO %s %s semantic gates\n", transport_name, direction_name);
  printf("| payload | mode | errors | rejections | stale completions |\n");
  printf("| ---: | :--- | ---: | ---: | ---: |\n");
  for (size_t payload = 0u; payload < payload_count; ++payload) {
    for (size_t mode = 0u; mode < ADAPTER_BENCH_MODE_COUNT; ++mode) {
      const adapter_bench_result *current = &results[payload][mode];
      printf("| %zu KiB | %s | %" PRIu64 " | %" PRIu64 " | %" PRIu64 " |\n",
             current->payload_size / 1024u, adapter_bench_mode_name((adapter_bench_mode)mode),
             current->errors, current->rejections, current->stale_completions);
    }
  }

  printf("\nCFlow NativeIO %s %s process CPU\n", transport_name, direction_name);
  printf("Process CPU includes the measured owner, peer worker, and Reactive "
         "Publisher/Subscriber workers; it is not per-thread CPU.\n");
  printf("| payload | mode | CPU us/transfer | MiB/CPU-s |\n");
  printf("| ---: | :--- | ---: | ---: |\n");
  for (size_t payload = 0u; payload < payload_count; ++payload) {
    for (size_t mode = 0u; mode < ADAPTER_BENCH_MODE_COUNT; ++mode) {
      const adapter_bench_result *current = &results[payload][mode];
      printf("| %zu KiB | %s | %.3f | %.2f |\n", current->payload_size / 1024u,
             adapter_bench_mode_name((adapter_bench_mode)mode),
             (double)current->cpu_ns / 1000.0 / (double)current->cpu_transfers,
             adapter_bench_cpu_throughput(current));
    }
  }
#if defined(_WIN32)
  printf("Windows process CPU values use GetProcessTimes and may be "
         "quantized at the host accounting interval.\n");
#endif

  printf("\nCFlow NativeIO %s %s normalized mean stage costs\n", transport_name, direction_name);
  printf("| payload | mode | admission ns | native submit ns | observe ns | "
         "Actor transition ns | Executor delivery ns | acknowledgement ns | "
         "Source delivery ns | Source owner drive ns |\n");
  printf("| ---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: | "
         "---: | ---: |\n");
  for (size_t payload = 0u; payload < payload_count; ++payload) {
    for (size_t mode = 0u; mode < ADAPTER_BENCH_MODE_COUNT; ++mode) {
      const adapter_bench_result *current = &results[payload][mode];
      const adapter_bench_stages *stages = &current->stages;
      printf("| %zu KiB | %s | %.2f | %.2f | %.2f | %.2f | %.2f | "
             "%.2f | %.2f | %.2f |\n",
             current->payload_size / 1024u, adapter_bench_mode_name((adapter_bench_mode)mode),
             adapter_bench_mean(stages->admission_ns, stages->actor_operations),
             adapter_bench_mean(stages->native_submit_ns, stages->native_submit_calls),
             adapter_bench_mean(stages->observe_ns, stages->observe_calls),
             adapter_bench_mean(stages->actor_transition_ns, stages->actor_operations),
             adapter_bench_mean(stages->executor_delivery_ns, stages->actor_operations),
             adapter_bench_mean(stages->acknowledge_ns, stages->actor_operations),
             adapter_bench_mean(stages->reactive_subscription_ns, stages->actor_operations),
             adapter_bench_mean(stages->reactive_owner_ns, stages->actor_operations));
    }
  }
  printf("Native submit and observe are normalized per corresponding call; Actor and Source "
         "columns are normalized per admitted Actor operation.\n");
}

spec("CFlow NativeIO adapter benchmark") {
  it("compares directional TCP and Pipe Actor and Source overhead against NativeIO direct") {
    static adapter_bench_result
        results[ADAPTER_BENCH_TRANSPORT_COUNT][ADAPTER_BENCH_DIRECTION_COUNT]
               [sizeof(ADAPTER_BENCH_PAYLOADS) / sizeof(ADAPTER_BENCH_PAYLOADS[0])]
               [ADAPTER_BENCH_MODE_COUNT];
    const size_t payload_count = sizeof(ADAPTER_BENCH_PAYLOADS) / sizeof(ADAPTER_BENCH_PAYLOADS[0]);

    memset(results, 0, sizeof(results));
    for (size_t transport = 0u; transport < ADAPTER_BENCH_TRANSPORT_COUNT; ++transport) {
      for (size_t direction = 0u; direction < ADAPTER_BENCH_DIRECTION_COUNT; ++direction) {
        for (size_t payload = 0u; payload < payload_count; ++payload) {
          static adapter_bench_fixture fixtures[ADAPTER_BENCH_MODE_COUNT];
          bool validated[ADAPTER_BENCH_MODE_COUNT] = {false};
          size_t initialized = 0u;
          int status = SALTS_OK;
          int cleanup_status = SALTS_OK;

          memset(fixtures, 0, sizeof(fixtures));
          for (size_t mode = 0u; mode < ADAPTER_BENCH_MODE_COUNT; ++mode) {
            adapter_bench_result *result = &results[transport][direction][payload][mode];
            result->payload_size = ADAPTER_BENCH_PAYLOADS[payload];
            status = adapter_bench_fixture_init(
                &fixtures[mode], (adapter_bench_transport)transport, (adapter_bench_mode)mode,
                (adapter_bench_role)direction, result->payload_size, NULL);
            initialized = mode + 1u;
            if (status != SALTS_OK) break;
            for (size_t warmup = 0u; warmup < ADAPTER_BENCH_WARMUP_TRANSFERS; ++warmup) {
              status = adapter_bench_exchange(&fixtures[mode], NULL);
              if (status != SALTS_OK) break;
            }
            if (status != SALTS_OK) break;
          }

          if (status == SALTS_OK) {
            for (size_t sample = 0u; sample < ADAPTER_BENCH_SAMPLES && status == SALTS_OK;
                 ++sample) {
              for (size_t offset = 0u; offset < ADAPTER_BENCH_MODE_COUNT; ++offset) {
                const size_t mode =
                    (transport + direction + payload + sample + offset) % ADAPTER_BENCH_MODE_COUNT;
                status = adapter_bench_run_sample(&fixtures[mode],
                                                  &results[transport][direction][payload][mode]);
                if (status != SALTS_OK) break;
              }
            }
          }
          if (status == SALTS_OK) {
            for (size_t offset = 0u; offset < ADAPTER_BENCH_MODE_COUNT; ++offset) {
              const size_t mode =
                  (transport + direction + payload + offset) % ADAPTER_BENCH_MODE_COUNT;
              status = adapter_bench_measure_cpu(&fixtures[mode],
                                                 &results[transport][direction][payload][mode]);
              if (status != SALTS_OK) break;
            }
          }
          if (status == SALTS_OK) {
            for (size_t offset = 0u; offset < ADAPTER_BENCH_MODE_COUNT; ++offset) {
              const size_t mode =
                  (transport + direction + payload + offset + 1u) % ADAPTER_BENCH_MODE_COUNT;
              status = adapter_bench_measure_stages(&fixtures[mode],
                                                    &results[transport][direction][payload][mode]);
              if (status != SALTS_OK) break;
            }
          }
          for (size_t mode = 0u; mode < initialized; ++mode) {
            adapter_bench_result *result = &results[transport][direction][payload][mode];
            if (status == SALTS_OK) {
              if (result->cpu_ns == 0u || result->cpu_transfers == 0u) {
                status = SALTS_EPROTO;
              } else {
                validated[mode] = adapter_bench_validate(&fixtures[mode], result);
                if (!validated[mode]) status = SALTS_EPROTO;
                else adapter_bench_finalize(result);
              }
            }
          }
          for (size_t mode = 0u; mode < initialized; ++mode) {
            adapter_bench_keep_first_status(&cleanup_status,
                                            adapter_bench_fixture_destroy(&fixtures[mode]));
          }
          check_equal(status, SALTS_OK);
          check_equal(cleanup_status, SALTS_OK);
          if (status != SALTS_OK || cleanup_status != SALTS_OK) return;
          for (size_t mode = 0u; mode < ADAPTER_BENCH_MODE_COUNT; ++mode)
            check_true(validated[mode]);
        }
        adapter_bench_print_tables((adapter_bench_transport)transport,
                                   (adapter_bench_role)direction, results[transport][direction],
                                   payload_count);
      }
    }
  }
}
