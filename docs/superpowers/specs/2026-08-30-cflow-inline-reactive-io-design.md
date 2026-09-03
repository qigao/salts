# CFlow Inline Reactive I/O Design

## Background

The NativeIO adapter benchmark currently drives a two-operation Pipe exchange
through a queued test Scheduler and calls the I/O Publisher owner before and
after every completion batch. Windows measurements attribute about 0.69 us per
1 KiB exchange to Subscription scheduling and about 0.85 us to owner driving,
while the direct Pipe exchange is about 1.9 us. The payload is not copied on
these control edges; the dominant difference is repeated lock, queue, callback,
and driver admission work.

The public Scheduler contract already permits an implementation to execute a
zero-delay task before admission returns. CFlow also already coalesces I/O
Publisher drive credits, but currently invokes the external drive callback even
while the owner is active, so a synchronous callback can only observe
`SALTS_EBUSY`.

## Decision

### Inline Scheduler

Add `cflow_scheduler_inline_init()`. It is an owning, single-thread Scheduler
with these rules:

- only zero-delay work is accepted;
- accepted work runs exactly once before admission returns;
- no task remains pending, so cancellation always returns false and drive
  methods report no queued work;
- shutdown closes admission; destroy restores the handle to zero state;
- callbacks may post more inline work, but callers must serialize access and
  must not destroy the Scheduler from one of its callbacks.

This is an explicit execution-policy choice. A Subscription using it executes
prepare, operator, and Subscriber callbacks on the thread that requests or
wakes the Subscription. It provides no delayed scheduling and no thread hop.

### Caller-driven Scheduler

Add `cflow_scheduler_manual_init[_with_capacity]()` for bounded zero-delay work
that must preserve an external batch boundary. Admission stores a copied task
descriptor in the existing Manual Executor; the owner thread explicitly drains
it. It has no clock or timer queue, rejects delayed work, never grows, and does
not support removal by task ID. Shutdown closes admission while allowing an
explicit drain; destroy cancels any remaining descriptor according to the
Executor lifecycle contract.

### Owner drive coalescing

The I/O Publisher state remains the single source of truth for `driver_active`,
`drive_pending`, and `drive_generation`. When an Actor edge arrives:

- if no owner driver is active, retain and invoke the configured drive callback;
- if a driver is active, record only the pending credit;
- the active owner consumes that credit before leaving, or releases the driver
  and invokes one callback when its step budget is exhausted.

This removes a callback whose only legal result is `SALTS_EBUSY` without losing
an edge. The existing gate orders edge publication against driver release.

### Actor transition selection

The Actor driver selects exactly one state-machine transition while holding its
gate, then performs any NativeIO or Executor side effect after releasing the
gate. Transition priority remains command, cancellation, backend submission,
then completion dispatch. The previous implementation probed each transition
with a separate lock, so an idle or completion step repeatedly acquired the
same gate without changing state. A single locked selection preserves the Actor
request table as the only fact source and removes those empty probes without
weakening callback re-entry or shutdown protection.

### Low-latency NativeIO binding

Driving the owner synchronously for every Actor edge destroys NativeIO batch
boundaries: each submit and each completion becomes a separate nested owner
run. The supported NativeIO path therefore uses a caller-driven Publisher
(`drive == NULL`), a bounded caller-driven Scheduler, and
The later two-worker NativeIO design supersedes this serialized helper. One call drained ready Scheduler
work, drains the pending submit batch, observes one fixed completion batch,
drains completion delivery and acknowledgement, then drains newly ready
Scheduler work. The Scheduler must explicitly advertise the caller-driven,
zero-delay capability, and one step budget independently bounds every Scheduler
and owner phase. Keeping these phases flat avoids recursive Subscription pumps
inside an active owner. NativeIO remains the completion source; Actor request
state remains authoritative; the Publisher window remains bounded and applies
the same backpressure.

## Data-path protocol

| Concern | Contract |
|---|---|
| Data unit | One move-only `cflow_io_operation`, then one typed completion value |
| Fact source | Actor request slot and Publisher fixed window entry |
| Ownership | Prepare transfers the operation on accepted submission; Actor releases it exactly once after acknowledge/cancel |
| Lifetime | Typed completion storage is valid until Subscription delivery; borrowed callbacks/config live until owner close |
| Topology | Serialized caller-driven Scheduler; NativeIO completion may wake from the observe/callback thread |
| Ordering | Publisher result order remains authoritative completion-delivery order |
| Capacity | Existing fixed window, 1..`CFLOW_IO_PUBLISHER_MAX_WINDOW`; no new storage |
| Backpressure | Full window stops preparation; no retry, drop, fallback, or growth |
| Failure | Scheduler rejection terminates the Subscription; owner errors remain explicit; `SALTS_EBUSY` remains valid for genuinely concurrent external drivers |
| Shutdown | Stop demand, close Subscription, drain/cancel Actor, close owner, then destroy Scheduler and NativeIO |
| Observation | Existing Scheduler, Actor, Publisher-window, and benchmark stage counters |

## Compatibility and risks

- Existing queued and worker Schedulers are unchanged.
- Inline scheduling is opt-in and changes callback thread/timing, not values,
  demand, ordering, ownership, or error semantics.
- Synchronous drive callbacks remain suitable for serialized backends without
  an external batch boundary. NativeIO callers use the batch composition;
  close/destroy from any drive or Subscription callback remains forbidden.
- Recursive user posting can grow the C stack. The Scheduler does not hide this
  with an unbounded queue; users needing isolation select a queued/worker
  Scheduler.

## Verification

- Unit-test immediate execution, delayed-work rejection, shutdown, cancellation,
  descriptor finalization, and zero-state destroy.
- Unit-test that a drive edge raised during owner execution is consumed without
  a reentrant busy callback, including a one-step tail-budget case.
- Run I/O Publisher and Scheduler tests on Windows.
- Run the real NativeIO adapter Pipe/TCP benchmark and compare Direct, Actor,
  and Reactive stage timing using the same payload and operation counts.
- Run the adjacent CFlow test set; Linux verification remains required before
  merge because callback scheduling and mutex implementations are platform code.
