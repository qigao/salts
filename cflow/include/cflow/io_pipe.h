#ifndef CFLOW_IO_PIPE_H
#define CFLOW_IO_PIPE_H

#include <cflow/io_native.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cflow_io_pipe_direction {
  CFLOW_IO_PIPE_READ = 1u,
  CFLOW_IO_PIPE_WRITE = 2u,
  CFLOW_IO_PIPE_DUPLEX = 3u
} cflow_io_pipe_direction;

typedef enum cflow_io_pipe_capability {
  CFLOW_IO_PIPE_WINDOWS_SERVER_ACCEPT = 0,
  CFLOW_IO_PIPE_WINDOWS_CLIENT_CONNECT,
  CFLOW_IO_PIPE_POSIX_FIFO_OPEN
} cflow_io_pipe_capability;

typedef struct cflow_io_pipe_endpoint {
  uintptr_t handle;
  uint32_t flags;
} cflow_io_pipe_endpoint;

typedef struct cflow_io_pipe_server {
  void *impl;
} cflow_io_pipe_server;

typedef enum cflow_io_pipe_submit_status {
  CFLOW_IO_PIPE_SUBMIT_ACCEPTED = 0,
  CFLOW_IO_PIPE_SUBMIT_INVALID_ARGUMENT,
  CFLOW_IO_PIPE_SUBMIT_UNSUPPORTED,
  CFLOW_IO_PIPE_SUBMIT_FULL,
  CFLOW_IO_PIPE_SUBMIT_CLOSED,
  CFLOW_IO_PIPE_SUBMIT_ID_EXHAUSTED,
  CFLOW_IO_PIPE_SUBMIT_NATIVE_ERROR
} cflow_io_pipe_submit_status;

typedef struct cflow_io_pipe_submit_result {
  cflow_io_pipe_submit_status status;
  cflow_io_request_id request_id;
  int error;
} cflow_io_pipe_submit_result;

typedef void (*cflow_io_pipe_accept_completion_fn)(void *user, cflow_io_request_id request_id,
                                                   const cflow_io_completion *completion,
                                                   cflow_io_pipe_endpoint endpoint);

typedef struct cflow_io_pipe_server_config {
  const char *name;
  cflow_io_pipe_direction direction;
  size_t request_capacity;
  size_t input_buffer_size;
  size_t output_buffer_size;
  cflow_io_pipe_accept_completion_fn completion;
  void *completion_user;
} cflow_io_pipe_server_config;

typedef struct cflow_io_pipe_server_stats {
  size_t request_capacity;
  size_t active_requests;
  uint64_t submitted;
  uint64_t completed;
  uint64_t cancelled;
  uint64_t rejected_full;
  bool admission_open;
} cflow_io_pipe_server_stats;

/** Returns compile-time platform support for one rendezvous control plane. */
bool cflow_io_pipe_capability_supported(cflow_io_pipe_capability capability);

/** Initializes an endpoint to the invalid, unowned state. */
void cflow_io_pipe_endpoint_init(cflow_io_pipe_endpoint *endpoint);

/** Returns whether endpoint currently owns one native handle or descriptor. */
bool cflow_io_pipe_endpoint_is_valid(const cflow_io_pipe_endpoint *endpoint);

/** Closes an owned endpoint and restores the invalid state. */
int cflow_io_pipe_endpoint_close(cflow_io_pipe_endpoint *endpoint);

/**
 * Initializes a bounded Windows overlapped named-pipe accept service.
 * Other platforms return SALTS_ENOTSUP without publishing state.
 * The server and all of its operations require exclusive external access;
 * run_ready additionally guarantees that callbacks use its single driver.
 */
int cflow_io_pipe_server_init(cflow_io_pipe_server *server,
                              const cflow_io_pipe_server_config *config);

/**
 * Creates one directional byte-pipe instance and begins overlapped client
 * connection. ACCEPTED transfers no endpoint yet; FULL/CLOSED are bounded
 * control results and NATIVE_ERROR carries its platform error in error.
 */
cflow_io_pipe_submit_result cflow_io_pipe_server_try_accept(cflow_io_pipe_server *server);

/** Requests cancellation; native completion remains authoritative. */
cflow_io_cancel_status cflow_io_pipe_server_try_cancel(cflow_io_pipe_server *server,
                                                       cflow_io_request_id request_id);

/**
 * Drives at most max_steps terminal callbacks on exactly one driver thread.
 * A successful callback owns its endpoint before invocation; cancellation or
 * failure receives an invalid endpoint. The callback must not reenter server.
 */
int cflow_io_pipe_server_run_ready(cflow_io_pipe_server *server, size_t max_steps,
                                   size_t *progressed);

/** Stops admission and requests cancellation for every pending accept. */
int cflow_io_pipe_server_close(cflow_io_pipe_server *server);

/** Returns true after close and authoritative completion drain. */
bool cflow_io_pipe_server_is_quiescent(const cflow_io_pipe_server *server);

/** Copies bounded admission and lifecycle statistics in O(1). */
bool cflow_io_pipe_server_get_stats(const cflow_io_pipe_server *server,
                                    cflow_io_pipe_server_stats *out);

/** Destroys a closed, quiescent server; otherwise returns SALTS_EBUSY. */
int cflow_io_pipe_server_destroy(cflow_io_pipe_server *server);

/**
 * Performs one synchronous Windows named-pipe client open attempt.
 * It never calls WaitNamedPipe or retries. Success transfers endpoint to out.
 * Missing and busy instances return SALTS_ENOENT and SALTS_EBUSY. Non-Windows
 * hosts return SALTS_ENOTSUP without modifying an initialized invalid output.
 */
int cflow_io_pipe_client_connect(const char *name, cflow_io_pipe_direction direction,
                                 cflow_io_pipe_endpoint *out);

/**
 * Opens one POSIX FIFO endpoint with O_NONBLOCK and close-on-exec semantics.
 * Duplex FIFO open is rejected because it has no portable rendezvous meaning.
 * A writer with no reader returns SALTS_EPIPE. Windows returns SALTS_ENOTSUP.
 */
int cflow_io_fifo_open(const char *path, cflow_io_pipe_direction direction,
                       cflow_io_pipe_endpoint *out);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_IO_PIPE_H */
