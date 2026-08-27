# CFlow Statechart Actor Facade Design

Date: 2026-08-27
Issue: #125

## Decision

Extend the existing `cflow_actor` lifecycle shell with a private runtime
strategy. The existing Machine initializer remains unchanged. A new
Statechart-specific initializer constructs a Statechart instance in the same
shell and projects its terminal-only Source into the existing identity Run.

This is an Actor lifecycle facade, not a second Statechart engine and not a
Graph-owned Statechart. The Statechart instance remains the sole semantic fact
source and its SerialExecutor remains the sole semantic owner.

## Public surface

- `cflow_statechart_actor_config` embeds one
  `cflow_statechart_instance_config`, the borrowed concurrent Scheduler, and
  sink callbacks.
- `cflow_statechart_actor_init()` publishes the existing `cflow_actor` handle
  and preserves the exact Statechart initialization rejection.
- `cflow_statechart_actor_get_stats()` returns Actor lifecycle/rejection
  counters together with a Statechart runtime snapshot.
- Existing start, stop, wait, error, producer-ref, send, and destroy operations
  work unchanged for either runtime kind.

The existing `cflow_actor_config`, `cflow_actor_init_result`, and
`cflow_actor_stats` remain Machine-specific so their layout and meaning do not
change. Calling the wrong runtime-specific statistics getter returns false and
does not modify the destination.

## Runtime strategy

The private strategy has only the operations the Actor shell owns:

1. attach one Source to the Run;
2. admit one copied Event;
3. close admission and settle controlled shutdown;
4. cancel after Actor failure;
5. destroy the owned runtime after Run detachment.

No public generic runtime union or `void *` callback table is introduced. The
strategy is selected once at initialization and never changes.

## Data-path protocol

| Concern | Contract |
| --- | --- |
| Data unit | One typed `cflow_event_view`; acceptance copies its trivial payload. |
| Fact source | The owned Statechart instance and its published configuration/state. |
| Producers | MPMC calls through independently retained `cflow_actor_ref` handles. |
| Semantic owner | The Statechart's borrowed non-manual SerialExecutor. |
| Run dispatch | The Actor's borrowed concurrent Scheduler. |
| Capacity | `external_event_capacity` is the hard external Mailbox bound. |
| Backpressure | `FULL` is returned exactly; there is no retry, growth, overwrite, drop, or fallback. |
| Observation | The terminal Source emits no values; only done/error reaches Actor callbacks. |

The Actor owns the Statechart instance, terminal Source after attachment,
identity Graph, Run, first error copy, and lifecycle shell. It borrows the
Statechart declaration, Statechart Executor, optional Clock, Scheduler,
descriptors, bindings, and callback contexts until Actor destruction returns.

## Lifecycle and terminal semantics

- `START -> RUNNING` occurs only after Source attachment and Run open.
- A root FINAL reached while `RUNNING` is normal Statechart completion:
  `RUNNING -> STOPPED`, followed by exactly one `on_done` callback.
- Machine natural Run completion while `RUNNING` remains the existing failure;
  this policy is runtime-specific and preserves compatibility.
- `request_stop` closes admission first. `START` stops directly; `RUNNING`
  enters `STOPPING` until the terminal Source reports done.
- Runtime or Run error stores the first Actor-owned error, changes the Actor to
  `FAILED`, cancels the runtime, settles the terminal boundary, and invokes
  `on_error` outside the Actor gate.
- Owner destruction first marks retained producer refs stale, then closes the
  runtime, closes the Run (detaching the Source), destroys the runtime and
  Graph, and finally releases the root shell reference.

Destroy remains an owner-side control-plane operation. It is forbidden from
Statechart callbacks, Actor callbacks, Scheduler callbacks, or concurrently
with wait/admission lifecycle control, matching the existing Actor contract.

## Error mapping

Statechart initialization failures are preserved in
`cflow_statechart_actor_init_result.statechart_status` and reported as
`CFLOW_ACTOR_STATECHART_REJECTED`. Event admission maps the common Mailbox
statuses identically for Machine and Statechart runtimes. A closed or cancelled
Mailbox observed while Actor state is still `RUNNING` becomes
`CFLOW_ACTOR_SEND_FAILED`; lifecycle checks continue to take precedence.

## Compatibility and migration

This change is additive. Existing Machine Actor source, enum values, structure
layouts, callbacks, and behavior remain unchanged. Code that wants Statechart
Actor lifecycle changes only its initializer/config/stats types; its producer
and lifecycle calls are shared.

The facade deliberately does not add supervision, restart, remoting,
persistence, SCXML `send`/`invoke`, mailbox resizing, or Graph-visible Statechart
values. Those remain separate issue scopes.

## Verification

Tests must cover:

1. exact invalid/scheduler/runtime initialization rejection;
2. pre-start, accepted, type mismatch, full, stopped, failed, and stale sends;
3. root FINAL as normal completion with no value callback;
4. explicit stop and queued Event cancellation;
5. stable Statechart runtime failure propagation;
6. correct runtime-specific statistics getter rejection;
7. retained-ref safety across owner destruction;
8. existing Machine Actor regression, C++ header compilation, ASan, and
   installed-package consumers.
