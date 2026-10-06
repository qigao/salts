#include "cnet_kqueue_trace.h"

#include <salts/clock.h>
#include <salts/error_codes.h>

#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sys/event.h>

static _Thread_local bool trace_active;
static _Thread_local cnet_kqueue_trace_stats trace_stats;

int cnet_kqueue_trace_begin(void) {
  if (trace_active) return SALTS_EBUSY;
  memset(&trace_stats, 0, sizeof(trace_stats));
  trace_active = true;
  return SALTS_OK;
}

int cnet_kqueue_trace_take(cnet_kqueue_trace_stats *out) {
  if (out == NULL) return SALTS_EINVAL;
  if (!trace_active) return SALTS_EINVAL;
  trace_active = false;
  *out = trace_stats;
  return SALTS_OK;
}

static int trace_kevent(int fd, const struct kevent *changes, int change_count,
                        struct kevent *events, int event_count,
                        const struct timespec *timeout) {
  const bool measured = trace_active;
  const int incoming_errno = errno;
  uint64_t started = 0u;
  int result;
  int saved_errno;
  if (measured) {
    for (int index = 0; index < change_count; ++index) {
      ++trace_stats.change_items;
      if ((changes[index].flags & EV_ADD) != 0u) ++trace_stats.adds;
      if ((changes[index].flags & EV_DELETE) != 0u) ++trace_stats.deletes;
    }
    started = cmeta_hrtime();
  }
  errno = incoming_errno;
  /* dyld does not substitute a replacee referenced by its own interposer. */
  result = kevent(fd, changes, change_count, events, event_count, timeout);
  saved_errno = errno;
  if (measured) {
    const uint64_t elapsed = cmeta_hrtime() - started;
    cnet_kqueue_trace_calls *calls = event_count > 0 ? &trace_stats.waits : &trace_stats.changes;
    ++calls->calls;
    calls->elapsed_ns += elapsed;
    if (elapsed > calls->max_ns) calls->max_ns = elapsed;
    if (result < 0) ++calls->errors;
    if (result > 0) trace_stats.returned_events += (uint64_t)result;
  }
  errno = saved_errno;
  return result;
}

/* Mach-O interposition ABI: replacement/replacee pairs in __DATA,__interpose.
 * Typed pointers avoid function/object-pointer casts in this strict C11 target.
 * https://github.com/apple-oss-distributions/dyld/blob/main/include/mach-o/dyld-interposing.h */
typedef int (*cnet_kevent_fn)(int, const struct kevent *, int, struct kevent *, int,
                              const struct timespec *);
__attribute__((used, section("__DATA,__interpose")))
static const struct { cnet_kevent_fn replacement; cnet_kevent_fn replacee; }
    trace_interpose = {trace_kevent, kevent};
