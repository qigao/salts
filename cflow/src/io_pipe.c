#include <cflow/io_pipe.h>

#include <turbo/error_codes.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #if defined(interface)
    #undef interface
  #endif
  #include <windows.h>
#else
  #include <errno.h>
  #include <fcntl.h>
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <unistd.h>
#endif

enum { CFLOW_IO_PIPE_NAME_CAPACITY = 256 };

typedef enum cflow_io_pipe_slot_phase {
  CFLOW_IO_PIPE_SLOT_FREE = 0,
  CFLOW_IO_PIPE_SLOT_PENDING,
  CFLOW_IO_PIPE_SLOT_READY_OK,
  CFLOW_IO_PIPE_SLOT_READY_CANCELLED,
  CFLOW_IO_PIPE_SLOT_READY_FAILED
} cflow_io_pipe_slot_phase;

typedef struct cflow_io_pipe_slot {
  cflow_io_pipe_slot_phase phase;
  cflow_io_request_id request_id;
  uintptr_t handle;
  int error;
#if defined(_WIN32)
  OVERLAPPED overlapped;
  HANDLE event;
#endif
} cflow_io_pipe_slot;

typedef struct cflow_io_pipe_server_impl {
  cflow_io_pipe_slot *slots;
  char *name;
  size_t capacity;
  size_t active;
  size_t cursor;
  size_t input_buffer_size;
  size_t output_buffer_size;
  cflow_io_request_id next_request_id;
  cflow_io_pipe_direction direction;
  cflow_io_pipe_accept_completion_fn completion;
  void *completion_user;
  uint64_t submitted;
  uint64_t completed;
  uint64_t cancelled;
  uint64_t rejected_full;
  bool close_requested;
  bool driver_active;
} cflow_io_pipe_server_impl;

static cflow_io_pipe_submit_result pipe_submit_result(cflow_io_pipe_submit_status status,
                                                      cflow_io_request_id request_id, int error) {
  cflow_io_pipe_submit_result result;
  result.status = status;
  result.request_id = request_id;
  result.error = error;
  return result;
}

bool cflow_io_pipe_capability_supported(cflow_io_pipe_capability capability) {
#if defined(_WIN32)
  return capability == CFLOW_IO_PIPE_WINDOWS_SERVER_ACCEPT ||
         capability == CFLOW_IO_PIPE_WINDOWS_CLIENT_CONNECT;
#else
  return capability == CFLOW_IO_PIPE_POSIX_FIFO_OPEN;
#endif
}

void cflow_io_pipe_endpoint_init(cflow_io_pipe_endpoint *endpoint) {
  if (endpoint == NULL) return;
  endpoint->handle = UINTPTR_MAX;
  endpoint->flags = 0u;
}

bool cflow_io_pipe_endpoint_is_valid(const cflow_io_pipe_endpoint *endpoint) {
  return endpoint != NULL && endpoint->handle != UINTPTR_MAX;
}

int cflow_io_pipe_endpoint_close(cflow_io_pipe_endpoint *endpoint) {
  uintptr_t handle;
  if (endpoint == NULL) return TURBO_EINVAL;
  if (!cflow_io_pipe_endpoint_is_valid(endpoint)) return TURBO_OK;
  handle = endpoint->handle;
  cflow_io_pipe_endpoint_init(endpoint);
#if defined(_WIN32)
  return CloseHandle((HANDLE)handle) ? TURBO_OK : -(int)GetLastError();
#else
  return close((int)handle) == 0 ? TURBO_OK : -errno;
#endif
}

#if defined(_WIN32)

static bool pipe_direction_valid(cflow_io_pipe_direction direction) {
  return direction == CFLOW_IO_PIPE_READ || direction == CFLOW_IO_PIPE_WRITE ||
         direction == CFLOW_IO_PIPE_DUPLEX;
}

static DWORD pipe_server_access(cflow_io_pipe_direction direction) {
  if (direction == CFLOW_IO_PIPE_READ) return PIPE_ACCESS_INBOUND;
  if (direction == CFLOW_IO_PIPE_WRITE) return PIPE_ACCESS_OUTBOUND;
  return PIPE_ACCESS_DUPLEX;
}

static DWORD pipe_client_access(cflow_io_pipe_direction direction) {
  DWORD access = 0u;
  if ((direction & CFLOW_IO_PIPE_READ) != 0u) access |= GENERIC_READ;
  if ((direction & CFLOW_IO_PIPE_WRITE) != 0u) access |= GENERIC_WRITE | FILE_READ_ATTRIBUTES;
  return access;
}

static void pipe_slot_release(cflow_io_pipe_slot *slot, bool close_handle) {
  if (close_handle && slot->handle != UINTPTR_MAX) (void)CloseHandle((HANDLE)slot->handle);
  if (slot->event != NULL) (void)CloseHandle(slot->event);
  memset(slot, 0, sizeof(*slot));
  slot->handle = UINTPTR_MAX;
}

static void pipe_slot_poll(cflow_io_pipe_slot *slot) {
  DWORD bytes = 0u;
  DWORD error;
  if (slot->phase != CFLOW_IO_PIPE_SLOT_PENDING ||
      WaitForSingleObject(slot->event, 0u) != WAIT_OBJECT_0)
    return;
  if (GetOverlappedResult((HANDLE)slot->handle, &slot->overlapped, &bytes, FALSE)) {
    slot->phase = CFLOW_IO_PIPE_SLOT_READY_OK;
    return;
  }
  error = GetLastError();
  if (error == ERROR_OPERATION_ABORTED) {
    slot->phase = CFLOW_IO_PIPE_SLOT_READY_CANCELLED;
    slot->error = TURBO_OK;
  } else {
    slot->phase = CFLOW_IO_PIPE_SLOT_READY_FAILED;
    slot->error = -(int)error;
  }
}

#endif

int cflow_io_pipe_server_init(cflow_io_pipe_server *server,
                              const cflow_io_pipe_server_config *config) {
#if defined(_WIN32)
  cflow_io_pipe_server_impl *impl;
  size_t name_length;
  size_t index;
  if (server == NULL || server->impl != NULL || config == NULL || config->name == NULL ||
      config->name[0] == '\0' || !pipe_direction_valid(config->direction) ||
      config->request_capacity == 0u || config->request_capacity > PIPE_UNLIMITED_INSTANCES ||
      config->input_buffer_size == 0u || config->input_buffer_size > UINT32_MAX ||
      config->output_buffer_size == 0u || config->output_buffer_size > UINT32_MAX ||
      config->completion == NULL)
    return TURBO_EINVAL;
  name_length = strlen(config->name);
  if (name_length >= CFLOW_IO_PIPE_NAME_CAPACITY ||
      config->request_capacity > SIZE_MAX / sizeof(cflow_io_pipe_slot))
    return TURBO_EINVAL;
  impl = (cflow_io_pipe_server_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return TURBO_ENOMEM;
  impl->slots = (cflow_io_pipe_slot *)calloc(config->request_capacity, sizeof(*impl->slots));
  impl->name = (char *)malloc(name_length + 1u);
  if (impl->slots == NULL || impl->name == NULL) {
    free(impl->name);
    free(impl->slots);
    free(impl);
    return TURBO_ENOMEM;
  }
  memcpy(impl->name, config->name, name_length + 1u);
  impl->capacity = config->request_capacity;
  impl->input_buffer_size = config->input_buffer_size;
  impl->output_buffer_size = config->output_buffer_size;
  impl->direction = config->direction;
  impl->completion = config->completion;
  impl->completion_user = config->completion_user;
  impl->next_request_id = 1u;
  for (index = 0u; index < impl->capacity; ++index)
    impl->slots[index].handle = UINTPTR_MAX;
  server->impl = impl;
  return TURBO_OK;
#else
  (void)server;
  (void)config;
  return TURBO_ENOTSUP;
#endif
}

cflow_io_pipe_submit_result cflow_io_pipe_server_try_accept(cflow_io_pipe_server *server) {
#if defined(_WIN32)
  cflow_io_pipe_server_impl *impl =
      server != NULL ? (cflow_io_pipe_server_impl *)server->impl : NULL;
  cflow_io_pipe_slot *slot = NULL;
  HANDLE handle;
  DWORD error;
  size_t index;
  if (impl == NULL)
    return pipe_submit_result(CFLOW_IO_PIPE_SUBMIT_INVALID_ARGUMENT, 0u, TURBO_EINVAL);
  if (impl->close_requested)
    return pipe_submit_result(CFLOW_IO_PIPE_SUBMIT_CLOSED, 0u, TURBO_EALREADY);
  if (impl->active == impl->capacity) {
    ++impl->rejected_full;
    return pipe_submit_result(CFLOW_IO_PIPE_SUBMIT_FULL, 0u, TURBO_EBUSY);
  }
  if (impl->next_request_id == 0u)
    return pipe_submit_result(CFLOW_IO_PIPE_SUBMIT_ID_EXHAUSTED, 0u, TURBO_ERANGE);
  for (index = 0u; index < impl->capacity; ++index) {
    if (impl->slots[index].phase == CFLOW_IO_PIPE_SLOT_FREE) {
      slot = &impl->slots[index];
      break;
    }
  }
  if (slot == NULL) {
    ++impl->rejected_full;
    return pipe_submit_result(CFLOW_IO_PIPE_SUBMIT_FULL, 0u, TURBO_EBUSY);
  }
  slot->event = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (slot->event == NULL)
    return pipe_submit_result(CFLOW_IO_PIPE_SUBMIT_NATIVE_ERROR, 0u, -(int)GetLastError());
  memset(&slot->overlapped, 0, sizeof(slot->overlapped));
  slot->overlapped.hEvent = slot->event;
  handle =
      CreateNamedPipeA(impl->name, pipe_server_access(impl->direction) | FILE_FLAG_OVERLAPPED,
                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                       (DWORD)impl->capacity, (DWORD)impl->output_buffer_size,
                       (DWORD)impl->input_buffer_size, 0u, NULL);
  if (handle == INVALID_HANDLE_VALUE) {
    error = GetLastError();
    pipe_slot_release(slot, false);
    return pipe_submit_result(CFLOW_IO_PIPE_SUBMIT_NATIVE_ERROR, 0u, -(int)error);
  }
  slot->handle = (uintptr_t)handle;
  slot->request_id = impl->next_request_id++;
  if (ConnectNamedPipe(handle, &slot->overlapped)) {
    slot->phase = CFLOW_IO_PIPE_SLOT_READY_OK;
  } else {
    error = GetLastError();
    if (error == ERROR_IO_PENDING) slot->phase = CFLOW_IO_PIPE_SLOT_PENDING;
    else if (error == ERROR_PIPE_CONNECTED) slot->phase = CFLOW_IO_PIPE_SLOT_READY_OK;
    else {
      cflow_io_pipe_submit_result result =
          pipe_submit_result(CFLOW_IO_PIPE_SUBMIT_NATIVE_ERROR, 0u, -(int)error);
      pipe_slot_release(slot, true);
      return result;
    }
  }
  ++impl->active;
  ++impl->submitted;
  return pipe_submit_result(CFLOW_IO_PIPE_SUBMIT_ACCEPTED, slot->request_id, TURBO_OK);
#else
  (void)server;
  return pipe_submit_result(CFLOW_IO_PIPE_SUBMIT_UNSUPPORTED, 0u, TURBO_ENOTSUP);
#endif
}

cflow_io_cancel_status cflow_io_pipe_server_try_cancel(cflow_io_pipe_server *server,
                                                       cflow_io_request_id request_id) {
#if defined(_WIN32)
  cflow_io_pipe_server_impl *impl =
      server != NULL ? (cflow_io_pipe_server_impl *)server->impl : NULL;
  size_t index;
  if (impl == NULL || request_id == 0u) return CFLOW_IO_CANCEL_INVALID_ARGUMENT;
  for (index = 0u; index < impl->capacity; ++index) {
    cflow_io_pipe_slot *slot = &impl->slots[index];
    if (slot->request_id != request_id || slot->phase == CFLOW_IO_PIPE_SLOT_FREE) continue;
    pipe_slot_poll(slot);
    if (slot->phase != CFLOW_IO_PIPE_SLOT_PENDING) return CFLOW_IO_CANCEL_NOT_FOUND;
    if (CancelIoEx((HANDLE)slot->handle, &slot->overlapped) || GetLastError() == ERROR_NOT_FOUND)
      return CFLOW_IO_CANCEL_ACCEPTED;
    return CFLOW_IO_CANCEL_NOT_FOUND;
  }
  return CFLOW_IO_CANCEL_NOT_FOUND;
#else
  (void)server;
  (void)request_id;
  return CFLOW_IO_CANCEL_INVALID_ARGUMENT;
#endif
}

int cflow_io_pipe_server_run_ready(cflow_io_pipe_server *server, size_t max_steps,
                                   size_t *progressed) {
#if defined(_WIN32)
  cflow_io_pipe_server_impl *impl =
      server != NULL ? (cflow_io_pipe_server_impl *)server->impl : NULL;
  size_t visited = 0u;
  if (impl == NULL || max_steps == 0u || progressed == NULL) return TURBO_EINVAL;
  *progressed = 0u;
  if (impl->driver_active) return TURBO_EBUSY;
  impl->driver_active = true;
  while (*progressed < max_steps && visited < impl->capacity) {
    cflow_io_pipe_slot *slot = &impl->slots[impl->cursor];
    impl->cursor = (impl->cursor + 1u) % impl->capacity;
    ++visited;
    pipe_slot_poll(slot);
    if (slot->phase == CFLOW_IO_PIPE_SLOT_READY_OK ||
        slot->phase == CFLOW_IO_PIPE_SLOT_READY_CANCELLED ||
        slot->phase == CFLOW_IO_PIPE_SLOT_READY_FAILED) {
      cflow_io_completion completion;
      cflow_io_pipe_endpoint endpoint;
      cflow_io_request_id request_id = slot->request_id;
      cflow_io_pipe_slot_phase phase = slot->phase;
      cflow_io_pipe_endpoint_init(&endpoint);
      completion.bytes = 0u;
      completion.error = slot->error;
      if (phase == CFLOW_IO_PIPE_SLOT_READY_OK) {
        completion.kind = CFLOW_IO_COMPLETION_OK;
        endpoint.handle = slot->handle;
        endpoint.flags = CFLOW_IO_NATIVE_PIPE_ASYNC_CAPABLE;
        slot->handle = UINTPTR_MAX;
        ++impl->completed;
      } else if (phase == CFLOW_IO_PIPE_SLOT_READY_CANCELLED) {
        completion.kind = CFLOW_IO_COMPLETION_CANCELLED;
        ++impl->cancelled;
      } else {
        completion.kind = CFLOW_IO_COMPLETION_FAILED;
        ++impl->completed;
      }
      pipe_slot_release(slot, true);
      --impl->active;
      ++*progressed;
      impl->completion(impl->completion_user, request_id, &completion, endpoint);
    }
  }
  impl->driver_active = false;
  return TURBO_OK;
#else
  (void)server;
  (void)max_steps;
  if (progressed != NULL) *progressed = 0u;
  return TURBO_ENOTSUP;
#endif
}

int cflow_io_pipe_server_close(cflow_io_pipe_server *server) {
#if defined(_WIN32)
  cflow_io_pipe_server_impl *impl =
      server != NULL ? (cflow_io_pipe_server_impl *)server->impl : NULL;
  size_t index;
  if (impl == NULL) return TURBO_EINVAL;
  if (impl->close_requested) return TURBO_OK;
  impl->close_requested = true;
  for (index = 0u; index < impl->capacity; ++index) {
    cflow_io_pipe_slot *slot = &impl->slots[index];
    pipe_slot_poll(slot);
    if (slot->phase == CFLOW_IO_PIPE_SLOT_PENDING)
      (void)CancelIoEx((HANDLE)slot->handle, &slot->overlapped);
  }
  return TURBO_OK;
#else
  (void)server;
  return TURBO_ENOTSUP;
#endif
}

bool cflow_io_pipe_server_is_quiescent(const cflow_io_pipe_server *server) {
  const cflow_io_pipe_server_impl *impl =
      server != NULL ? (const cflow_io_pipe_server_impl *)server->impl : NULL;
  return impl != NULL && impl->close_requested && impl->active == 0u;
}

bool cflow_io_pipe_server_get_stats(const cflow_io_pipe_server *server,
                                    cflow_io_pipe_server_stats *out) {
  const cflow_io_pipe_server_impl *impl =
      server != NULL ? (const cflow_io_pipe_server_impl *)server->impl : NULL;
  if (impl == NULL || out == NULL) return false;
  out->request_capacity = impl->capacity;
  out->active_requests = impl->active;
  out->submitted = impl->submitted;
  out->completed = impl->completed;
  out->cancelled = impl->cancelled;
  out->rejected_full = impl->rejected_full;
  out->admission_open = !impl->close_requested;
  return true;
}

int cflow_io_pipe_server_destroy(cflow_io_pipe_server *server) {
  cflow_io_pipe_server_impl *impl =
      server != NULL ? (cflow_io_pipe_server_impl *)server->impl : NULL;
  if (impl == NULL) return TURBO_EINVAL;
  if (!impl->close_requested || impl->active != 0u || impl->driver_active) return TURBO_EBUSY;
  free(impl->name);
  free(impl->slots);
  free(impl);
  server->impl = NULL;
  return TURBO_OK;
}

int cflow_io_pipe_client_connect(const char *name, cflow_io_pipe_direction direction,
                                 cflow_io_pipe_endpoint *out) {
#if defined(_WIN32)
  HANDLE handle;
  DWORD error;
  if (name == NULL || name[0] == '\0' || !pipe_direction_valid(direction) || out == NULL ||
      cflow_io_pipe_endpoint_is_valid(out))
    return TURBO_EINVAL;
  handle = CreateFileA(name, pipe_client_access(direction), 0u, NULL, OPEN_EXISTING,
                       FILE_FLAG_OVERLAPPED, NULL);
  if (handle == INVALID_HANDLE_VALUE) {
    error = GetLastError();
    if (error == ERROR_PIPE_BUSY) return TURBO_EBUSY;
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return TURBO_ENOENT;
    return -(int)error;
  }
  out->handle = (uintptr_t)handle;
  out->flags = CFLOW_IO_NATIVE_PIPE_ASYNC_CAPABLE;
  return TURBO_OK;
#else
  (void)name;
  (void)direction;
  (void)out;
  return TURBO_ENOTSUP;
#endif
}

int cflow_io_fifo_open(const char *path, cflow_io_pipe_direction direction,
                       cflow_io_pipe_endpoint *out) {
#if defined(_WIN32)
  (void)path;
  (void)direction;
  (void)out;
  return TURBO_ENOTSUP;
#else
  struct stat info;
  int flags;
  int descriptor;
  if (path == NULL || path[0] == '\0' || out == NULL || cflow_io_pipe_endpoint_is_valid(out) ||
      (direction != CFLOW_IO_PIPE_READ && direction != CFLOW_IO_PIPE_WRITE))
    return TURBO_EINVAL;
  flags = direction == CFLOW_IO_PIPE_READ ? O_RDONLY : O_WRONLY;
  flags |= O_NONBLOCK;
  #if defined(O_CLOEXEC)
  flags |= O_CLOEXEC;
  #endif
  do {
    descriptor = open(path, flags);
  } while (descriptor < 0 && errno == EINTR);
  if (descriptor < 0) return errno == ENXIO ? TURBO_EPIPE : -errno;
  if (fstat(descriptor, &info) != 0) {
    int status = -errno;
    (void)close(descriptor);
    return status;
  }
  if (!S_ISFIFO(info.st_mode)) {
    (void)close(descriptor);
    return TURBO_ENOTSUP;
  }
  #if !defined(O_CLOEXEC)
  if (fcntl(descriptor, F_SETFD, FD_CLOEXEC) != 0) {
    int status = -errno;
    (void)close(descriptor);
    return status;
  }
  #endif
  out->handle = (uintptr_t)descriptor;
  out->flags = CFLOW_IO_NATIVE_PIPE_ASYNC_CAPABLE;
  return TURBO_OK;
#endif
}
