# CFlow Hierarchical Machine and Temporal Source Design

**Issue:** qigao/salts#69
**Date:** 2026-08-24

## Scope and compatibility

This change is additive. Existing flat `cflow_machine`, `cflow_machine_instance`,
`cflow_timer_event_queue`, Graph, Stream, and Source APIs retain their behavior.
Hierarchy is a control-plane declaration that is normalized into the existing
immutable flat Machine IR. Temporal behavior is provided by Source adapters so
waiting remains in the existing scheduler/runtime protocol and downstream
demand remains value demand.

Version one deliberately has no shallow or deep history. A transition targeting
a composite state always follows that state's declared initial-child chain.
All hierarchy nodes use one state value descriptor so inherited guards and
actions have the same typed state contract at every nesting depth.

## Hierarchical Machine declaration

`cflow/machine_hierarchy.h` adds:

- `cflow_machine_hierarchy_state`, containing `id`, `parent`,
  `initial_child`, `value_type`, and `kind`;
- `cflow_machine_hierarchy_definition`, containing hierarchy states plus the
  existing Event, guard, action, and transition declarations;
- an opaque owning `cflow_machine_hierarchy` whose immutable flat Machine is
  exposed as a borrowed view;
- borrowed route queries aligned with normalized flat transition indices.

Validation is fail-fast and atomic. A valid hierarchy has exactly one root,
unique nonzero IDs, no parent cycles, an ACTIVE composite's initial child is a
direct child, every composite has an initial child, every leaf has none, every
terminal state is a leaf, and all state descriptors are equal. The initial
state may be composite and resolves through initial children to a leaf.

Transitions may be declared on leaf or composite nodes. For every reachable
source leaf and Event, normalization collects transitions from that leaf and
then each ancestor. Deeper declarations always precede ancestor declarations;
within one declaration node, lower numeric priority wins. The resulting rows
receive dense priorities and are passed to `cflow_machine_build`, which remains
the executable and validated IR.

Targets resolve through initial children. A route records the semantic state
boundary crossed by a normalized transition:

- exit nodes: source leaf upward, excluding the least common ancestor;
- entry nodes: the child below the least common ancestor downward to target
  leaf.

The route is immutable metadata. It does not introduce entry/exit callbacks,
reentrancy, or a second mutable current-state value.

## Hierarchical runtime and scoped Timer Events

`cflow_machine_hierarchy_instance` owns exactly one existing Machine instance
and one existing fixed-capacity Timer Event queue. Its configuration borrows the
immutable hierarchy, monotonic Clock, serial executor, bindings, and callback
data. State queries delegate to the inner Machine instance.

An internal hook brackets the flat state commit after guard/action success and
receives the selected normalized transition index. The wrapper gate makes the
commit plus exit-scope cancellation atomically visible to its Timer data plane.
The same private hook closes all scopes on runtime failure or non-transition
termination. It cannot replace state, emit observations, or call user code.

Timer Event slots gain an internal scope state ID. Existing public scheduling
uses scope zero and is unaffected. Hierarchy scheduling requires a valid node
scope. On exit, PENDING timers in matching scopes are canceled. FIRING timers
retain the existing fire-wins contract. Equal deadlines retain FIFO order.

The wrapper forwards admission, Source attachment, close, cancel, state copy,
statistics, and error queries. Closing rejects new Events and timers, cancels
pending scoped timers, and preserves an already in-flight Machine commit.
Destroy remains a quiescent control-plane operation.

## Temporal Source adapters

`cflow/temporal.h` adds move-style constructors:

```c
bool cflow_source_delay(cflow_source *out, cflow_source *inner,
                        cflow_duration delay);
bool cflow_source_debounce(cflow_source *out, cflow_source *inner,
                           cflow_duration quiet_period);
bool cflow_source_timeout(cflow_source *out, cflow_source *inner,
                          cflow_duration timeout);
```

Success moves and clears `inner`; failure leaves it owned by the caller. Each
adapter allocates fixed control state. Delay retains one typed value slot;
debounce uses one retained slot plus one scratch slot so it can poll a
spuriously-woken upstream without discarding the retained value; timeout needs
no value slot. It accepts both trivial values and managed values with
COPY/MOVE/DESTROY traits. No resume-path allocation or unbounded queue exists.

- `delay`: retain one upstream value, arm a monotonic deadline, and emit only
  when the deadline wins. Upstream is not polled for another value meanwhile.
- `debounce`: retain the latest value, replacing it when upstream produces
  again before the quiet deadline. A quiet deadline emits the retained value.
  Upstream completion emits a retained final value immediately and then
  completes; completion with no retained value completes directly.
- `timeout`: arm a deadline only while upstream is waiting. On resume after
  both upstream and deadline readiness have accumulated, observe upstream
  first and report `"temporal source timed out"` only if it still returns
  WAIT. A produced value restarts the rule for the next upstream wait.

Zero duration is valid and means a deadline at the scheduler's current
monotonic instant. Durations round up to the scheduler's millisecond tick, so
an adapter never fires early. Readiness callbacks only request another poll;
the first resume observation decides simultaneous readiness, with upstream
preferred over timeout. Spurious or coalesced wake delivery is supported.

Cancellation first marks the adapter canceled, then cancels the armed inner
waitable and scheduler task outside the lock. Inner readiness and terminal
wakers are forwarded directly, so no callback owned by the inner Source keeps
an adapter-state pointer. Destroy waits for an admitted timer callback, releases
a retained value, and destroys the moved inner Source exactly once. Source
terminal polling and terminal wakers remain observable without demand.

## Ownership and boundedness

| Object | Owns | Borrows | Bound |
|---|---|---|---|
| hierarchy | copied nodes, routes, flat Machine | CMeta descriptors | configured Machine limits |
| hierarchy instance | Machine instance, Timer Event queue | hierarchy, clock, executor, callbacks | mailbox + timer capacities |
| temporal Source | moved inner Source, up to two value slots, one timer task | scheduler only while resumed | one retained value plus debounce scratch |

Hierarchy build and temporal construction may allocate. Event admission,
transition selection, timer scheduling, and temporal resume paths use only
preallocated storage. Checked arithmetic rejects impossible capacities.

## Formal model and verification

Lean provides abstract helper models over already-normalized candidate and
route lists plus one-slot temporal states. The current proofs establish local
selection, route-order, retained-cardinality, deadline, replacement, and
observed-cause lemmas. They do not verify the C normalizer, scheduler,
callback lifetime, or end-to-end runtime implementation.

C tests cover invalid hierarchies, nested transitions, leaf/parent conflicts,
route order, composite targets, terminal/error propagation, scoped timer exit
cancellation, equal deadlines, demand, managed values, cancellation, failure,
and close. A Release TinyTest benchmark compares equivalent flat and normalized
hierarchical Machine execution and reports build cost separately from steady
state execution.
