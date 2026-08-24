# IO Actor and Executor Lean Protocol

## Scope

This phase defines and proves the platform-neutral safety contract shared by
Reactor and Proactor implementations. It does not model epoll, kqueue, IOCP,
io_uring, native handles, or C ABI conformance yet.

## Architecture

The model has three independent state machines:

1. `BoundedMpsc` is a bounded FIFO command mailbox with many logical
   publishers, one logical consumer, explicit `full`, and graceful close.
2. `Executor` owns accepted tasks and moves each task through queued, running,
   and completed states. Queue capacity and execution parallelism are separate
   limits.
3. `Actor` is the single owner of I/O request state. Concurrent callers publish
   submit and cancel commands; backend completion and dispatch are actor-owned
   transitions.

`disruptor_t` is an implementation candidate for `BoundedMpsc`, not part of the
public semantic model. A later refinement must connect its claim/publish and
worker release sequences to the abstract mailbox transitions.

## Data and ownership protocol

- A command is a fixed-size value containing a command kind and request ID.
- A request slot is the fact source for operation data, completion result, and
  buffer lease.
- Successful submit transfers the lease to the Actor. Failed submit leaves the
  caller's ownership unchanged.
- Every non-free request occupies exactly one bounded request slot. That slot
  is also the reserved completion credit.
- A request slot is released only by completion acknowledgement.
- Executor tasks carry request IDs, not borrowed pointers into queue entries.

## Concurrency topology

- Command path: logical MPSC, single Actor consumer, FIFO observation.
- Actor state: single-threaded mutable owner.
- Executor admission: logical MPSC.
- Executor execution: manual/serial/concurrent capability with configured
  positive parallelism.
- Reactor/Proactor backend events are intentionally outside this phase.

## Bounded resources and backpressure

Let `N` be request capacity and `C` command capacity.

```text
activeRequests <= N
queuedCommands <= C
```

Submit succeeds only when both a request slot and command slot are available.
On either capacity failure the whole state is unchanged. Because every
accepted request retains its request slot through acknowledgement, terminal
completion never needs to acquire a new capacity credit.

Executor queue capacity applies only to queued tasks. Running task count is
bounded separately by `parallelism`. `tryPost` returns `full`; it never grows an
unbounded fallback queue.

## State machines

### Bounded MPSC

```text
Open --close--> Draining

Open + space --publish--> append exactly once
Open + full  --publish--> FULL, unchanged
Draining     --publish--> CLOSED, unchanged
queue nonempty --consume--> remove FIFO head
Draining + queue empty --consume--> CLOSED
```

### Executor

```text
Open --shutdown--> Draining --settle when idle--> Closed

tryPost: Open + space -> Queued
start:   Queued -> Running
finish:  Running -> Completed observation
```

Serial execution has parallelism one. Concurrent execution does not promise
finish order, but every task still starts and finishes at most once.

### IO Actor request

```text
Admitted -> Ready -> BackendPending(cancelRequested = false)
Admitted/Ready + cancel -> Completed(Cancelled)
BackendPending + cancel -> BackendPending(cancelRequested = true)
BackendPending + backend result -> Completed(result)
Completed + executor accepts -> DispatchQueued
DispatchQueued + acknowledge -> released
```

Cancel after native submission is a request, not proof that cancellation won.
The backend terminal result remains authoritative. Backend completion applied
to an already terminal request is stale and unobservable.

## Shutdown

Actor lifecycle is:

```text
Running -> Closing -> Quiescent
```

Closing rejects new submit commands, but consumes commands accepted before the
close linearization point. At close, `Admitted` and `Ready` requests become
cancelled completions; `BackendPending` requests retain their slot and set
`cancelRequested = true` until a native terminal result arrives. Quiescence
requires no queued commands and no active request slots. A client that does not
acknowledge a dispatched completion can therefore prevent quiescence; the
implementation must report busy/timeout rather than free the lease.

## Required safety properties

- Every transition preserves configured capacity bounds.
- Successful mailbox publication appends exactly once.
- Mailbox consumption is FIFO.
- Failed submit leaves request ownership unchanged.
- Accepted request IDs and active leases are unique.
- Every accepted request retains one completion credit until acknowledgement.
- Backend completion generates at most one terminal result.
- Executor full preserves the completed request for retry.
- Completion acknowledgement releases exactly one request slot.
- Closing rejects new admissions and preserves already accepted work.

## Deferred refinements

- `DisruptorRefinement`: one worker consumer, power-of-two capacity,
  claim-must-publish, external close gate, and quiescent destruction.
- `ReactorRefinement`: readiness and retry steps erase to silent transitions or
  one abstract completion.
- `ProactorRefinement`: native submit/completion/cancel acknowledgement erase to
  the same abstract request protocol.
- C conformance tests and generated manifests connecting implementation enums
  and transitions to this Lean model.
