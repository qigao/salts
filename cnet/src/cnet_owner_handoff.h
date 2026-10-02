#ifndef CNET_OWNER_HANDOFF_H
#define CNET_OWNER_HANDOFF_H

#if !defined(CNET_INTERNAL_MULTI_OWNER_POC)
#error "cnet_owner_handoff is private to the CNet multi-owner POC"
#endif

#include "cnet_session.h"
#include "cnet_write_queue.h"

#include <cnet/cnet.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct cnet_owner_handoff {
  void *impl;
} cnet_owner_handoff;

typedef enum cnet_owner_handoff_kind {
  CNET_OWNER_HANDOFF_NONE = 0,
  CNET_OWNER_HANDOFF_SEND_BUFFER,
  CNET_OWNER_HANDOFF_SEND_SLICE,
  CNET_OWNER_HANDOFF_RECEIVE
} cnet_owner_handoff_kind;

typedef struct cnet_owner_handoff_config {
  size_t capacity;
  size_t max_payload_bytes;
} cnet_owner_handoff_config;

typedef struct cnet_owner_handoff_view {
  cnet_owner_handoff_kind kind;
  cnet_session_handle connection;
  mem_buffer_t *backing;
  size_t offset;
  size_t size;
  size_t demand;
  bool close_after_send;
  uint64_t _sequence;
} cnet_owner_handoff_view;

typedef struct cnet_owner_handoff_stats {
  size_t live;
  size_t peak;
  uint64_t accepted;
  uint64_t rejected_full;
  uint64_t rejected_closed;
  bool admission_open;
} cnet_owner_handoff_stats;

/** Initializes one bounded MPSC descriptor queue with exactly one owner consumer. */
int cnet_owner_handoff_init(cnet_owner_handoff *handoff,
                            const cnet_owner_handoff_config *config);

/**
 * Retains one immutable buffer only after validating the descriptor contract.
 * Queue-full rejection leaves caller ownership unchanged.
 */
int cnet_owner_handoff_publish_buffer(cnet_owner_handoff *handoff,
                                      cnet_session_handle connection,
                                      mem_buffer_t *buffer,
                                      bool close_after_send);

/**
 * Retains the canonical slice backing and copies only offset/length metadata.
 * Payload bytes are never copied.
 */
int cnet_owner_handoff_publish_slice(cnet_owner_handoff *handoff,
                                     cnet_session_handle connection,
                                     const mem_slice_t *slice,
                                     bool close_after_send);

/** Publishes copied receive-demand metadata; no payload ownership is involved. */
int cnet_owner_handoff_publish_receive(cnet_owner_handoff *handoff,
                                       cnet_session_handle connection,
                                       size_t demand);

/** Single-owner nonblocking take. Empty-open returns SALTS_ETIMEDOUT. */
int cnet_owner_handoff_take(cnet_owner_handoff *handoff,
                            cnet_owner_handoff_view *out_view);

/**
 * Releases the queue-owned retain and returns the descriptor slot.
 * Exactly one taken view may be outstanding.
 */
int cnet_owner_handoff_release(cnet_owner_handoff *handoff,
                               cnet_owner_handoff_view *view);

/**
 * Owner-only ownership transfer into the existing CNet write FIFO.
 *
 * On success the write FIFO has retained the same backing and the handoff view
 * is released. On write-FIFO rejection the handoff view and its retain remain
 * live so the owner may retry without copying or re-admitting from the producer.
 */
int cnet_owner_handoff_transfer_write(cnet_owner_handoff *handoff,
                                      cnet_owner_handoff_view *view,
                                      cnet_write_queue *writes,
                                      cnet_write_handle *out_handle);

bool cnet_owner_handoff_get_stats(const cnet_owner_handoff *handoff,
                                  cnet_owner_handoff_stats *out_stats);

/**
 * Stops producer admission. If a publisher is currently inside admission this
 * returns SALTS_EBUSY after closing admission; retry to complete close.
 */
int cnet_owner_handoff_close(cnet_owner_handoff *handoff);

/** Requires completed close, zero live descriptors, and no borrowed view. */
int cnet_owner_handoff_destroy(cnet_owner_handoff *handoff);

#endif /* CNET_OWNER_HANDOFF_H */
