#include "tinytest.h"

#include <salts/native_io_sharded.h>
#include <salts/thread.h>
#include <salts_buffer.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <errno.h>
  #include <fcntl.h>
  #include <unistd.h>
#endif

enum { CNET_SHARDED_BUFFER_PIPE_CAPACITY = 4096 };

typedef struct cnet_sharded_buffer_pipe {
  uintptr_t handle;
  uintptr_t peer;
} cnet_sharded_buffer_pipe;

typedef struct cnet_sharded_buffer_state {
  native_io_sharded_endpoint endpoint;
  native_io_sharded_request request;
  native_io_sharded_completion completion;
  uintptr_t native_handle;
  uintptr_t peer;
  int attach_status;
  int admission_status;
  int observe_status;
  int release_status;
  size_t observe_count;
  size_t admission_shard;
  size_t terminal_shard;
  size_t terminal_bytes;
  uint32_t terminal_ref_count;
  native_io_completion_kind terminal_kind;
  atomic_int admissions;
  atomic_int terminals;
  atomic_int finalizes;
} cnet_sharded_buffer_state;

typedef struct cnet_sharded_buffer_token {
  mem_buffer_t *buffer;
  cnet_sharded_buffer_state *state;
} cnet_sharded_buffer_token;

typedef struct cnet_sharded_buffer_free_probe {
  atomic_int freed;
} cnet_sharded_buffer_free_probe;

static native_io_backend_kind cnet_sharded_buffer_backend(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
  return NATIVE_IO_BACKEND_EPOLL;
#elif (defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
       defined(__DragonFly__)) && \
    UINTPTR_MAX > UINT32_MAX
  return NATIVE_IO_BACKEND_KQUEUE;
#else
  return (native_io_backend_kind)0;
#endif
}

static int cnet_sharded_buffer_runtime_create(native_io_sharded **out_runtime) {
  const native_io_backend_kind kind = cnet_sharded_buffer_backend();
  const native_io_sharded_config config = {2u, 2u, {kind, 2u, 2u, 2u}};
  if (kind == (native_io_backend_kind)0 || !native_io_backend_kind_supported(kind))
    return SALTS_ENOTSUP;
  return native_io_sharded_create(&config, out_runtime);
}

static int cnet_sharded_buffer_pipe_create(cnet_sharded_buffer_pipe *pipe_endpoint) {
  if (pipe_endpoint == NULL) return SALTS_EINVAL;
#if defined(_WIN32)
  static LONG sequence = 0;
  char name[128];
  OVERLAPPED connected = {0};
  HANDLE event = NULL;
  HANDLE server = INVALID_HANDLE_VALUE;
  HANDLE client = INVALID_HANDLE_VALUE;
  DWORD error = ERROR_SUCCESS;
  BOOL pending = FALSE;
  const int length =
      snprintf(name, sizeof(name), "\\\\.\\pipe\\cnet-sharded-buffer-%lu-%ld",
               GetCurrentProcessId(), InterlockedIncrement(&sequence));
  if (length < 0 || (size_t)length >= sizeof(name)) return SALTS_ERANGE;
  server = CreateNamedPipeA(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1u,
                            CNET_SHARDED_BUFFER_PIPE_CAPACITY,
                            CNET_SHARDED_BUFFER_PIPE_CAPACITY, 0u, NULL);
  if (server == INVALID_HANDLE_VALUE) return -(int)GetLastError();
  event = CreateEventA(NULL, TRUE, FALSE, NULL);
  if (event == NULL) {
    error = GetLastError();
    goto failed;
  }
  connected.hEvent = event;
  if (!ConnectNamedPipe(server, &connected)) {
    error = GetLastError();
    if (error == ERROR_IO_PENDING)
      pending = TRUE;
    else if (error != ERROR_PIPE_CONNECTED)
      goto failed;
  }
  client = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0u, NULL, OPEN_EXISTING, 0u, NULL);
  if (client == INVALID_HANDLE_VALUE) {
    error = GetLastError();
    goto failed;
  }
  if (pending) {
    DWORD transferred = 0u;
    if (!GetOverlappedResult(server, &connected, &transferred, TRUE)) {
      error = GetLastError();
      goto failed;
    }
  }
  (void)CloseHandle(event);
  pipe_endpoint->handle = (uintptr_t)server;
  pipe_endpoint->peer = (uintptr_t)client;
  return SALTS_OK;

failed:
  if (client != INVALID_HANDLE_VALUE) (void)CloseHandle(client);
  if (server != INVALID_HANDLE_VALUE) (void)CloseHandle(server);
  if (event != NULL) (void)CloseHandle(event);
  return -(int)error;
#else
  int descriptors[2] = {-1, -1};
  int flags;
  if (pipe(descriptors) != 0) return -errno;
  flags = fcntl(descriptors[1], F_GETFL, 0);
  if (flags < 0 || fcntl(descriptors[1], F_SETFL, flags | O_NONBLOCK) != 0) {
    const int error = errno;
    (void)close(descriptors[0]);
    (void)close(descriptors[1]);
    return -error;
  }
  pipe_endpoint->handle = (uintptr_t)descriptors[1];
  pipe_endpoint->peer = (uintptr_t)descriptors[0];
  return SALTS_OK;
#endif
}

static void cnet_sharded_buffer_close_handle(uintptr_t *handle) {
  if (handle == NULL || *handle == 0u) return;
#if defined(_WIN32)
  (void)CloseHandle((HANDLE)*handle);
#else
  (void)close((int)*handle);
#endif
  *handle = 0u;
}

static int cnet_sharded_buffer_peer_read(uintptr_t peer, unsigned char *out_value) {
  if (peer == 0u || out_value == NULL) return SALTS_EINVAL;
#if defined(_WIN32)
  DWORD transferred = 0u;
  if (!ReadFile((HANDLE)peer, out_value, 1u, &transferred, NULL))
    return -(int)GetLastError();
  return transferred == 1u ? SALTS_OK : SALTS_EIO;
#else
  const ssize_t transferred = read((int)peer, out_value, 1u);
  return transferred == 1 ? SALTS_OK : transferred < 0 ? -errno : SALTS_EIO;
#endif
}

static void cnet_sharded_buffer_external_free(void *data, void *user_data) {
  cnet_sharded_buffer_free_probe *probe = (cnet_sharded_buffer_free_probe *)user_data;
  free(data);
  atomic_fetch_add_explicit(&probe->freed, 1, memory_order_release);
}

static void cnet_sharded_buffer_attach(native_io_sharded_context *context, void *arg) {
  cnet_sharded_buffer_state *state = (cnet_sharded_buffer_state *)arg;
  state->attach_status = native_io_sharded_context_attach_pipe(
      context, state->native_handle, NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &state->endpoint);
}

static void cnet_sharded_buffer_observe(native_io_sharded_context *context, void *arg) {
  cnet_sharded_buffer_state *state = (cnet_sharded_buffer_state *)arg;
  state->observe_count = 0u;
  state->observe_status =
      native_io_sharded_context_observe(context, &state->completion, 1u, 5000u,
                                        &state->observe_count);
}

static void cnet_sharded_buffer_release(native_io_sharded_context *context, void *arg) {
  cnet_sharded_buffer_state *state = (cnet_sharded_buffer_state *)arg;
  state->release_status = native_io_sharded_context_release_pipe(context, state->endpoint);
}

static void cnet_sharded_buffer_admission(native_io_sharded_context *context, int status,
                                          native_io_sharded_request request, void *arg) {
  cnet_sharded_buffer_state *state = (cnet_sharded_buffer_state *)arg;
  state->admission_status = status;
  state->request = request;
  state->admission_shard = native_io_sharded_context_shard(context);
  atomic_fetch_add_explicit(&state->admissions, 1, memory_order_release);
}

static void cnet_sharded_buffer_terminal(native_io_sharded_context *context,
                                         const native_io_sharded_completion *completion,
                                         void *arg) {
  cnet_sharded_buffer_token *token = (cnet_sharded_buffer_token *)arg;
  cnet_sharded_buffer_state *state = token->state;
  state->terminal_kind = completion->kind;
  state->terminal_bytes = completion->bytes;
  state->terminal_shard = native_io_sharded_context_shard(context);
  state->terminal_ref_count = mem_buffer_ref_count(token->buffer);
  atomic_fetch_add_explicit(&state->terminals, 1, memory_order_release);
}

static void cnet_sharded_buffer_finalize(void *arg) {
  cnet_sharded_buffer_token *token = (cnet_sharded_buffer_token *)arg;
  cnet_sharded_buffer_state *state = token->state;
  mem_buffer_release(token->buffer);
  token->buffer = NULL;
  atomic_fetch_add_explicit(&state->finalizes, 1, memory_order_release);
}

spec("CNet retained buffer adapter over NativeIO sharded ownership") {
  it("retains a mem_buffer across cross-shard raw I/O and releases only at terminal observe") {
    native_io_sharded *runtime = NULL;
    native_io_sharded *other_runtime = NULL;
    cnet_sharded_buffer_pipe pipe_endpoint = {0};
    cnet_sharded_buffer_state state = {0};
    cnet_sharded_buffer_free_probe free_probe = {0};
    cnet_sharded_buffer_token token = {0};
    cnet_sharded_buffer_token rejected_token = {0};
    native_io_sharded_task attach_task = {
        cnet_sharded_buffer_attach, NULL, NULL, &state};
    native_io_sharded_task observe_task = {
        cnet_sharded_buffer_observe, NULL, NULL, &state};
    native_io_sharded_task release_task = {
        cnet_sharded_buffer_release, NULL, NULL, &state};
    mem_buffer_t *buffer = NULL;
    unsigned char *payload = NULL;
    unsigned char received = 0u;
    int status = cnet_sharded_buffer_runtime_create(&runtime);

    if (status == SALTS_ENOTSUP) {
      check_equal(status, SALTS_ENOTSUP);
    } else {
      native_io_sharded_operation operation = {0};
      native_io_sharded_ownership ownership;
      native_io_sharded_ownership rejected_ownership;

      check_equal(status, SALTS_OK);
      check_equal(cnet_sharded_buffer_pipe_create(&pipe_endpoint), SALTS_OK);
      state.native_handle = pipe_endpoint.handle;
      state.peer = pipe_endpoint.peer;

      payload = (unsigned char *)malloc(1u);
      check_not_null(payload);
      *payload = 0x6du;
      buffer = mem_wrap_external(payload, 1u, cnet_sharded_buffer_external_free, &free_probe);
      check_not_null(buffer);
      check_equal(mem_buffer_ref_count(buffer), UINT32_C(1));

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &attach_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.attach_status, SALTS_OK);
      check_true(native_io_sharded_endpoint_valid(state.endpoint));

      token.buffer = mem_buffer_retain(buffer);
      token.state = &state;
      check_not_null(token.buffer);
      check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));
      ownership = (native_io_sharded_ownership){
          cnet_sharded_buffer_terminal, cnet_sharded_buffer_finalize, &token};
      operation.kind = NATIVE_IO_OPERATION_PIPE_WRITE;
      operation.endpoint = state.endpoint;
      operation.buffer = (void *)mem_buffer_const_data(buffer);
      operation.length = 1u;
      operation.user_data = 0x475bu;

      check_equal(native_io_sharded_try_submit_owned(
                      runtime, &operation, &ownership,
                      cnet_sharded_buffer_admission, &state),
                  SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(atomic_load_explicit(&state.admissions, memory_order_acquire), 1);
      check_equal(state.admission_status, SALTS_OK);
      check_equal(state.admission_shard, (size_t)1);
      check_true(native_io_sharded_request_valid(state.request));
      check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));
      check_equal(atomic_load_explicit(&state.finalizes, memory_order_acquire), 0);

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &observe_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.observe_status, SALTS_OK);
      check_equal(state.observe_count, (size_t)1);
      check_equal(state.completion.kind, NATIVE_IO_COMPLETION_OK);
      check_equal(state.completion.user_data, (uintptr_t)0x475bu);
      check_equal(atomic_load_explicit(&state.terminals, memory_order_acquire), 1);
      check_equal(atomic_load_explicit(&state.finalizes, memory_order_acquire), 1);
      check_equal(state.terminal_kind, NATIVE_IO_COMPLETION_OK);
      check_equal(state.terminal_bytes, (size_t)1);
      check_equal(state.terminal_shard, (size_t)1);
      check_equal(state.terminal_ref_count, UINT32_C(2));
      check_equal(mem_buffer_ref_count(buffer), UINT32_C(1));
      check_equal(cnet_sharded_buffer_peer_read(state.peer, &received), SALTS_OK);
      check_equal(received, (unsigned char)0x6du);

      check_equal(cnet_sharded_buffer_runtime_create(&other_runtime), SALTS_OK);
      rejected_token.buffer = mem_buffer_retain(buffer);
      rejected_token.state = &state;
      check_not_null(rejected_token.buffer);
      check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));
      rejected_ownership = (native_io_sharded_ownership){
          cnet_sharded_buffer_terminal, cnet_sharded_buffer_finalize, &rejected_token};
      check_equal(native_io_sharded_try_submit_owned(
                      other_runtime, &operation, &rejected_ownership,
                      cnet_sharded_buffer_admission, &state),
                  SALTS_ENOENT);
      check_equal(atomic_load_explicit(&state.admissions, memory_order_acquire), 1);
      check_equal(atomic_load_explicit(&state.finalizes, memory_order_acquire), 1);
      check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));
      mem_buffer_release(rejected_token.buffer);
      rejected_token.buffer = NULL;
      check_equal(mem_buffer_ref_count(buffer), UINT32_C(1));
      check_equal(native_io_sharded_destroy(other_runtime), SALTS_OK);
      other_runtime = NULL;

      cnet_sharded_buffer_close_handle(&pipe_endpoint.handle);
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &release_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.release_status, SALTS_OK);
      cnet_sharded_buffer_close_handle(&pipe_endpoint.peer);
      check_equal(native_io_sharded_destroy(runtime), SALTS_OK);
      runtime = NULL;

      mem_buffer_release(buffer);
      buffer = NULL;
      check_equal(atomic_load_explicit(&free_probe.freed, memory_order_acquire), 1);
    }
  }
}
