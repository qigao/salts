# CFlow Statechart Controlled Exit Design

## Context

The Statechart runtime currently has two immediate control operations:

- `close` stops admission and preserves a microstep whose commit wins, but it
  does not execute actions for the active configuration;
- `cancel` stops admission and may discard an uncommitted microstep, and it
  likewise does not execute active-state exit actions.

SCXML cancellation needs a third operation. When an invoked session is
cancelled it must leave every active state in exit order, execute each exit
action, cancel nested invocations through those actions, and terminate without
normal completion. Implementing that behavior above CFlow would duplicate the
opaque active configuration and extended-state transaction, creating two fact
sources.

## Decision

Add the following asynchronous, additive API:

```c
void cflow_statechart_instance_request_exit(
    cflow_statechart_instance *instance);
```

The request closes external, adapter-internal, and timer admission immediately.
Pending semantic queues are cancelled. The owning SerialExecutor then performs
one controlled-exit transaction:

1. preserve a semantic microstep that already reaches commit;
2. copy the published extended state into the existing staging slot;
3. visit every published active real state in descendant-before-ancestor exit
   order, with reverse document order for orthogonal states;
4. execute each declared exit action and remove that state from the action's
   active-configuration view;
5. publish the staged extended state and an empty terminal configuration;
6. discard internally raised events, commit staged external effects, and settle
   the instance as cancelled.

No transition or entry action is synthesized. The existing `close` and hard
`cancel` operations retain their current behavior. A hard cancel may still win
over a pending controlled exit. A later close does not replace an already
accepted controlled-exit request, so instance destruction can wait for the
executor-owned exit transaction.

## State and execution protocol

| Property | Contract |
|---|---|
| Fact source | The published CFlow configuration and extended state remain the only mutable facts. |
| Owner | Only the configured SerialExecutor mutates semantic configuration or invokes exit actions. |
| Request topology | Any thread may request exit; the request is idempotent and protected by the instance mutex. |
| Ordering | A committing microstep publishes first; controlled exit is the next executor-owned semantic operation. |
| Admission | New external, adapter-internal, and timer work is rejected as soon as the request linearizes. |
| Capacity | No new queue or dynamic growth path is added; existing state, action, and effect staging storage is reused. |
| Effects | Exit effects commit exactly once after empty configuration publication; rollback or a competing terminal winner discards them exactly once. |
| Internal events | Events raised by exit actions are discarded because no further macrostep is admitted after interpreter exit. |
| Failure | Copy, action, or effect-staging failure rolls back the exit transaction and records the existing first terminal error. |
| Observation | Successful exit reports `closed`, `cancelled`, and `done`; configuration snapshots report zero active states. |

The request flag is distinct from the terminal outcome. This permits an
already-running microstep to commit without triggering the hard-cancel branch,
while the closed admission boundary prevents later work from overtaking the
exit transaction.

## Alternatives

- Change `cflow_statechart_instance_cancel()` to execute exits: rejected because
  existing callers rely on its hard-cancel and rollback semantics.
- Mirror the active configuration in TurboSCXML and call exit blocks directly:
  rejected because CFlow owns configuration, extended-state transactions,
  ordering, and effect staging.
- Compile a hidden transition into every SCXML document: rejected because it
  changes the user state graph, can create observable synthetic states, and
  conflates cancellation with a normal transition.

## Compatibility and migration

The CFlow change is an additive symbol. Existing close, cancel, terminal
adapter, Actor, snapshot, and event-admission callers do not change. TurboSCXML
will migrate only `scxml_session_cancel()` to the new operation; its public
signature remains unchanged.

Rollback is two independent PR reverts: TurboSCXML can return to hard cancel,
then the unused additive CFlow symbol can be removed in a later compatibility
window. No stored data or configuration migration is required.

## Verification

- A quiescent nested configuration exits descendant before ancestor, publishes
  an empty configuration, and commits staged effects after publication.
- A request made inside an executing transition preserves that transition's
  commit, discards its queued internal continuation, and then exits the newly
  published configuration.
- Exit-action failure rolls back state/configuration and terminates with the
  original error.
- Repeated request and close are idempotent; hard cancel remains able to win.
- Existing Statechart lifecycle, timer, terminal-adapter, Actor, and aggregate
  CFlow tests remain unchanged.
- TurboSCXML W3C test 250 observes both nested `onexit` handlers and no
  `done.invoke` completion.
