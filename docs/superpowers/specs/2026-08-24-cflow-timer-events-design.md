# CFlow Monotonic Timer Events Design

## Context

Issue #65 connects the internal monotonic TimerQueue from Execution Model v2 to
the typed Event/Mailbox and Machine runtime delivered by issues #62 and #64.
The existing scheduler stores untyped `fn/user` tasks. The existing Machine
runtime accepts typed Events only through its bounded Mailbox. Timer Events must
join those two ownership domains without creating another executor, mailbox, or
state-transition path.

## Scope and compatibility

This change is additive. It adds `cflow/timer_event.h` and one opaque
`cflow_timer_event_queue`. Existing Clock, TimerQueue, scheduler, Mailbox,
Machine, Source, and Run behavior remains unchanged.

The queue is a typed readiness adapter, not a scheduler. It reads a borrowed
CFlow Clock, orders deadlines with the existing internal TimerQueue, and sends
ready Events through `cflow_machine_instance_try_send`. It never invokes guard
or action callbacks and never executes a Machine transition inline.

## Public surface

The public module provides:

- an opaque, zero-initialized `cflow_timer_event_queue`;
- a config borrowing one `cflow_clock` and one initialized
  `cflow_machine_instance`, plus a fixed timer capacity;
- `try_schedule_at` and `try_schedule_after` operations returning an exact
  Timer admission status and non-zero timer ID;
- `cancel`, `run_one_ready`, `close`, statistics, and quiescent destroy;
- a fire result that distinguishes `NOT_READY`, successful delivery, and a
  terminal Mailbox rejection while preserving the exact `cflow_mailbox_status`.

The queue accepts only Event IDs and CMeta payload types declared by its target
Machine. Validation occurs before timer admission. This keeps payload size and
descriptor ownership tied to the Machine schema and prevents an arbitrary type
from expanding the retained byte budget.

## State and ownership protocol

Each slot is exactly one of:

```text
FREE -> PENDING -> FIRING -> FREE
                  |
PENDING -> CANCELLED/CLOSED -> FREE
```

`PENDING -> FIRING` is the fire linearization point. Before it, cancel wins and
the Event cannot be sent. After it, cancel returns `FIRE_WON`; the single
consumer performs exactly one Mailbox send and records either delivery or the
exact terminal Mailbox rejection. There is no retry.

The queue owns:

- the copied timer slot rows;
- one preallocated payload region sized for the target Machine's largest Event;
- TimerQueue ordering metadata and accounting counters;
- the mutex and condition variable protecting control state.

The queue borrows until destroy:

- the Clock and its state;
- the Machine instance;
- the immutable Machine and canonical CMeta descriptors already borrowed by
  that instance.

The caller owns the input `cflow_event_view` and payload for the duration of a
schedule call. A successful schedule copies the canonical payload bytes into
queue-owned storage. The Machine Mailbox makes a second independent copy when
fire wins. Cancel, close, terminal Mailbox rejection, and destroy release the
Timer Event slot; all supported payloads are trivial, so release needs no user
destructor.

## Concurrency and lifecycle

Schedule, cancel, close, and statistics are thread-safe. `run_one_ready` has one
logical handoff consumer: after one call claims a ready timer, another handoff
attempt returns `BUSY` until the Mailbox send finishes. Calls that overlap only
while observing no ready timer may each return `NOT_READY`. The mutex protects
slot state, TimerQueue mutation, close state, and counters. No Machine or Clock
operation and no external callback runs while holding that mutex.

Close stops admission, cancels every pending timer, and waits for any firing
handoff to finish. Therefore no Timer Event is emitted after close returns.
Repeated close is idempotent. Destroy is a quiescent control-plane operation:
all queue callers have stopped, and the Clock and Machine instance remain alive
until destroy returns. Machine close/cancel may race with a fire and becomes the
exact Mailbox result; Machine destroy may not race with any queue operation.
Because VirtualClock mutation is not internally synchronized, callers must also
serialize Clock advance with schedule-after and ready observation unless a
different Clock implementation documents a stronger contract.

The Machine instance itself may reject delivery because its Mailbox is full,
closed, cancelled, or otherwise invalid. That result is a terminal fire outcome
and is never converted into Timer capacity failure, retry, or inline delivery.

## Ordering and time

Deadlines are `cflow_deadline`; relative delays are `cflow_duration`. Relative
schedule computes `cflow_deadline_after(cflow_clock_now(clock), delay)`, which
saturates at `UINT64_MAX`. Realtime timestamps never enter the adapter.

Ready means `deadline.ns <= clock.now().ns`. TimerQueue compares deadline first
and monotonically increasing insertion order second, so equal deadlines fire in
successful schedule order. Failed admission does not consume ordering state.

## Bounded accounting

Initialization computes, with checked arithmetic:

```text
payload_stride = align_up(max_machine_event_payload, CMETA_CAPTURE_ALIGN)
slot_bytes = capacity * sizeof(timer_event_slot)
payload_bytes = capacity * payload_stride
reserved_bytes = slot_bytes + payload_bytes + TimerQueue item bytes
```

No data-path allocation occurs after successful initialization. At every
mutex-consistent snapshot:

```text
scheduled = pending + in_flight + delivered + cancelled + mailbox_rejected
pending + in_flight <= capacity
```

Timer admission failures (`FULL`, `CLOSED`, invalid/type mismatch) and Mailbox
handoff failures are counted independently.

## Error semantics

- Invalid queue/config/Event/type inputs fail before mutation.
- Timer capacity exhaustion returns `FULL`; no payload is retained.
- Cancel of a pending ID returns `OK`; an in-flight ID returns `FIRE_WON`; an
  unknown terminal ID returns `NOT_FOUND`.
- `run_one_ready` returns `NOT_READY` without mutation when no deadline is due.
- A due timer always reaches one terminal outcome: delivered, cancelled before
  claim, cancelled by close before claim, or exact Mailbox rejection.
- Allocation and arithmetic failures occur only during initialization and do
  not publish a partial handle.

## Verification

TinyTest uses only VirtualClock for semantic timing:

- exact deadline boundary and saturating relative deadline;
- equal-deadline FIFO;
- TimerQueue capacity saturation and reuse;
- cancel before fire;
- deterministic cancel during a claimed handoff;
- close/shutdown with pending and in-flight work;
- full Mailbox results and the Machine close/cancel terminal rejection;
- repeated run with no duplicate Event;
- accounting identity, fixed storage, and repeated test execution.

Machine runtime tests verify that delivered Timer Events use the same serialized
transition path as directly sent Events. Public C and C++ header tests cover the
new API. Native Release and sanitizer tests provide executable conformance.

Lean adds a pure Timer Event state machine with one `active` fact source for
pending and claimed slots. It proves combined capacity, non-zero and unique
timer IDs, unique schedule order, next-ID/order bounds, preservation across all
five state operations, and arbitrary-list deadline/order minimum selection. Its
composed relation connects one successful commit to reception of that same
Event and the Machine runtime step driven by it, proving append-once,
receive-once, and the resulting observation suffix together. The proof does not
claim arbitrary C memory-model, OS scheduling, or fairness verification.
