#include "cnet_kqueue_trace.h"
#include "tinytest.h"
#include <salts/error_codes.h>
#include <salts/thread.h>
#include <errno.h>
#include <sys/event.h>
#include <unistd.h>

static void trace_peer_wait(void *argument) {
  int *status = argument;
  const struct timespec timeout = {0};
  struct kevent event;
  const int fd = kqueue();
  *status = fd < 0 ? -errno : kevent(fd, NULL, 0, &event, 1, &timeout);
  if (fd >= 0) (void)close(fd);
}

spec("kqueue client trace window") {
  static int fd;
  before_each() { fd = kqueue(); check_greater_equal(fd, 0); }
  after_each() {
    cnet_kqueue_trace_stats discarded;
    (void)cnet_kqueue_trace_take(&discarded);
    if (fd >= 0) (void)close(fd);
  }

  it("interposes registration and wait calls and preserves errno") {
    struct kevent event;
    cnet_kqueue_trace_stats stats;
    const struct timespec timeout = {0};
    check_equal(cnet_kqueue_trace_begin(), SALTS_OK);
    EV_SET(&event, 1u, EVFILT_USER, EV_ADD | EV_CLEAR, 0u, 0, NULL);
    check_equal(kevent(fd, &event, 1, NULL, 0, NULL), 0);
    check_equal(kevent(fd, NULL, 0, &event, 1, &timeout), 0);
    EV_SET(&event, 1u, EVFILT_USER, EV_DELETE, 0u, 0, NULL);
    check_equal(kevent(fd, &event, 1, NULL, 0, NULL), 0);
    check_equal(kevent(-1, NULL, 0, &event, 1, &timeout), -1);
    check_equal(errno, EBADF);
    check_equal(cnet_kqueue_trace_take(&stats), SALTS_OK);
    check_equal(stats.changes.calls, 2u);
    check_equal(stats.waits.calls, 2u);
    check_equal(stats.waits.errors, 1u);
    check_equal(stats.adds, 1u);
    check_equal(stats.deletes, 1u);
    check_equal(stats.change_items, 2u);
    check_equal(stats.returned_events, 0u);
  }

  it("excludes echo peer calls on another thread and resets each window") {
    cmeta_thread_t peer;
    cnet_kqueue_trace_stats stats;
    int peer_status = SALTS_EIO;
    check_equal(cnet_kqueue_trace_begin(), SALTS_OK);
    check_equal(cnet_kqueue_trace_begin(), SALTS_EBUSY);
    check_equal(cmeta_thread_create(&peer, trace_peer_wait, &peer_status), SALTS_OK);
    check_equal(cmeta_thread_join(&peer), SALTS_OK);
    cmeta_thread_destroy(&peer);
    check_equal(peer_status, 0);
    check_equal(cnet_kqueue_trace_take(&stats), SALTS_OK);
    check_equal(stats.waits.calls, 0u);
    check_equal(stats.changes.calls, 0u);
    check_equal(cnet_kqueue_trace_take(&stats), SALTS_EINVAL);
  }
}
