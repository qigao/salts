#include <salts/native_io_sharded.h>
#include <salts/error_codes.h>
#include <tinytest.h>
#include <string.h>

typedef struct host_fixture {
  native_io_sharded_host_lease leases[2];
  native_io_backend *borrowed[2];
  int registered[2];
  int released[2];
  bool quiescent[2];
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
  check(i < 2u);
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
  check(i < 2u);
  check_equal(native_io_sharded_context_release_host(context, f->leases[i]), SALTS_EBUSY);
  stale.generation++;
  check_equal(native_io_sharded_context_release_host(context, stale), SALTS_ENOENT);
  f->quiescent[i] = true;
  check_equal(native_io_sharded_context_release_host(context, f->leases[i]), SALTS_OK);
  check_equal(native_io_sharded_context_release_host(context, f->leases[i]), SALTS_ENOENT);
  ++f->released[i];
}
spec("NativeIO SG long-lived host-consumer lease") {
  it("shares only one observe authority per owner and blocks shutdown until lease released") {
    native_io_sharded *sg = NULL;
    host_fixture f = {0};
    native_io_sharded_config config = {
        2u, 8u, {host_backend_kind(), 8u, 8u, 8u}};
    native_io_sharded_task take = {acquire_host, NULL, NULL, &f};
    native_io_sharded_task drop = {release_host, NULL, NULL, &f};
    check_equal(native_io_sharded_create(&config, &sg), SALTS_OK);
    check_not_null(sg);
    check_equal(native_io_sharded_submit_to(sg, 0u, &take), SALTS_OK);
    check_equal(native_io_sharded_submit_to(sg, 1u, &take), SALTS_OK);
    check_equal(native_io_sharded_wait(sg), SALTS_OK);
    check_equal(f.registered[0], 1);
    check_equal(f.registered[1], 1);
    check(f.borrowed[0] != f.borrowed[1]);
    check_equal(native_io_sharded_shutdown(sg), SALTS_EBUSY);
    check_equal(native_io_sharded_submit_to(sg, 0u, &drop), SALTS_OK);
    check_equal(native_io_sharded_wait(sg), SALTS_OK);
    check_equal(native_io_sharded_shutdown(sg), SALTS_EBUSY);
    check_equal(native_io_sharded_submit_to(sg, 1u, &drop), SALTS_OK);
    check_equal(native_io_sharded_wait(sg), SALTS_OK);
    check_equal(f.released[0], 1);
    check_equal(f.released[1], 1);
    check_equal(native_io_sharded_shutdown(sg), SALTS_OK);
    check_equal(native_io_sharded_destroy(sg), SALTS_OK);
  }
}
