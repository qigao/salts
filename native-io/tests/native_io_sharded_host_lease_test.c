#include <salts/native_io_sharded.h>
#include <salts/error_codes.h>
#include <tinytest.h>
#include <string.h>
#if !defined(_WIN32)
  #include <fcntl.h>
  #include <unistd.h>
#endif

typedef struct host_fixture {
  native_io_sharded_host_lease leases[4];
  native_io_backend *borrowed[4];
  int registered[4];
  int released[4];
  bool quiescent[4];
} host_fixture;

static native_io_backend_kind host_backend_kind(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
  return NATIVE_IO_BACKEND_EPOLL;
#else
  return NATIVE_IO_BACKEND_KQUEUE;
#endif
}
static bool host_quiescent(void *arg) {
  const bool *ready = (const bool *)arg;
  return *ready;
}
static void acquire_host(native_io_sharded_context *context, void *arg) {
  host_fixture *f = (host_fixture *)arg;
  size_t i = native_io_sharded_context_shard(context);
  native_io_sharded_completion event = {0};
  size_t count = 0u;
  int status;
  check(i < 4u);
  status = native_io_sharded_context_acquire_host(
      context, host_quiescent, &f->quiescent[i], &f->leases[i], &f->borrowed[i]);
  check_equal(status, SALTS_OK);
  check_not_null(f->borrowed[i]);
  check_equal(f->leases[i].owner_shard, (uint32_t)i);
  check(f->leases[i].owner_identity != 0u && f->leases[i].generation != 0u);
  check_equal(native_io_sharded_context_observe(context, &event, 1u, 0u, &count),
              SALTS_EPERM); /* No competing observer while lease is live. */
  status = native_io_sharded_context_observe_host(
      context, f->leases[i], &event, 1u, 0u, &count);
  check(status == SALTS_OK || status == SALTS_ETIMEDOUT);
  ++f->registered[i];
}
static void release_host(native_io_sharded_context *context, void *arg) {
  host_fixture *f = (host_fixture *)arg;
  size_t i = native_io_sharded_context_shard(context);
  native_io_sharded_host_lease stale = f->leases[i];
  check(i < 4u);
  check_equal(native_io_sharded_context_release_host(context, f->leases[i]), SALTS_EBUSY);
  stale.generation++;
  check_equal(native_io_sharded_context_release_host(context, stale), SALTS_ENOENT);
  f->quiescent[i] = true;
#if !defined(_WIN32)
  /* A misreported quiescent host must NOT release its borrowed backend while
   * a raw socket/pipe endpoint is still attached, even with zero requests. */
  {
    int handles[2] = {-1, -1};
    native_io_endpoint raw = {0};
    native_io_sharded_endpoint forged = {0}, sg_owned = {0};
    int flags;
    check_equal(pipe(handles), 0);
    flags = fcntl(handles[0], F_GETFL, 0);
    check(flags >= 0);
    check_equal(fcntl(handles[0], F_SETFL, flags | O_NONBLOCK), 0);
    check_equal(native_io_backend_attach_pipe(
                    f->borrowed[i], (uintptr_t)handles[0],
                    NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &raw), SALTS_OK);
    check_equal(native_io_sharded_context_release_host(context, f->leases[i]),
                SALTS_EBUSY);
    forged.owner_identity = f->leases[i].owner_identity;
    forged.owner_shard = f->leases[i].owner_shard;
    forged.native_endpoint = raw;
    check_equal(native_io_sharded_context_release_pipe(context, forged),
                SALTS_ENOENT); /* Borrowed raw is NOT SG-wrapped. */
    check_equal(native_io_backend_release_pipe(f->borrowed[i], raw), SALTS_OK);
    (void)close(handles[0]); (void)close(handles[1]);

    check_equal(pipe(handles), 0);
    flags = fcntl(handles[0], F_GETFL, 0);
    check(flags >= 0);
    check_equal(fcntl(handles[0], F_SETFL, flags | O_NONBLOCK), 0);
    check_equal(native_io_sharded_context_attach_pipe(
                    context, (uintptr_t)handles[0],
                    NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &sg_owned), SALTS_OK);
    /* An SG-owned endpoint is allowed to continue after the host lease ends. */
    check_equal(native_io_sharded_context_release_host(context, f->leases[i]),
                SALTS_OK);
    check_equal(native_io_sharded_context_release_pipe(context, sg_owned),
                SALTS_OK);
    (void)close(handles[0]); (void)close(handles[1]);
  }
#else
  check_equal(native_io_sharded_context_release_host(context, f->leases[i]), SALTS_OK);
#endif
  check_equal(native_io_sharded_context_release_host(context, f->leases[i]), SALTS_ENOENT);
  ++f->released[i];
}
static void host_topology_test(size_t shard_count) {
  native_io_sharded *sg = NULL;
  host_fixture f = {0};
  native_io_sharded_config config = {
      shard_count, 8u, {host_backend_kind(), 8u, 8u, 8u}};
  native_io_sharded_task take = {acquire_host, NULL, NULL, &f};
  native_io_sharded_task drop = {release_host, NULL, NULL, &f};
  check(shard_count == 2u || shard_count == 4u);
  check_equal(native_io_sharded_create(&config, &sg), SALTS_OK);
  check_not_null(sg);
  for (size_t i = 0u; i < shard_count; ++i)
    check_equal(native_io_sharded_submit_to(sg, i, &take), SALTS_OK);
  check_equal(native_io_sharded_wait(sg), SALTS_OK);
  for (size_t i = 0u; i < shard_count; ++i) {
    check_equal(f.registered[i], 1);
    for (size_t j = 0u; j < i; ++j)
      check(f.borrowed[i] != f.borrowed[j]);
  }
  check_equal(native_io_sharded_shutdown(sg), SALTS_EBUSY);
  for (size_t i = 0u; i < shard_count; ++i) {
    check_equal(native_io_sharded_submit_to(sg, i, &drop), SALTS_OK);
    check_equal(native_io_sharded_wait(sg), SALTS_OK);
    check_equal(f.released[i], 1);
    if (i + 1u < shard_count)
      check_equal(native_io_sharded_shutdown(sg), SALTS_EBUSY);
  }
  check_equal(native_io_sharded_shutdown(sg), SALTS_OK);
  check_equal(native_io_sharded_destroy(sg), SALTS_OK);
}

spec("NativeIO SG long-lived host-consumer lease") {
  it("enforces owner authority and endpoint quiescence with two shards") {
    host_topology_test(2u);
  }
  it("keeps each of four owner shards independently borrowed and drained") {
    host_topology_test(4u);
  }
}
