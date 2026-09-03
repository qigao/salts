#include <cflow/process.h>

#include <cflow/io_pipe.h>
#include <salts/error_codes.h>
#include <salts/thread.h>

#include <limits.h>
#include <stdio.h>
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
  #include <unistd.h>
#endif

enum { CFLOW_PROCESS_PIPE_NAME_CAPACITY = 192, CFLOW_PROCESS_PIPE_BUFFER_CAPACITY = 4096 };

typedef struct cflow_process_impl cflow_process_impl;

typedef struct cflow_process_slot {
  cflow_io_native_pipe_operation operation;
  cflow_process_impl *owner;
  cflow_io_request_id request_id;
  cflow_process_stream stream;
  bool in_use;
  bool delivered;
} cflow_process_slot;

_Static_assert(offsetof(cflow_process_slot, operation) == 0u,
               "native operation must remain the slot prefix");

struct cflow_process_impl {
  cflow_io_native_backend backend;
  cflow_executor executor;
  cflow_io_actor actor;
  cflow_process_slot *slots;
  size_t slot_capacity;
  salts_mutex_t gate;
  salts_process_t *native_process;
  cflow_io_pipe_endpoint stdin_endpoint;
  cflow_io_pipe_endpoint stdout_endpoint;
  cflow_io_pipe_endpoint stderr_endpoint;
  cflow_process_completion_fn completion;
  void *completion_user;
  int cleanup_error;
  bool close_requested;
  bool driver_active;
};

typedef struct cflow_process_pipe_pair {
  cflow_io_pipe_endpoint parent;
  uintptr_t child;
} cflow_process_pipe_pair;

static cflow_process_impl *process_impl(cflow_process *process) {
  return process != NULL ? (cflow_process_impl *)process->impl : NULL;
}

static const cflow_process_impl *process_const_impl(const cflow_process *process) {
  return process != NULL ? (const cflow_process_impl *)process->impl : NULL;
}

static void process_pair_init(cflow_process_pipe_pair *pair) {
  cflow_io_pipe_endpoint_init(&pair->parent);
  pair->child = SALTS_PROCESS_STDIO_INHERIT;
}

static void process_child_close(uintptr_t *handle) {
  if (handle == NULL || *handle == SALTS_PROCESS_STDIO_INHERIT) return;
#if defined(_WIN32)
  (void)CloseHandle((HANDLE)*handle);
#else
  (void)close((int)*handle);
#endif
  *handle = SALTS_PROCESS_STDIO_INHERIT;
}

static void process_pair_close(cflow_process_pipe_pair *pair) {
  process_child_close(&pair->child);
  (void)cflow_io_pipe_endpoint_close(&pair->parent);
}

#if defined(_WIN32)

static int process_pipe_pair_create(cflow_process_pipe_pair *pair, bool parent_writes,
                                    unsigned int discriminator) {
  static volatile LONG pipe_sequence = 0;
  char name[CFLOW_PROCESS_PIPE_NAME_CAPACITY];
  SECURITY_ATTRIBUTES child_security = {sizeof(child_security), NULL, TRUE};
  OVERLAPPED overlapped;
  HANDLE event = NULL;
  HANDLE parent = INVALID_HANDLE_VALUE;
  HANDLE child = INVALID_HANDLE_VALUE;
  DWORD server_access = parent_writes ? PIPE_ACCESS_OUTBOUND : PIPE_ACCESS_INBOUND;
  DWORD child_access = parent_writes ? GENERIC_READ : GENERIC_WRITE;
  DWORD error;
  DWORD bytes = 0u;
  int status = SALTS_OK;

  snprintf(name, sizeof(name), "\\\\.\\pipe\\cflow-process-%lu-%lu-%u",
           (unsigned long)GetCurrentProcessId(),
           (unsigned long)InterlockedIncrement(&pipe_sequence), discriminator);
  parent = CreateNamedPipeA(
      name, server_access | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1u,
      CFLOW_PROCESS_PIPE_BUFFER_CAPACITY, CFLOW_PROCESS_PIPE_BUFFER_CAPACITY, 0u, NULL);
  if (parent == INVALID_HANDLE_VALUE) return -(int)GetLastError();
  event = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (event == NULL) {
    status = -(int)GetLastError();
    goto failed;
  }
  memset(&overlapped, 0, sizeof(overlapped));
  overlapped.hEvent = event;
  if (!ConnectNamedPipe(parent, &overlapped)) {
    error = GetLastError();
    if (error != ERROR_IO_PENDING) {
      status = -(int)error;
      goto failed;
    }
  }
  child = CreateFileA(name, child_access, 0u, &child_security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                      NULL);
  if (child == INVALID_HANDLE_VALUE) {
    status = -(int)GetLastError();
    (void)CancelIoEx(parent, &overlapped);
    (void)GetOverlappedResult(parent, &overlapped, &bytes, TRUE);
    goto failed;
  }
  if (!GetOverlappedResult(parent, &overlapped, &bytes, TRUE)) {
    status = -(int)GetLastError();
    goto failed;
  }
  (void)CloseHandle(event);
  pair->parent.handle = (uintptr_t)parent;
  pair->parent.flags = CFLOW_IO_NATIVE_PIPE_ASYNC_CAPABLE;
  pair->child = (uintptr_t)child;
  return SALTS_OK;

failed:
  if (child != INVALID_HANDLE_VALUE) (void)CloseHandle(child);
  if (event != NULL) (void)CloseHandle(event);
  if (parent != INVALID_HANDLE_VALUE) (void)CloseHandle(parent);
  return status;
}

#else

static int process_pipe_pair_create(cflow_process_pipe_pair *pair, bool parent_writes,
                                    unsigned int discriminator) {
  int handles[2] = {-1, -1};
  int parent_index = parent_writes ? 1 : 0;
  int child_index = parent_writes ? 0 : 1;
  int flags;
  (void)discriminator;
  if (pipe(handles) != 0) return -errno;
  if (fcntl(handles[0], F_SETFD, FD_CLOEXEC) != 0 || fcntl(handles[1], F_SETFD, FD_CLOEXEC) != 0) {
    int status = -errno;
    (void)close(handles[0]);
    (void)close(handles[1]);
    return status;
  }
  flags = fcntl(handles[parent_index], F_GETFL, 0);
  if (flags < 0 || fcntl(handles[parent_index], F_SETFL, flags | O_NONBLOCK) != 0) {
    int status = -errno;
    (void)close(handles[0]);
    (void)close(handles[1]);
    return status;
  }
  pair->parent.handle = (uintptr_t)handles[parent_index];
  pair->parent.flags = CFLOW_IO_NATIVE_PIPE_ASYNC_CAPABLE;
  pair->child = (uintptr_t)handles[child_index];
  return SALTS_OK;
}

#endif

static void process_record_cleanup_error(cflow_process_impl *impl, int status) {
  if (status != SALTS_OK && status != SALTS_ENOENT && impl->cleanup_error == SALTS_OK)
    impl->cleanup_error = status;
}

static void process_close_parent_endpoint(cflow_process_impl *impl,
                                          cflow_io_pipe_endpoint *endpoint) {
  uintptr_t handle;
  int status;
  if (!cflow_io_pipe_endpoint_is_valid(endpoint)) return;
  handle = endpoint->handle;
  status = cflow_io_pipe_endpoint_close(endpoint);
  process_record_cleanup_error(impl, status);
  status = cflow_io_native_backend_forget_pipe(&impl->backend, handle);
  process_record_cleanup_error(impl, status);
}

static void process_slot_release(void *operation_user) {
  cflow_process_slot *slot = (cflow_process_slot *)operation_user;
  cflow_process_impl *impl;
  if (slot == NULL || slot->owner == NULL) return;
  impl = slot->owner;
  salts_mutex_lock(&impl->gate);
  memset(&slot->operation, 0, sizeof(slot->operation));
  slot->request_id = 0u;
  slot->in_use = false;
  slot->delivered = false;
  salts_mutex_unlock(&impl->gate);
}

static void process_actor_completion(void *user, cflow_io_request_id request_id,
                                     cflow_io_lease_id lease_id, void *operation_user,
                                     const cflow_io_completion *completion) {
  cflow_process_impl *impl = (cflow_process_impl *)user;
  cflow_process_slot *slot = (cflow_process_slot *)operation_user;
  impl->completion(impl->completion_user, request_id, lease_id, slot->stream, completion);
  salts_mutex_lock(&impl->gate);
  slot->delivered = true;
  salts_mutex_unlock(&impl->gate);
}

static void process_start_cleanup(cflow_process_impl *impl, bool backend_initialized,
                                  bool executor_initialized, bool actor_initialized) {
  if (impl == NULL) return;
  if (actor_initialized) {
    (void)cflow_io_actor_close(&impl->actor);
    (void)cflow_io_actor_destroy(&impl->actor);
  }
  if (backend_initialized) {
    (void)cflow_io_native_backend_shutdown(&impl->backend);
    (void)cflow_io_native_backend_destroy(&impl->backend);
  }
  if (executor_initialized) {
    (void)cflow_executor_shutdown(&impl->executor);
    cflow_executor_destroy(&impl->executor);
  }
  if (impl->native_process != NULL) salts_process_destroy(impl->native_process);
  (void)cflow_io_pipe_endpoint_close(&impl->stdin_endpoint);
  (void)cflow_io_pipe_endpoint_close(&impl->stdout_endpoint);
  (void)cflow_io_pipe_endpoint_close(&impl->stderr_endpoint);
  salts_mutex_destroy(&impl->gate);
  free(impl->slots);
  free(impl);
}

int cflow_process_start(cflow_process *process, const salts_process_options_t *options,
                        const cflow_process_config *config) {
  const unsigned int conflicting_flags =
      SALTS_PROCESS_PIPE_STDIN | SALTS_PROCESS_CAPTURE_STDOUT | SALTS_PROCESS_CAPTURE_STDERR;
  cflow_process_impl *impl;
  cflow_process_pipe_pair stdin_pair;
  cflow_process_pipe_pair stdout_pair;
  cflow_process_pipe_pair stderr_pair;
  salts_process_stdio_bindings_t bindings;
  cflow_io_native_backend_config backend_config;
  cflow_io_actor_config actor_config;
  bool backend_initialized = false;
  bool executor_initialized = false;
  bool actor_initialized = false;
  size_t index;
  int status;

  if (process == NULL || process->impl != NULL || options == NULL || config == NULL ||
      config->request_capacity == 0u || config->command_capacity == 0u ||
      config->completion_batch_capacity == 0u || config->completion == NULL ||
      (options->flags & conflicting_flags) != 0u ||
      config->request_capacity > SIZE_MAX / sizeof(cflow_process_slot))
    return SALTS_EINVAL;
  if (!cflow_io_native_backend_pipe_supported(config->backend_kind)) return SALTS_ENOTSUP;

  process_pair_init(&stdin_pair);
  process_pair_init(&stdout_pair);
  process_pair_init(&stderr_pair);
  impl = (cflow_process_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return SALTS_ENOMEM;
  cflow_io_pipe_endpoint_init(&impl->stdin_endpoint);
  cflow_io_pipe_endpoint_init(&impl->stdout_endpoint);
  cflow_io_pipe_endpoint_init(&impl->stderr_endpoint);
  impl->slots = (cflow_process_slot *)calloc(config->request_capacity, sizeof(*impl->slots));
  if (impl->slots == NULL) {
    free(impl);
    return SALTS_ENOMEM;
  }
  impl->slot_capacity = config->request_capacity;
  impl->completion = config->completion;
  impl->completion_user = config->completion_user;
  salts_mutex_init(&impl->gate);
  if (impl->gate == NULL) {
    free(impl->slots);
    free(impl);
    return SALTS_ENOMEM;
  }
  for (index = 0u; index < impl->slot_capacity; ++index)
    impl->slots[index].owner = impl;

  backend_config = (cflow_io_native_backend_config){config->backend_kind, config->request_capacity,
                                                    config->completion_batch_capacity};
  status = cflow_io_native_backend_init(&impl->backend, &backend_config);
  if (status != SALTS_OK) goto failed;
  backend_initialized = true;
  if (!cflow_executor_manual_init_with_capacity(&impl->executor, config->request_capacity)) {
    status = SALTS_ENOMEM;
    goto failed;
  }
  executor_initialized = true;
  memset(&actor_config, 0, sizeof(actor_config));
  actor_config.request_capacity = config->request_capacity;
  actor_config.command_capacity = config->command_capacity;
  actor_config.executor = &impl->executor;
  actor_config.backend = cflow_io_native_backend_pipe_actor_ops();
  actor_config.backend_user = &impl->backend;
  actor_config.completion = process_actor_completion;
  actor_config.completion_user = impl;
  status = cflow_io_actor_init(&impl->actor, &actor_config);
  if (status != SALTS_OK) goto failed;
  actor_initialized = true;

  status = process_pipe_pair_create(&stdin_pair, true, 0u);
  if (status == SALTS_OK) status = process_pipe_pair_create(&stdout_pair, false, 1u);
  if (status == SALTS_OK) status = process_pipe_pair_create(&stderr_pair, false, 2u);
  if (status != SALTS_OK) goto failed_pairs;
  bindings.stdin_handle = stdin_pair.child;
  bindings.stdout_handle = stdout_pair.child;
  bindings.stderr_handle = stderr_pair.child;
  status = salts_process_spawn_with_stdio(options, &bindings, &impl->native_process);
  process_child_close(&stdin_pair.child);
  process_child_close(&stdout_pair.child);
  process_child_close(&stderr_pair.child);
  if (status != SALTS_OK) goto failed_pairs;
  impl->stdin_endpoint = stdin_pair.parent;
  impl->stdout_endpoint = stdout_pair.parent;
  impl->stderr_endpoint = stderr_pair.parent;
  cflow_io_pipe_endpoint_init(&stdin_pair.parent);
  cflow_io_pipe_endpoint_init(&stdout_pair.parent);
  cflow_io_pipe_endpoint_init(&stderr_pair.parent);
  process->impl = impl;
  return SALTS_OK;

failed_pairs:
  process_pair_close(&stdin_pair);
  process_pair_close(&stdout_pair);
  process_pair_close(&stderr_pair);
failed:
  process_start_cleanup(impl, backend_initialized, executor_initialized, actor_initialized);
  return status;
}

static cflow_process_submit_result process_submit_result(cflow_process_submit_status status,
                                                         cflow_io_request_id request_id) {
  cflow_process_submit_result result = {status, request_id};
  return result;
}

static cflow_process_submit_status process_map_submit(cflow_io_submit_status status) {
  switch (status) {
  case CFLOW_IO_SUBMIT_ACCEPTED:
    return CFLOW_PROCESS_SUBMIT_ACCEPTED;
  case CFLOW_IO_SUBMIT_INVALID_ARGUMENT:
    return CFLOW_PROCESS_SUBMIT_INVALID_ARGUMENT;
  case CFLOW_IO_SUBMIT_FULL:
    return CFLOW_PROCESS_SUBMIT_FULL;
  case CFLOW_IO_SUBMIT_CLOSED:
    return CFLOW_PROCESS_SUBMIT_CLOSED;
  case CFLOW_IO_SUBMIT_LEASE_IN_USE:
    return CFLOW_PROCESS_SUBMIT_LEASE_IN_USE;
  case CFLOW_IO_SUBMIT_ID_EXHAUSTED:
    return CFLOW_PROCESS_SUBMIT_ID_EXHAUSTED;
  }
  return CFLOW_PROCESS_SUBMIT_INVALID_ARGUMENT;
}

static cflow_process_submit_result process_try_submit(cflow_process *process,
                                                      cflow_io_lease_id lease_id,
                                                      cflow_process_stream stream, void *buffer,
                                                      size_t length) {
  cflow_process_impl *impl = process_impl(process);
  cflow_io_pipe_endpoint *endpoint;
  cflow_process_slot *slot = NULL;
  cflow_io_operation actor_operation;
  cflow_io_submit_result submitted;
  size_t index;
  if (impl == NULL || buffer == NULL || length == 0u || length > UINT32_MAX)
    return process_submit_result(CFLOW_PROCESS_SUBMIT_INVALID_ARGUMENT, 0u);
  endpoint = stream == CFLOW_PROCESS_STDIN    ? &impl->stdin_endpoint
             : stream == CFLOW_PROCESS_STDOUT ? &impl->stdout_endpoint
                                              : &impl->stderr_endpoint;
  if (!cflow_io_pipe_endpoint_is_valid(endpoint))
    return process_submit_result(CFLOW_PROCESS_SUBMIT_CLOSED, 0u);
  salts_mutex_lock(&impl->gate);
  if (impl->close_requested) {
    salts_mutex_unlock(&impl->gate);
    return process_submit_result(CFLOW_PROCESS_SUBMIT_CLOSED, 0u);
  }
  for (index = 0u; index < impl->slot_capacity; ++index) {
    if (!impl->slots[index].in_use) {
      slot = &impl->slots[index];
      slot->operation.kind =
          stream == CFLOW_PROCESS_STDIN ? CFLOW_IO_NATIVE_PIPE_WRITE : CFLOW_IO_NATIVE_PIPE_READ;
      slot->operation.handle = endpoint->handle;
      slot->operation.buffer = buffer;
      slot->operation.length = length;
      slot->operation.flags = endpoint->flags;
      slot->stream = stream;
      slot->request_id = 0u;
      slot->in_use = true;
      slot->delivered = false;
      break;
    }
  }
  salts_mutex_unlock(&impl->gate);
  if (slot == NULL) return process_submit_result(CFLOW_PROCESS_SUBMIT_FULL, 0u);
  actor_operation = (cflow_io_operation){&slot->operation, process_slot_release};
  submitted = cflow_io_actor_try_submit(&impl->actor, lease_id, &actor_operation);
  if (submitted.status != CFLOW_IO_SUBMIT_ACCEPTED) {
    process_slot_release(&slot->operation);
    return process_submit_result(process_map_submit(submitted.status), 0u);
  }
  salts_mutex_lock(&impl->gate);
  slot->request_id = submitted.request_id;
  salts_mutex_unlock(&impl->gate);
  return process_submit_result(CFLOW_PROCESS_SUBMIT_ACCEPTED, submitted.request_id);
}

cflow_process_submit_result cflow_process_try_write_stdin(cflow_process *process,
                                                          cflow_io_lease_id lease_id,
                                                          const void *buffer, size_t length) {
  return process_try_submit(process, lease_id, CFLOW_PROCESS_STDIN, (void *)buffer, length);
}

cflow_process_submit_result cflow_process_try_read_stdout(cflow_process *process,
                                                          cflow_io_lease_id lease_id, void *buffer,
                                                          size_t length) {
  return process_try_submit(process, lease_id, CFLOW_PROCESS_STDOUT, buffer, length);
}

cflow_process_submit_result cflow_process_try_read_stderr(cflow_process *process,
                                                          cflow_io_lease_id lease_id, void *buffer,
                                                          size_t length) {
  return process_try_submit(process, lease_id, CFLOW_PROCESS_STDERR, buffer, length);
}

cflow_io_cancel_status cflow_process_try_cancel(cflow_process *process,
                                                cflow_io_request_id request_id) {
  cflow_process_impl *impl = process_impl(process);
  return impl != NULL ? cflow_io_actor_try_cancel(&impl->actor, request_id)
                      : CFLOW_IO_CANCEL_INVALID_ARGUMENT;
}

int cflow_process_close_stdin(cflow_process *process) {
  cflow_process_impl *impl = process_impl(process);
  size_t index;
  if (impl == NULL) return SALTS_EINVAL;
  salts_mutex_lock(&impl->gate);
  for (index = 0u; index < impl->slot_capacity; ++index) {
    if (impl->slots[index].in_use && impl->slots[index].stream == CFLOW_PROCESS_STDIN) {
      salts_mutex_unlock(&impl->gate);
      return SALTS_EBUSY;
    }
  }
  salts_mutex_unlock(&impl->gate);
  process_close_parent_endpoint(impl, &impl->stdin_endpoint);
  return impl->cleanup_error;
}

int cflow_process_poll(const cflow_process *process, salts_process_result_t *out_result) {
  const cflow_process_impl *impl = process_const_impl(process);
  return impl != NULL ? salts_process_poll(impl->native_process, out_result) : SALTS_EINVAL;
}

int cflow_process_terminate(cflow_process *process) {
  cflow_process_impl *impl = process_impl(process);
  return impl != NULL ? salts_process_terminate(impl->native_process) : SALTS_EINVAL;
}

static cflow_io_request_id process_delivered_request(cflow_process_impl *impl) {
  cflow_io_request_id request_id = 0u;
  size_t index;
  salts_mutex_lock(&impl->gate);
  for (index = 0u; index < impl->slot_capacity; ++index) {
    if (impl->slots[index].in_use && impl->slots[index].delivered) {
      request_id = impl->slots[index].request_id;
      break;
    }
  }
  salts_mutex_unlock(&impl->gate);
  return request_id;
}

int cflow_process_run_ready(cflow_process *process, size_t max_steps, size_t *progressed) {
  cflow_process_impl *impl = process_impl(process);
  size_t count = 0u;
  int status = SALTS_OK;
  if (impl == NULL || max_steps == 0u || progressed == NULL) return SALTS_EINVAL;
  salts_mutex_lock(&impl->gate);
  if (impl->driver_active) {
    salts_mutex_unlock(&impl->gate);
    return SALTS_EBUSY;
  }
  impl->driver_active = true;
  salts_mutex_unlock(&impl->gate);
  while (count < max_steps) {
    cflow_io_request_id request_id = process_delivered_request(impl);
    cflow_io_run_result actor_result;
    if (request_id != 0u) {
      cflow_io_ack_status ack = cflow_io_actor_acknowledge(&impl->actor, request_id);
      if (ack == CFLOW_IO_ACK_RELEASED) {
        ++count;
        continue;
      }
      status = ack == CFLOW_IO_ACK_BUSY ? SALTS_EBUSY : SALTS_EPROTO;
      break;
    }
    actor_result = cflow_io_actor_run_one(&impl->actor);
    if (actor_result.status == CFLOW_IO_RUN_PROGRESSED) {
      ++count;
      continue;
    }
    if (actor_result.status == CFLOW_IO_RUN_BUSY) {
      status = SALTS_EBUSY;
      break;
    }
    if (actor_result.status == CFLOW_IO_RUN_INVALID_ARGUMENT) {
      status = SALTS_EINVAL;
      break;
    }
    if (cflow_executor_run_one(&impl->executor)) {
      ++count;
      continue;
    }
    break;
  }
  salts_mutex_lock(&impl->gate);
  impl->driver_active = false;
  salts_mutex_unlock(&impl->gate);
  if (impl->close_requested && cflow_io_actor_is_quiescent(&impl->actor)) {
    process_close_parent_endpoint(impl, &impl->stdin_endpoint);
    process_close_parent_endpoint(impl, &impl->stdout_endpoint);
    process_close_parent_endpoint(impl, &impl->stderr_endpoint);
  }
  *progressed = count;
  return status;
}

bool cflow_process_get_stats(const cflow_process *process, cflow_process_stats *out) {
  cflow_process_impl *impl = process != NULL ? (cflow_process_impl *)process->impl : NULL;
  cflow_process_stats snapshot = {0};
  if (impl == NULL || out == NULL || !cflow_io_actor_get_stats(&impl->actor, &snapshot.io))
    return false;
  salts_mutex_lock(&impl->gate);
  snapshot.stdin_open = cflow_io_pipe_endpoint_is_valid(&impl->stdin_endpoint);
  snapshot.stdout_open = cflow_io_pipe_endpoint_is_valid(&impl->stdout_endpoint);
  snapshot.stderr_open = cflow_io_pipe_endpoint_is_valid(&impl->stderr_endpoint);
  snapshot.close_requested = impl->close_requested;
  snapshot.cleanup_error = impl->cleanup_error;
  salts_mutex_unlock(&impl->gate);
  *out = snapshot;
  return true;
}

int cflow_process_close(cflow_process *process) {
  cflow_process_impl *impl = process_impl(process);
  int actor_status;
  int process_status;
  if (impl == NULL) return SALTS_EINVAL;
  actor_status = cflow_io_actor_close(&impl->actor);
  if (actor_status != SALTS_OK && actor_status != SALTS_EALREADY) return actor_status;
  salts_mutex_lock(&impl->gate);
  impl->close_requested = true;
  salts_mutex_unlock(&impl->gate);
  process_status = salts_process_terminate(impl->native_process);
  return process_status == SALTS_OK ? SALTS_OK : process_status;
}

bool cflow_process_is_quiescent(const cflow_process *process) {
  const cflow_process_impl *impl = process_const_impl(process);
  salts_process_result_t result;
  if (impl == NULL || !impl->close_requested || !cflow_io_actor_is_quiescent(&impl->actor) ||
      cflow_io_pipe_endpoint_is_valid(&impl->stdin_endpoint) ||
      cflow_io_pipe_endpoint_is_valid(&impl->stdout_endpoint) ||
      cflow_io_pipe_endpoint_is_valid(&impl->stderr_endpoint))
    return false;
  return salts_process_poll(impl->native_process, &result) == SALTS_OK;
}

int cflow_process_destroy(cflow_process *process) {
  cflow_process_impl *impl = process_impl(process);
  int result;
  int status;
  if (impl == NULL) return SALTS_EINVAL;
  if (!cflow_process_is_quiescent(process)) return SALTS_EBUSY;
  result = impl->cleanup_error;
  status = cflow_io_native_backend_shutdown(&impl->backend);
  if (status != SALTS_OK && status != SALTS_EALREADY) return status;
  status = cflow_io_actor_destroy(&impl->actor);
  if (status != SALTS_OK) return status;
  status = cflow_io_native_backend_destroy(&impl->backend);
  if (status != SALTS_OK && result == SALTS_OK) result = status;
  if (!cflow_executor_shutdown(&impl->executor) && result == SALTS_OK) result = SALTS_EBUSY;
  cflow_executor_destroy(&impl->executor);
  salts_process_destroy(impl->native_process);
  salts_mutex_destroy(&impl->gate);
  free(impl->slots);
  free(impl);
  process->impl = NULL;
  return result;
}
