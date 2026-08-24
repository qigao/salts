# CFlow Executor Runtime Protocol Design

## Decision

Refine the built-in Manual, Serial, and Worker executors against the proved
Lean Executor protocol without changing the existing `cflow_executor` vtable.
An additive `cflow_executor_control` interface exposes protocol-aware blocking
admission, waiting, shutdown policy, and accounting snapshots for built-in
executors. Existing methods remain compatibility wrappers and keep drain as
their shutdown policy.

The pool backend extends `turbo_threadpool` with additive status-returning wait
and policy-aware shutdown functions. A thread-local callback identity rejects
self-wait and a blocking self-post that would require the same saturated pool
to make progress.

Accepted work may additionally be submitted as a copied task descriptor with
`run`, optional `cancel`, optional `finalize`, and borrowed `user` fields. This
is an additive built-in API rather than a vtable extension: the legacy
`fn/user` entry points remain source- and ABI-compatible wrappers with no
terminal hooks.

The descriptor callback design is preferred over a task handle because the
required behavior is terminal notification during whole-executor shutdown. A
handle would also require an ID registry, per-task cancellation arbitration,
and concurrent handle reclamation without adding value for this scope.

## State and ownership

- The accepted task is the accounting unit.
- Manual owns a fixed task array. Pool executors own a bounded Disruptor worker
  queue through `turbo_threadpool`.
- Accepted work is partitioned into queued, running, completed, or cancelled.
  Public protocol snapshots derive accepted as the sum of those categories.
- Queue capacity bounds queued work only; running work is separate.
- The descriptor is copied during successful admission. `user` remains
  borrowed until terminal finalization completes.
- Every accepted descriptor follows exactly one path: `run -> finalize` or
  `cancel -> finalize`. Missing optional hooks are skipped. Rejected admission
  invokes no hook and leaves all payload responsibility with the caller.
- Completion/cancellation accounting advances only after `finalize` returns,
  so successful wait-idle guarantees that all accepted task hooks have
  finished.
- Executor lifecycle is `open`, `closing`, or `closed`. Shutdown atomically
  ends admission; closed is observable only after queued and running reach zero.

Concurrent pool counters are atomic implementation observations. Their abstract
linearization points are successful publication, worker claim, callback return,
and queued cancellation. Callers must use `wait_idle_status` before relying on
a terminal conservation snapshot; an in-flight diagnostic snapshot is not a
transactional cross-counter database snapshot.

## Operations and error semantics

- `try_post` remains non-blocking and returns accepted, full, or closed.
- Control `post` returns accepted, full, closed, invalid argument, or
  would-block. External callers may wait for bounded pool capacity. A callback
  on the same pool returns would-block instead of waiting when capacity is full.
- Control `wait_idle` returns idle, pending, invalid argument, or would-block.
  Calls from the same executor callback always return would-block.
- Drain shutdown rejects new work and runs all accepted work.
- Cancel-pending shutdown rejects new work, prevents queued callbacks from
  starting, invokes their cancellation/finalization hooks, and lets callbacks
  already running finish through normal finalization.
- Repeated shutdown is idempotent only for the same selected policy. A request
  to change policy after closing begins fails fast without changing state.
- Destroy joins pool workers. Remaining Manual descriptors execute cancel and
  finalize hooks; legacy queued callbacks have no hooks and are not run.

## Architecture and compatibility

`cflow_executor` remains the execution data-plane interface, so existing vtable
layout, constructors, source behavior, and ABI stay unchanged. The optional
`cflow_executor_control` view is an Interface Segregation/Adapter boundary over
the same backend state; custom executor implementations that do not implement
the built-in protocol simply fail the view conversion.

`turbo_threadpool_submit` gains a documented `TURBO_EBUSY` result only for the
previously deadlocking same-pool/full callback case. Existing external blocking
submission and drain shutdown behavior remain unchanged. New functions are
additive, and no serialized format, Graph IR, or generated metadata changes.

The ThreadPool queue entry and Manual fixed-array entry each grow from two
pointers to four pointers. Capacity continues to be measured in task slots, so
configured backpressure and public capacity values do not change; resident
task metadata increases by two pointers per physical slot. No per-submission
allocation is introduced.

Migration is opt-in: owners that require terminal cleanup use the descriptor
entry points; existing callers remain unchanged. Rolling back removes the
additive descriptor types/functions and restores the two-pointer internal queue
entry. No persisted state or task format requires migration.

## Concurrency and shutdown protocol

- Topology is MPMC admission and worker-pool consumption; Serial is the same
  backend with exactly one worker. Manual remains single-threaded.
- No lock is held while invoking a task callback.
- Successful descriptor admission has exactly one terminal path and exactly
  one finalizer invocation. All run/cancel/finalize hooks execute in the same
  executor callback context and therefore inherit self-blocking restrictions.
- Shutdown first closes admission, then wakes task/space waiters. Cancel mode
  drains queue slots as cancelled entries; drain mode executes them.
- Pool wait completes when queued plus running reaches zero. Callback-context
  waits do not enter the condition wait.
- No unconditional fairness or task-termination guarantee is claimed.

## Verification

TinyTest cases must demonstrate the previously deadlocking callback operations
return would-block, drain executes every accepted callback, cancel-pending never
starts queued callbacks, terminal accounting is conserved, repeated shutdown
policy is stable, descriptor hooks have exact-once ordering on execution and
cancellation, rejection invokes no hook, and legacy executor behavior remains
compatible. Run focused ThreadPool and CFlow execution tests before the
adjacent CFlow suite and full Windows Release preset.

## Rollback

Remove the additive descriptor types/functions and control interface, restore
the two-pointer internal task entries and legacy wrappers, and remove their
tests. Existing `cflow_executor` vtable ABI and persisted data require no
migration.
