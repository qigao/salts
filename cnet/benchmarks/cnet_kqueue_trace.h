#ifndef CNET_KQUEUE_TRACE_H
#define CNET_KQUEUE_TRACE_H

#include <stdint.h>

typedef struct cnet_kqueue_trace_calls {
  uint64_t calls;
  uint64_t elapsed_ns;
  uint64_t max_ns;
  uint64_t errors;
} cnet_kqueue_trace_calls;

typedef struct cnet_kqueue_trace_stats {
  cnet_kqueue_trace_calls changes;
  cnet_kqueue_trace_calls waits;
  uint64_t adds;
  uint64_t deletes;
  uint64_t change_items;
  uint64_t returned_events;
} cnet_kqueue_trace_stats;

/* Only the calling thread is measured. Setup, warmup and the echo peer are
 * excluded. These probes belong to a separate executable, never the A/B run. */
__attribute__((visibility("default"))) int cnet_kqueue_trace_begin(void);
__attribute__((visibility("default"))) int cnet_kqueue_trace_take(cnet_kqueue_trace_stats *out);

#endif
