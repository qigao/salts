#define _GNU_SOURCE
#include <errno.h>
#include <stdatomic.h>
#include <sys/syscall.h>
#include <unistd.h>

static _Atomic int salts_test_fsync_calls = 0;

int fsync(int fd) {
  const int call = atomic_fetch_add(&salts_test_fsync_calls, 1) + 1;
  if (call == 2) {
    errno = EIO;
    return -1;
  }
  return (int)syscall(SYS_fsync, fd);
}
