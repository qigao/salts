# CFlow Machine Resumable Runtime Design

**Status:** Approved for implementation under GitHub issue #64
**Date:** 2026-08-24

The concurrent cancel/commit boundary is refined by
`2026-08-24-cflow-actor-runtime-linearization-design.md`. That decision is
authoritative when cancellation overlaps the final transition commit.

## Context

Issue #63 delivered an immutable, transactionally validated Machine IR and a
Lean small-step semantics. Issue #62 delivered a bounded MPSC Mailbox. CFlow
already has Resumable/Source step kinds, downstream-value demand, waitables,
SerialExecutor, Graph execution, and synchronous/reentrant Run shutdown.

The missing layer is one runtime instance that binds guard/action callbacks to
one validated Machine, accepts typed Events without allowing producers to
mutate Machine state, and presents Machine progress through the existing
Resumable protocol. This layer must not create another scheduler, worker,
demand counter, or inline fallback.

## Decision

Add `cflow_machine_instance`, configured by
`cflow_machine_instance_config`, as the sole owner of mutable Machine execution
state. The instance:

- borrows one immutable `cflow_machine`;
- borrows one executor with `CMETA_EXEC_CAP_SERIAL` and without
  `CMETA_EXEC_CAP_MANUAL`;
- owns a fixed-capacity `cflow_mailbox` built from the Machine Event schema;
- copies the initial state value, binding rows, queued Event payloads, and the
  single prepared downstream value;
- borrows callback functions and callback user data until destroy;
- exposes one move-style Resumable or Source adapter at a time;
- preserves existing `cflow_run` ownership for Graph and scheduler.

External producers call only `cflow_machine_instance_try_send`. A successful
call copies the Event into the instance Mailbox. Producers never call guards,
actions, or state mutation code.

## Public API

The public header `cflow/machine_runtime.h` defines:

```c
typedef struct cflow_machine_instance { void *impl; } cflow_machine_instance;

typedef bool (*cflow_machine_guard_fn)(
    void *user, const void *state, const void *event,
    bool *out_enabled, const char **out_error);

typedef bool (*cflow_machine_action_fn)(
    void *user, const void *state, const void *event,
    void *out_target_state, void *out_observation,
    const char **out_error);

typedef struct cflow_machine_guard_binding {
    cflow_machine_guard_id id;
    cflow_machine_guard_fn fn;
    void *user;
} cflow_machine_guard_binding;

typedef struct cflow_machine_action_binding {
    cflow_machine_action_id id;
    cflow_machine_action_fn fn;
    void *user;
} cflow_machine_action_binding;
```

Initialization receives the borrowed Machine and SerialExecutor, initial state
bytes, the homogeneous downstream output type, exact guard/action binding
arrays, and Mailbox capacity. Bindings are copied and normalized by ID. Every
declared non-zero guard/action must have exactly one binding; unknown,
duplicate, missing, or NULL bindings fail transactionally.

The operations are:

```c
cflow_machine_runtime_status cflow_machine_instance_init(...);
cflow_mailbox_status cflow_machine_instance_try_send(...);
bool cflow_machine_instance_as_resumable(...);
bool cflow_machine_instance_as_source(...);
void cflow_machine_instance_close(...);
void cflow_machine_instance_cancel(...);
bool cflow_machine_instance_get_stats(...);
cflow_machine_state_id cflow_machine_instance_current_state(...);
const char *cflow_machine_instance_error(...);
void cflow_machine_instance_destroy(...);
```

`as_resumable` and `as_source` attach one adapter. Successful attachment moves
adapter ownership to its eventual consumer, while the instance remains owned
by its caller. Adapter cancel/destroy detaches and cancels execution; callers
destroy the instance only after the adapter consumer has closed and producers
are quiescent.

## Supported runtime fragment

The immutable Machine IR remains more general than the first runtime adapter.
Runtime initialization admits only descriptors that are valid, non-empty,
ABI-safe aligned, trivially copyable, and trivially destructible for:

- every state value;
- every Event payload (also enforced by Mailbox initialization);
- every action VALUE/EVENT observation;
- the configured downstream output type.

All `CFLOW_MACHINE_ACTION_VALUE` declarations must use the one configured
downstream output type. NONE and EVENT actions do not consume downstream
demand. This finite homogeneous-value fragment matches the existing
`cflow_resumable.output_type` contract without adding a variant transport or
changing Graph typing.

## Callback contract

Guard callbacks receive borrowed immutable state/Event views. A callback
returns `true` only when invocation succeeded and writes `out_enabled`. A
`false` return terminates the current Event with the first copied error string;
a missing error string produces a stable runtime diagnostic.

Action callbacks receive the same borrowed inputs and preallocated output
storage. On success they write the target state and, for VALUE/EVENT actions,
the observation bytes. NONE actions receive a NULL observation pointer. A
false result is a declared action failure only when the action has
`CMETA_EFFECT_MAY_FAIL`; otherwise it is reported as an action contract
violation. No target state is committed after callback failure or cancellation.

Callbacks run without the instance mutex and only from the borrowed
SerialExecutor. They may call `close` or `cancel` reentrantly. Callback user
data remains borrowed and must outlive instance destroy.

## State and execution protocol

The sole semantic owner is the SerialExecutor callback. The instance mutex
protects transport/control metadata and read-only snapshots; it never grants a
producer permission to mutate the current state ID/value.

One executor task performs at most `CFLOW_MACHINE_RUNTIME_QUANTUM` transitions:

1. receive one copied Event from Mailbox;
2. select the lowest-priority enabled transition for current state/Event;
3. invoke its guard and action bindings;
4. admit an EVENT observation back into the same Mailbox before state commit;
5. commit the target state exactly once;
6. publish at most one downstream VALUE, terminal DONE, or first ERROR;
7. continue over NONE/EVENT transitions until output, terminal, WAIT, or the
   quantum boundary.

If the quantum expires with runnable work, another task is admitted through
the same SerialExecutor. Executor FULL/CLOSED rejection becomes a terminal
runtime error and cancels queued Events. No transition executes inline on the
producer, caller, scheduler, or wake thread.

## Demand and Resumable mapping

The instance does not own a demand counter. `resume()` is called only by an
existing CFlow consumer that has downstream-value demand.

- prepared action VALUE -> `CFLOW_STEP_VALUE`;
- prepared action VALUE followed by terminal ->
  `CFLOW_STEP_VALUE_AND_DONE`;
- open instance with no prepared value -> `CFLOW_STEP_WAIT`;
- closed/drained, cancelled, or DONE state -> `CFLOW_STEP_DONE`;
- callback, contract, transition, executor, or ERROR-state failure ->
  `CFLOW_STEP_ERROR` with the first owned error message.

NONE/EVENT transitions may consume multiple input Events during one resume
cycle before one downstream VALUE, exactly as a filter may consume multiple
source items for one unit of downstream demand.

## Wait and wake protocol

The adapter waitable stores at most one downstream waker. The executor owns the
Mailbox waitable arm. An arriving Event consumes the Mailbox arm and requests a
SerialExecutor task. A prepared VALUE/DONE/ERROR consumes the downstream arm.

Arming after readiness invokes the waker immediately after unlocking. Cancel
clears pending arms and waits for any already-extracted downstream callback
unless cancel is reentrant from that callback. Instance storage is not freed
until producers, executor work, adapter use, and wake callbacks are quiescent.

## Close, cancel, and Event terminal accounting

Close and cancel are idempotent and first stop new admission.

- `close` lets an already executing callback finish and commits that in-flight
  Event, then cancels queued Events and publishes DONE (or
  VALUE_AND_DONE when that Event produced a VALUE).
- `cancel` lets an already entered callback return but discards its result,
  preserves the source state, cancels the in-flight Event and every queued
  Event, and publishes DONE without a value.

For an overlapping cancel, “discards” means cancellation linearizes before the
mutex-protected `begin_commit` decision. If commit linearizes first, that
transition commits exactly once and cancellation prevents subsequent Events.
Arbitrary external effects already performed by an action callback are not
rolled back.

Both paths detach the Mailbox waitable, reject later Events, and prevent stale
downstream callbacks. An ERROR transition or callback failure settles the
current Event as failed and cancels every queued Event.

Statistics expose accepted, completed, failed, cancelled, pending, in-flight,
emitted VALUE/Event counts, current state, and terminal flags. At quiescent
terminal state:

```text
accepted = completed + failed + cancelled
pending = 0
in_flight = 0
```

Mailbox counters remain the fact source for accepted/pending/queued-cancelled
data. Runtime counters add completed, failed, and in-flight cancellation.

## Ownership matrix

| Resource | Owner and lifetime |
|---|---|
| Machine IR | Caller-owned, borrowed through instance destroy |
| Machine state bytes | Instance-owned copy, mutated only by SerialExecutor |
| Mailbox | Instance-owned; destroyed after producer/executor quiescence |
| Queued Events | Mailbox-owned trivial copies until receive/cancel/destroy |
| SerialExecutor | Caller-owned, borrowed through instance destroy |
| Guard/action binding rows | Instance-owned copies |
| Callback user data | Caller-owned, borrowed through instance destroy |
| Prepared emitted VALUE | Instance-owned copy until one resume copies it out |
| Resumable/Source adapter | Consumer-owned attachment borrowing instance |
| Graph and scheduler | Borrowed by existing `cflow_run`; unchanged |
| Sink callbacks | Borrowed by existing `cflow_run`; unchanged |

## Formal refinement

Lean adds `CFlow.MachineRuntime` with the supported trivial homogeneous-value
fragment, runtime outcomes, mailbox-empty WAIT, and a projection from one
semantic Machine step to Resumable VALUE/VALUE_AND_DONE/DONE/ERROR. The theorem
`runtime_step_trace_refines_machine` proves that every admitted runtime
transition carries the same trace suffix as `Machine.SmallStep`; WAIT produces
no semantic trace or consumed Event. A sequence theorem composes the result
over a finite Event list.

The theorem does not claim C callback correctness or a C memory-model proof.
The trust boundary remains callback adherence, C descriptor alignment, the C
adapter implementation, compiler, and platform primitives.

## Compatibility

All changes are additive. Existing Machine, Mailbox, Graph, Source, Resumable,
Run, scheduler, and executor signatures and behavior remain unchanged. The new
runtime does not create a worker, scheduler, Graph, or fallback path.

## Verification

C tests cover:

- initialization/binding/type rejection and transactional output;
- literal differential traces against an independent test reference evaluator;
- NONE/EVENT transitions consuming Events until one demanded VALUE;
- WAIT then wake, VALUE_AND_DONE, initial DONE/ERROR, and no-transition ERROR;
- guard/action failure and first-error preservation;
- close during action callback and reentrant repeated close;
- cancel during callback with no state commit;
- queued Event cancellation and terminal accounting identity;
- repeated init/attach/run/close/destroy lifecycles;
- concurrent producers with every accepted Event settled;
- C and C++ aggregate-header compilation.

Verification runs the smallest Machine runtime target first, then adjacent
Machine/Mailbox/Runtime/Executor tests, Release CTest, Windows ASan Debug tests,
Lean focused tests and `lake test`, and installed public-header consumers where
available. Linux/macOS execution and sanitizer jobs remain CI evidence because
this workspace is Windows.
