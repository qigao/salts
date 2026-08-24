#ifndef CFLOW_TIMER_EVENT_H
#define CFLOW_TIMER_EVENT_H

#include <cflow/clock.h>
#include <cflow/machine_runtime.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t cflow_timer_event_id;

/** Result of Timer Event control-plane and admission operations. */
typedef enum cflow_timer_event_status {
    CFLOW_TIMER_EVENT_OK = 0,
    CFLOW_TIMER_EVENT_INVALID_ARGUMENT,
    CFLOW_TIMER_EVENT_TYPE_MISMATCH,
    CFLOW_TIMER_EVENT_FULL,
    CFLOW_TIMER_EVENT_CLOSED,
    CFLOW_TIMER_EVENT_NOT_FOUND,
    CFLOW_TIMER_EVENT_FIRE_WON,
    CFLOW_TIMER_EVENT_ALLOCATION_FAILED
} cflow_timer_event_status;

/** Result of one non-blocking ready-timer handoff attempt. */
typedef enum cflow_timer_event_fire_status {
    CFLOW_TIMER_EVENT_FIRE_NOT_READY = 0,
    CFLOW_TIMER_EVENT_FIRE_DELIVERED,
    CFLOW_TIMER_EVENT_FIRE_MAILBOX_REJECTED,
    CFLOW_TIMER_EVENT_FIRE_CLOSED,
    CFLOW_TIMER_EVENT_FIRE_BUSY,
    CFLOW_TIMER_EVENT_FIRE_INVALID_ARGUMENT
} cflow_timer_event_fire_status;

typedef struct cflow_timer_event_schedule_result {
    cflow_timer_event_status status;
    cflow_timer_event_id timer_id;
} cflow_timer_event_schedule_result;

typedef struct cflow_timer_event_fire_result {
    cflow_timer_event_fire_status status;
    cflow_timer_event_id timer_id;
    /** Meaningful only for DELIVERED or MAILBOX_REJECTED. */
    cflow_mailbox_status mailbox_status;
} cflow_timer_event_fire_result;

typedef struct cflow_timer_event_stats {
    size_t capacity;
    size_t pending;
    size_t in_flight;
    size_t peak_pending;
    size_t payload_stride;
    size_t reserved_payload_bytes;
    size_t reserved_bytes;
    uint64_t scheduled;
    uint64_t delivered;
    uint64_t cancelled;
    uint64_t cancelled_on_close;
    uint64_t mailbox_rejected;
    uint64_t mailbox_rejected_full;
    uint64_t mailbox_rejected_closed;
    uint64_t mailbox_rejected_cancelled;
    uint64_t mailbox_rejected_other;
    uint64_t rejected_full;
    uint64_t rejected_closed;
    bool closed;
} cflow_timer_event_stats;

/**
 * Opaque fixed-capacity Timer Event Queue. Zero initialization is required.
 *
 * Thread topology and lifetime contract:
 *
 * - schedule, cancel, statistics, and close may run concurrently;
 * - ready-timer handoff has one logical consumer; once a handoff is claimed,
 *   another handoff attempt returns `CFLOW_TIMER_EVENT_FIRE_BUSY`;
 * - the borrowed Clock, Machine instance, Machine schema, and CMeta descriptors
 *   must remain alive until destroy returns;
 * - Machine close or cancel may race with handoff and is reported as the exact
 *   terminal Mailbox rejection; Machine destroy may not race with the queue;
 * - Clock mutation must be externally synchronized with schedule-after and
 *   ready-timer handoff unless that Clock implementation documents stronger
 *   thread-safety; and
 * - destroy is control-plane only: every schedule, cancel, handoff, statistics,
 *   and close caller must already be quiescent.
 */
typedef struct cflow_timer_event_queue {
    void *impl;
} cflow_timer_event_queue;

typedef struct cflow_timer_event_queue_config {
    /** Borrowed monotonic Clock; must outlive the queue. */
    cflow_clock *clock;
    /** Borrowed initialized Machine instance; must outlive the queue. */
    cflow_machine_instance *machine;
    /** Maximum combined pending and firing Timer Events. */
    size_t capacity;
} cflow_timer_event_queue_config;

/**
 * Initialize fixed Timer Event and payload storage transactionally.
 *
 * The queue borrows the Clock, Machine instance, immutable Machine schema, and
 * canonical CMeta descriptors until destroy. No data-path allocation occurs
 * after this function succeeds.
 */
cflow_timer_event_status cflow_timer_event_queue_init(
    cflow_timer_event_queue *queue,
    const cflow_timer_event_queue_config *config);

/**
 * Copy one Machine-schema Event and schedule it at an absolute monotonic
 * deadline. Equal deadlines fire in successful schedule order.
 */
cflow_timer_event_schedule_result cflow_timer_event_queue_try_schedule_at(
    cflow_timer_event_queue *queue,
    cflow_deadline deadline,
    const cflow_event_view *event);

/**
 * Copy one Machine-schema Event and schedule it after a monotonic duration.
 * Deadline addition saturates at UINT64_MAX.
 */
cflow_timer_event_schedule_result cflow_timer_event_queue_try_schedule_after(
    cflow_timer_event_queue *queue,
    cflow_duration delay,
    const cflow_event_view *event);

/**
 * Cancel one pending Timer Event. `FIRE_WON` means its fire claim already
 * linearized and exactly one Mailbox handoff will finish.
 */
cflow_timer_event_status cflow_timer_event_queue_cancel(
    cflow_timer_event_queue *queue,
    cflow_timer_event_id timer_id);

/**
 * Hand off at most one ready Event to the target Machine Mailbox.
 *
 * Once this operation claims a ready timer, another handoff attempt returns
 * `CFLOW_TIMER_EVENT_FIRE_BUSY` until the Mailbox send finishes. Overlapping
 * observations that claim no timer may each return `NOT_READY`. Mailbox
 * rejection is a terminal Timer outcome and is returned exactly; this function
 * never retries and never executes a Machine transition inline.
 */
cflow_timer_event_fire_result cflow_timer_event_queue_run_one_ready(
    cflow_timer_event_queue *queue);

/** Read one mutex-consistent bounded-resource and terminal-outcome snapshot. */
bool cflow_timer_event_queue_get_stats(
    const cflow_timer_event_queue *queue,
    cflow_timer_event_stats *out);

/**
 * Stop admission, cancel pending timers, and wait for a claimed handoff.
 * This operation may run concurrently with data-plane calls and blocks until
 * an already claimed Mailbox send has recorded its terminal result. No Timer
 * Event can be emitted after this function returns.
 */
cflow_timer_event_status cflow_timer_event_queue_close(
    cflow_timer_event_queue *queue);

/**
 * Destroy a quiescent queue, release owned storage, and clear its handle.
 * No queue operation may be active, and the borrowed Clock and Machine instance
 * must still be alive. Destroying either borrowed object first is invalid.
 */
void cflow_timer_event_queue_destroy(cflow_timer_event_queue *queue);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_TIMER_EVENT_H */
