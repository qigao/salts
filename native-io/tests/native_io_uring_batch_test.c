#if !defined(_GNU_SOURCE)
  #define _GNU_SOURCE
#endif

#include "../src/native_io_internal.h"

#include <salts/error_codes.h>

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

static int make_nonblocking_pipe(int descriptors[2]) {
  int flags;
  if (pipe(descriptors) != 0) return -1;
  flags = fcntl(descriptors[0], F_GETFL, 0);
  if (flags < 0 || fcntl(descriptors[0], F_SETFL, flags | O_NONBLOCK) != 0) return -1;
  flags = fcntl(descriptors[1], F_GETFL, 0);
  if (flags < 0 || fcntl(descriptors[1], F_SETFL, flags | O_NONBLOCK) != 0) return -1;
  return 0;
}

static int fail(const char *message) {
  fprintf(stderr, "native_io_uring_batch_test: %s\n", message);
  return 1;
}

int main(void) {
#if !defined(__linux__)
  return 0;
#else
  static const unsigned char payload[] = {0x41u, 0x42u, 0x43u, 0x44u};
  native_io_backend backend = {0};
  const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 2u, 2u, 2u};
  int descriptors[2] = {-1, -1};
  native_io_endpoint endpoints[2] = {0};
  native_io_request requests[2] = {0};
  native_io_completion events[2] = {0};
  native_io_operation operations[2];
  native_io_uring_profile before = {0};
  unsigned char received[sizeof(payload)] = {0};
  size_t completed = 0u;
  int status;

  status = native_io_backend_init(&backend, &config);
  if (status != SALTS_OK) return fail("io_uring backend init failed");
  if (make_nonblocking_pipe(descriptors) != 0) return fail("pipe setup failed");
  if (native_io_backend_attach_pipe(&backend, (uintptr_t)descriptors[0],
                                    NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE,
                                    &endpoints[0]) != SALTS_OK)
    return fail("read endpoint attach failed");
  if (native_io_backend_attach_pipe(&backend, (uintptr_t)descriptors[1],
                                    NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE,
                                    &endpoints[1]) != SALTS_OK)
    return fail("write endpoint attach failed");

  operations[0] = (native_io_operation){.kind = NATIVE_IO_OPERATION_PIPE_READ,
                                       .endpoint = endpoints[0],
                                       .buffer = received,
                                       .length = sizeof(received),
                                       .user_data = 1u};
  operations[1] = (native_io_operation){.kind = NATIVE_IO_OPERATION_PIPE_WRITE,
                                       .endpoint = endpoints[1],
                                       .buffer = (void *)payload,
                                       .length = sizeof(payload),
                                       .user_data = 2u};

  if (native_io_backend_submit(&backend, &operations[0], &requests[0]) != SALTS_OK)
    return fail("read submit failed");
  if (native_io_backend_submit(&backend, &operations[1], &requests[1]) != SALTS_OK)
    return fail("write submit failed");
  if (!native_io_io_uring_profile_take(&backend, &before))
    return fail("profile unavailable");
  if (before.sqes_published != 2u) return fail("expected two published SQEs");
  if (before.enter_calls != 0u) return fail("submit entered kernel before observe");
  if (before.enter_submitted != 0u) return fail("submit reported kernel submission before observe");

  while (completed < 2u) {
    size_t count = 0u;
    status = native_io_backend_observe(&backend, events + completed, 2u - completed, 5000u, &count);
    if (status != SALTS_OK) return fail("observe failed");
    completed += count;
  }
  if (received[0] != payload[0] || received[1] != payload[1] || received[2] != payload[2] ||
      received[3] != payload[3])
    return fail("payload mismatch");

  close(descriptors[0]);
  close(descriptors[1]);
  if (native_io_backend_release_pipe(&backend, endpoints[0]) != SALTS_OK)
    return fail("read endpoint release failed");
  if (native_io_backend_release_pipe(&backend, endpoints[1]) != SALTS_OK)
    return fail("write endpoint release failed");
  if (native_io_backend_close(&backend) != SALTS_OK) return fail("backend close failed");
  if (native_io_backend_destroy(&backend) != SALTS_OK) return fail("backend destroy failed");
  return 0;
#endif
}
