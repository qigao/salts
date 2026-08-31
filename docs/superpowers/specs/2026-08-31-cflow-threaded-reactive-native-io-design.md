# CFlow Threaded Reactive NativeIO Design

## Background

The former serialized composition drove the Subscription Scheduler, Publisher
owner, NativeIO observation, completion delivery, and acknowledgement on one
caller thread. That is not the Reactive execution model: a
Publisher/Subscriber boundary isolates I/O progress from typed Subscription
callbacks.

This change also completes the NativeIO public naming migration. Public types,
constants, and object operations use only `native_io_*` and `NATIVE_IO_*`; no
legacy compatibility surface remains.

## Decision

### Two execution roles

Reactive NativeIO uses two independent bounded worker roles:

1. A Publisher/NativeIO owner task runs on a dedicated one-worker
   `turbo_threadpool_t`. It alone submits, cancels, observes, correlates, and
   acknowledges NativeIO operations.
2. A Subscriber pump runs on a one-worker CFlow Worker Scheduler. It owns
   downstream demand, Graph operators, and Subscriber callbacks.

The Publisher pool accepts one long-lived owner task. A `drive` edge first
posts a coalesced NativeIO control wake and only then signals the owner
condition; it never queues another task behind a blocking observe. NativeIO
completion delivery wakes the Subscription Scheduler. Payload bytes are not
placed in either control path.

The adapter exposes `cflow_io_native_adapter_drive_publisher()` instead of the
serialized helper. One call runs pending Publisher work,
polls away a prior control wake or ready completion, runs pending Publisher
work, observes one NativeIO batch when a bridge is active, then runs completion
delivery and acknowledgement. It never drives a Subscription Scheduler.

### NativeIO owner transfer

NativeIO initialization and endpoint attachment are control-plane operations.
Before the first successful submit, a quiescent backend may move to its final
data-plane owner thread. From the first submit until close/drain/release/destroy,
all backend and adapter data-plane operations remain serialized on that fixed
owner. `native_io_backend_wake()` is the sole cross-thread operation: it is a
bounded, coalesced control signal and never mutates request state or publishes
a fake completion. There is no concurrent backend drive and no implicit worker
inside NativeIO.

## Data-path protocol

| Concern | Contract |
|---|---|
| Data unit | One move-only operation descriptor and one typed completion value |
| Fact source | NativeIO request slot for native progress; Actor request slot for command/acknowledge state; Subscription demand for downstream admission |
| Ownership | Submit copies the descriptor but borrows payload/address storage until terminal observe; Actor releases the operation token exactly once after terminal acknowledge/cancel |
| Lifetime | Payload views expire when the matching completion is observed; typed values remain live through the Subscriber callback only; callback/config contexts remain valid through owner close |
| Topology | One long-lived Publisher/NativeIO owner task and one Subscriber Scheduler worker; producer threads publish commands and call the coalesced wake, while only the owner accesses request state |
| Ordering | Command publish happens before NativeIO wake, which happens before owner-condition signal; the owner polls away that control edge before submitting the next batch. NativeIO lane FIFO and Publisher delivery order remain authoritative |
| Capacity | NativeIO endpoint/request/batch capacities, Publisher window, one coalesced wake bit/OS signal, the one-task Publisher pool, and Subscriber Scheduler queues are fixed at initialization |
| Backpressure | Full operation or Scheduler admission returns an explicit error. Wake is coalesced, never drops payload state, and never grows storage |
| Failure | NativeIO and owner errors remain first-class status values; a failed task admission is observable and prevents a success claim; no fallback Scheduler/backend is selected |
| Shutdown | Stop demand, close/cancel Subscription, drain native terminals and acknowledgements, wake and stop the long-lived owner task, then close Publisher owner and close/release/destroy NativeIO on that same pool worker; destroy Subscriber Scheduler after callbacks settle |
| Observation | NativeIO/adapter/Actor/Publisher/Scheduler/thread-pool counters plus test-recorded Publisher and Subscriber execution roles |

## Public API and compatibility

- Expose `cflow_io_native_adapter_drive_publisher()` without a Scheduler
  parameter; the serialized Scheduler-driving helper is removed.
- Add `native_io_backend_wake()` and `cflow_io_native_adapter_wake()` as the
  explicit coalesced cross-thread control edge.
- Rename every public NativeIO identifier to `native_io_*` or `NATIVE_IO_*`.
- This is an intentional source/ABI break. No aliases, wrappers, deprecated
  spellings, or fallback paths are added.
- NativeIO remains independent of CFlow and does not create threads.

## Verification

- Change the NativeIO C++ header test first and observe compilation failure on
  the new names.
- Add a real pipe Reactive test using a one-worker Publisher thread pool and a
  one-worker Subscriber Scheduler; record callback roles and assert they are
  distinct.
- Verify NativeIO payload bytes, terminal counts, operation release,
  acknowledgement, quiescence, and shutdown ordering.
- Change the NativeIO adapter benchmark Reactive mode to the same two-role
  topology so Direct remains the denominator and Reactive includes its real
  cross-thread scheduling cost.
- Run focused Windows Release tests and benchmark smoke verification, then
  scan for obsolete public spellings and run `git diff --check`.
