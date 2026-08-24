# CFlow Typed Event and Bounded Mailbox Design

**Status:** Approved for implementation under GitHub issue #62  
**Date:** 2026-08-24

## Context

CFlow already has homogeneous channels and kernel execution events, but the next
execution-model phases need a finite, heterogeneous domain-event vocabulary.
Those events must cross producer/consumer boundaries without unbounded growth,
ambiguous ownership, or lost wake-ups.

This design introduces a separate `cflow_event` module. It does not change the
existing `cflow_channel` API and does not reuse the kernel trace-label type named
`CFlow.Execution.Event`.

## Decision

Add a typed event schema and a fixed-capacity mailbox with these properties:

- a schema is a finite set of non-zero event identifiers, each mapped to one
  CMeta payload descriptor;
- schema rows are copied at mailbox initialization, while CMeta descriptors are
  borrowed and must remain valid until mailbox destruction;
- payload descriptors must be valid, non-empty, no more aligned than CMeta's
  ABI-safe capture storage, trivially copyable, and trivially destructible;
- successful send copies the payload into mailbox-owned, preallocated storage;
- failed send leaves the caller's payload untouched;
- receive copies the oldest accepted payload into caller-owned aligned storage;
- graceful close rejects later sends and permits already accepted events to
  drain; cancellation rejects later sends and discards accepted events;
- a single consumer may arm one `cflow_waitable`; the first transition from no
  pending work to observable work consumes that arm, so later sends coalesce
  until the consumer rearms;
- destruction is a quiescent control-plane operation and never invokes a stale
  waker.

The initial payload restriction is intentional. Calling arbitrary CMeta copy or
destroy callbacks while holding the mailbox lock would violate the repository's
lock-boundary rules, while moving callbacks outside the lock requires a larger
claim/commit/release protocol. Machine/runtime events are plain value records,
so trivial payloads cover the immediate execution-model use case without
publishing a partially safe ownership API.

## Public API shape

The module exposes opaque mailbox storage and value-oriented schema/event views:

```c
typedef uint64_t cflow_event_id;

typedef struct cflow_event_type {
    cflow_event_id id;
    const cmeta_type_desc *payload_type;
} cflow_event_type;

typedef struct cflow_event_view {
    cflow_event_id id;
    const cmeta_type_desc *payload_type;
    const void *payload;
} cflow_event_view;

typedef struct cflow_mailbox {
    void *impl;
} cflow_mailbox;
```

All operations return a `cflow_mailbox_status`. Distinct values cover success,
invalid arguments, type mismatch, full, empty, graceful closure, cancellation,
allocation failure, and an undersized output buffer.

`cflow_mailbox_try_receive` returns the canonical payload descriptor from the
mailbox schema and copies payload bytes into caller storage. On any failure, it
does not dequeue the event and clears only the metadata outputs; caller payload
storage remains unchanged.

## Data-path protocol

### Data unit and source of truth

One committed slot consists of a schema-row index plus `payload_stride` bytes.
The mailbox-owned ring metadata and payload region are the sole source of truth
for pending events. Statistics are derived under the same mutex.

### Ownership and lifetime

- Schema input array: borrowed for the duration of `init`; rows are copied.
- CMeta descriptors: borrowed until `destroy`.
- Send view and payload: borrowed only for the call.
- Accepted slot: owned by the mailbox until receive, cancellation, or destroy.
- Receive destination: owned by the caller before and after the call.
- Waitable interface: borrows the mailbox and is invalid after `destroy`.

### Topology and ordering

The supported topology is MPSC with exactly one consumer. A mutex serializes
admission commits, yielding global FIFO order by successful commit order.
`try_send` and `try_receive` are non-blocking with respect to capacity; they may
briefly wait for the mailbox mutex.

### Capacity and memory accounting

Let `C` be slot capacity, `S` the schema row count, and `P` the maximum payload
size rounded up to the maximum schema alignment. Initialization checks every
addition and multiplication before allocation.

```text
payload bytes  = C * P
slot metadata  = C * sizeof(slot)
schema bytes   = S * sizeof(cflow_event_type)
```

No allocation occurs after successful initialization. Capacity must be greater
than zero. Full mailboxes reject sends with `CFLOW_MAILBOX_FULL`; they never
overwrite, grow, spin, or silently drop events.

### State machine

```text
OPEN --close--> DRAINING --queue empty--> CLOSED observation
  |
  +--cancel--> CANCELLED
DRAINING --cancel--> CANCELLED
```

- `OPEN`: send and receive are permitted.
- `DRAINING`: sends return `CLOSED`; receives drain committed slots, then return
  `CLOSED`.
- `CANCELLED`: sends and receives return `CANCELLED`; queued events have been
  discarded and counted.

`destroy` requires all producers and the consumer to be quiescent. It silently
detaches any armed waker because invoking user code during destruction could
re-enter freed state.

### Wake protocol

The waitable has at most one armed waker. Arming an empty open mailbox stores the
waker. Arming a non-empty or terminal mailbox schedules an immediate wake after
unlocking. A successful send, close, or cancel takes and clears the stored waker
under lock, then invokes it after unlock. Multiple sends before rearm therefore
produce at most one wake.

## Error semantics

- Invalid schema, duplicate/zero identifiers, unsupported CMeta traits,
  arithmetic overflow, null storage, or invalid alignment fail fast.
- `TYPE_MISMATCH` means an event identifier exists but the supplied descriptor
  is not semantically equal to its schema descriptor.
- `BUFFER_TOO_SMALL` preserves the head event for a later receive.
- Allocation failure is possible only during initialization.

## Formal model

Lean receives a distinct `CFlow.Mailbox` namespace with:

- typed events carrying a payload type and a value of that type;
- mailbox state (`capacity`, FIFO queue, and open/closed/cancelled terminal mode);
- pure send, receive, close, and cancel transitions;
- proofs that valid transitions preserve the capacity bound, successful sends
  append exactly once, receive observes FIFO order, close rejects new sends but
  preserves the queue, and cancel empties the queue.

The formal model proves functional and boundedness properties. It does not claim
wall-clock performance, mutex fairness, or a C memory-model refinement theorem.

## Compatibility and alternatives

- Existing channels remain source-compatible and behaviorally unchanged.
- Reusing `cflow_channel` was rejected because it fixes one homogeneous type and
  reports only Boolean admission results.
- A Disruptor/ring implementation was deferred because no profile yet shows the
  mutex as a bottleneck, and the first contract needs deterministic failure and
  shutdown semantics more than lock-free complexity.
- Non-trivial CMeta payloads require a future claim/commit/release design or an
  explicitly owned event envelope; this API rejects them rather than falling
  back to unsafe byte copying.

## Verification

Tests cover schema validation, heterogeneous FIFO order, full/empty status,
undersized buffers, close-and-drain, cancellation, statistics, wake coalescing,
MPSC admission, C++ header compatibility, and Lean invariants. Release-profile
targeted tests run first, followed by the complete CTest and Lean suites.
