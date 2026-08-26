# CFlow execution lifecycle hardening design

## Context

CFlow already exposes terminal task descriptors for repository-owned Executors,
but Scheduler still stores only `fn/user`. Consequently shutdown can discard an
accepted delayed Run pump without releasing the Run task reference. Scheduler
and Run initializers also overwrite live owning handles, and parallel reduce
posts plain callbacks whose accepted-but-cancelled work never settles its join.

The affected state owners are:

- Scheduler owns delayed admission until handoff to its Executor.
- Executor owns ready work after successful handoff.
- Run owns `task_refs`; every successful pump admission must release one ref.
- Parallel-reduce frame owns `accepted/completed`; every accepted shard must
  increment `completed` exactly once, whether run or cancelled.

## Decision

Keep the public ABI unchanged. Add private CFlow helpers that let built-in
Schedulers store a copied `cflow_executor_task` descriptor. Public Scheduler
methods remain compatibility wrappers that construct a descriptor containing
only `run/user`.

For each accepted built-in Scheduler descriptor, exactly one terminal path is
required:

1. hand it to the ready Executor, which invokes `run` or `cancel`, then
   `finalize`; or
2. if cancellation/shutdown/handoff failure occurs while Scheduler still owns
   it, Scheduler invokes `cancel` and then `finalize` outside its mutex.

Run pump descriptors use cancellation to terminate the Run and release the
matching task reference. Parallel reduce uses descriptor cancellation to mark
the frame failed and increment `completed`. A private current-executor query
rejects parallel reduce invoked inside the same built-in Executor callback,
because its synchronous join cannot make progress there.

Owning initializers validate that destination handles are empty before any
allocation or mutation. They build temporary state and publish the handle only
after full success. `cflow_run_open_subgraph` likewise rejects a live Run
without changing it.

## Alternatives

- Add Scheduler descriptor methods to the public interface: strongest custom
  backend contract, but changes public ABI and every implementation. Rejected
  for this compatibility repair.
- Drain all delayed tasks during shutdown: changes shutdown latency and causes
  delayed work to execute after shutdown begins. Rejected.
- Release Run references by scanning Scheduler state from Run close: violates
  single ownership and cannot safely distinguish queued from dispatching work.
  Rejected.

## Failure and shutdown semantics

Admission rejection invokes no terminal callback. Successful admission invokes
exactly one of `run/cancel` and then optional `finalize`. Terminal callbacks are
never called under a Scheduler lock. Shutdown closes admission first, settles
Scheduler-owned delayed descriptors, then shuts down the ready Executor.

Custom Schedulers keep their existing public contract; private descriptor
admission applies only to the two repository-owned backends.

## Compatibility, migration, and rollback

No installed header layout or function signature changes. Existing plain
Scheduler callbacks observe the same run/cancel behavior because their
compatibility descriptors have no cancel/finalize callback. Invalid live-handle
reinitialization changes from destructive undefined use to an immediate `false`.

Rollback is a single PR revert. No data or configuration migration is needed.

## Verification

- Reinitialization tests prove failed second init/open preserves the first live
  owner and remains usable/closable.
- Scheduler tests prove explicit cancel and shutdown settle descriptors once.
- Runtime test proves shutdown followed by Run close returns and releases state.
- Parallel-reduce tests cover cancel-pending and same-single-worker invocation.
- Run focused tests, all `cflow_*` CTest targets, then Windows ASan CFlow tests.
