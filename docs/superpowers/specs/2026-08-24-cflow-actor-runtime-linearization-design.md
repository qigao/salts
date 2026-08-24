# CFlow Actor Runtime Linearization and Graph Boundary Design

**Status:** Proposed for review as a follow-up to issue #71
**Date:** 2026-08-24

## Context

CFlow now has a bounded MPSC Mailbox, a serialized Machine runtime, hierarchical
Machine timers, an Actor lifecycle facade, readiness-backed Sources, Graph/Run
execution, and Lean models for each local protocol. The components are useful
together, but two boundaries need to be made explicit before further runtime
composition:

1. `cflow_machine_instance_cancel()` can race with the final state commit after
   the executor has performed its last cancellation check.
2. The Actor owns an identity Graph and Run, but Graph does not and should not
   own the Actor Mailbox, Machine state, executors, timers, or native readiness
   resources.

This decision fixes the semantic model. A following implementation change will
make the C runtime conform to it. Communication, remoting, persistence,
supervision, and restart remain later work.

## Evidence and severity

### HIGH: cancellation and commit have no shared linearization point

`process_event()` checks cancellation after the action returns, then resolves
the target and reaches the final instance lock. Once that lock is acquired it
copies the staged target state without checking cancellation again. In the
intervening window, `request_cancel()` can acquire the same lock and publish
`cancelled = true`, `done = true`, and DONE readiness.

The observable interleaving is:

```text
executor  observes cancelled = false
cancel    acquires lock, publishes cancellation, releases lock
executor  acquires lock, commits target state and completed accounting
```

This contradicts the existing contract that cancellation discards the
in-flight result and preserves the source state. The current reentrant-cancel
test cancels from inside the action and is observed by the post-action check;
it does not force this final external-thread window.

### MED: the formal model omits control-plane interleavings

`CFlow.MachineRuntime` currently refines only WAIT or one committed Machine
transition. `CFlow.Actor` models lifecycle admission and Mailbox handoff. These
models are valid within their stated scope, but they contain no cancellation
request, staged result, commit phase, scheduler admission, Graph/Run binding,
or concurrent arbitration relation. Existing refinement theorems therefore do
not imply the missing cancellation property.

### MED: Graph participation can be mistaken for resource ownership

The Actor owns an identity Graph and Run and attaches the Machine as a Source.
The Graph governs the output topology and Run demand; the Actor and Machine
remain the owners of input admission and mutable state. Treating the identity
Graph as ownership of the whole Actor would make normalized Graph cloning,
optimization, or replay duplicate move-only runtime resources.

## Theoretical basis

The Actor model serializes access to private behavior and state through one
Mailbox. Akka describes an Actor as queuing a message, becoming schedulable,
and processing one message at a time:

- https://doc.akka.io/libraries/akka-core/current/typed/guide/actors-intro.html

Statechart run-to-completion semantics require all microsteps caused by one
external event to finish before the next external event is processed:

- https://www.w3.org/TR/scxml/#AlgorithmforSCXMLInterpretation

Reactive Streams governs signals across asynchronous boundaries, including
serial delivery and demand, but deliberately does not own an application's
mutable domain resources:

- https://github.com/reactive-streams/reactive-streams-jvm

Machine action execution and state commit are therefore one serialized Actor
turn. Cancellation is an out-of-band control operation. Because the public
contract permits cancellation to discard an already executing action result,
the turn must stage its result and arbitrate cancellation at one explicit
commit boundary. This is stricter than merely running callbacks one at a time.

## Decision

### 1. Preserve the existing ownership layers

```text
producer(s)
    |
    v
ActorRef -> bounded Mailbox -> SerialExecutor -> Machine turn
                                                |
                                                v
                                      stateful Source adapter
                                                |
                                                v
                                   Graph -> Run -> Sink

Runtime assembly binds and orders the lifetime of the concrete resources.
```

Responsibilities remain:

| Layer | Fact source and responsibility |
|---|---|
| Graph | Typed topology, effects/properties, normalization and optimization |
| Run | Demand, Source movement, scheduling, wake/cancel propagation and sink signals |
| Actor | Public lifecycle, producer references, exact admission classification and first Actor error |
| Machine instance | Mailbox, current/staged state, transition accounting and commit arbitration |
| Hierarchy | Machine plus scoped timer ownership and hierarchical transition side effects |
| Platform readiness | Native registration slots, backend handles and quiescent shutdown |
| Runtime assembly | Explicit binding and dependency-ordered shutdown of the above resources |

Graph must not own or clone Mailboxes, mutable Machine states, executors,
timers, native handles, callback user data, or retained Actor references.
Machine and readiness become visible to Graph only through move-style Source
adapters owned by Run after successful open.

### 2. Use orthogonal lifecycle and worker states

Do not encode the protocol as one Cartesian-product enum, and do not keep
adding independent booleans. Model two orthogonal dimensions protected by the
existing instance mutex:

```text
Lifecycle:
OPEN -> CLOSE_REQUESTED -> TERMINAL
  +--> CANCEL_REQUESTED -> TERMINAL

Worker:
IDLE -> SCHEDULED -> EXECUTING -> COMMITTING -> IDLE or terminal settlement
```

`CLOSE_REQUESTED` and `CANCEL_REQUESTED` are distinct because they settle the
in-flight turn differently. `TERMINAL` means the control result is published;
storage destruction still requires executor, producer, adapter, and wake
callback quiescence as required by the existing ownership contract.

The authoritative data unit is one accepted typed Event. Mailbox admission
copies its trivial payload. The SerialExecutor is the single semantic
consumer. During `EXECUTING`, the source state stays authoritative and the
action writes only instance-owned staged target/observation storage.

### 3. Define one commit linearization point

After guard/action completion, the executor acquires the instance mutex and
performs `begin_commit` as one indivisible decision:

```text
precondition: worker == EXECUTING and one staged result exists

if lifecycle == CANCEL_REQUESTED:
    cancellation wins
    discard staged state and observation
    settle the Event as cancelled
else:
    worker = COMMITTING
    commit wins
```

No cancellation check made before this critical section is sufficient for
commit admission.

Once `worker == COMMITTING`, the transition has linearized and must commit
exactly once even if cancellation is requested concurrently. Cancellation then
prevents subsequent Events and applies after that commit. When cancellation
wins before `begin_commit`, the source state and downstream trace stay
unchanged and the in-flight Event is counted exactly once as cancelled.

The state copy, state ID update, transition accounting, prepared output state,
and worker phase update form one locked commit. Wakers and user callbacks are
extracted under the lock and invoked after unlocking.

### 4. Keep close and cancel observably different

| Control operation | Executing turn | Queued Events | Downstream result |
|---|---|---|---|
| `close` | Commit exactly once | Cancel | VALUE_AND_DONE when the committed turn produced VALUE, otherwise DONE |
| `cancel`, cancel wins | Discard staged logical result | Cancel | DONE without VALUE |
| `cancel`, commit wins | Commit exactly once, then stop | Cancel | Committed VALUE may precede DONE |
| runtime failure | Do not commit a failed turn | Cancel | First ERROR |

The concurrent cancel contract is linearizable, not wall-clock based: a cancel
invocation that overlaps `begin_commit` observes whichever operation wins the
mutex-protected decision.

Action callbacks may perform external effects because they run outside the
instance lock. Cancellation can discard only staged Machine state and
observations; it cannot roll back arbitrary effects already performed by user
code. Callbacks requiring atomic external effects must instead stage a typed
command/outbox entry and execute it after commit, or provide their own
idempotency/compensation contract. No implicit rollback is promised.

### 5. Treat Machine/Actor as an atomic stateful Source

An Actor-backed Source is semantically:

```text
STATEFUL | HOT | ORDERED | SINGLE_CONSUMER |
NON_REPLAYABLE | NON_CLONEABLE | OPTIMIZER_BARRIER
```

These names describe the design properties; they do not add public property
constants in this PR. A future Graph admission API must map them to existing or
new typed effect/property descriptors before optimizer use.

The optimizer must not duplicate, reorder across, fuse into, or compile through
an Actor/Machine Source unless a dedicated proof establishes equivalence.
Internal Machine transitions remain an atomic cyclic behavior rather than
ordinary acyclic Graph operators.

Actor currently requests `SIZE_MAX` output demand. Its bounded input Mailbox is
therefore the producer-facing backpressure boundary, while Graph demand still
governs how many downstream values Run may signal. `ACCEPTED` means exactly one
Event copy entered the Mailbox; it does not promise successful processing, as a
later executor rejection or callback failure is represented by terminal error
and Event accounting.

### 6. Add Runtime Assembly only when Graph-level composition needs it

The future assembly is a control-plane builder/binding object, not another
executor or service locator. It binds stable logical resource keys from a
Graph construction boundary to explicitly supplied runtime owners:

```text
logical source slot -> one move-style Source adapter
Machine owner       -> borrowed SerialExecutor and immutable Machine IR
Run                 -> borrowed Graph and Scheduler, moved Source
native source       -> readiness registration and external resource owner
```

Bindings fail transactionally on missing, duplicate, incompatible, stale, or
already-moved resources. Successful Run open transfers each Source exactly
once. Shutdown follows the existing order: stop admission, cancel/finish Run,
close Run and moved Sources, wait for callbacks/executors to become quiescent,
then destroy external drivers and owners.

This PR defines the boundary but adds no public Runtime Assembly API. An API is
admitted only with a concrete multi-source composition requirement and exact
ownership tests.

## Formal model and proof obligations

Extend `CFlow.MachineRuntime` with:

- `ControlLifecycle`: OPEN, CLOSE_REQUESTED, CANCEL_REQUESTED, TERMINAL;
- `WorkerPhase`: IDLE, SCHEDULED, EXECUTING, COMMITTING;
- a commit-arbitration state containing authoritative source `Config`, optional
  staged `Config`, and completed/cancelled counters;
- pure `requestCancel`, `requestClose`, `beginCommit`, `commit`, and
  `discardCancelled` transitions;
- a small legacy relation that exposes the missing recheck counterexample.

Required theorems are:

1. legacy last-check, concurrent cancel, then unconditional commit can produce
   `cancel requested` together with the staged target state;
2. cancel before `beginCommit` makes commit admission impossible;
3. cancel-before-commit settlement preserves the source state and increments
   cancellation exactly once;
4. `beginCommit` before cancel commits the staged state exactly once;
5. the two arbitration outcomes are exclusive;
6. successful runtime transition trace refinement remains unchanged.

These theorems prove the abstract protocol. They do not prove C callback
correctness, C11 mutex implementation, compiler behavior, or native backend
memory ordering. C tests and sanitizers remain the executable refinement
evidence.

## Alternatives and tradeoffs

### Serialize cancel as an ordinary Actor message

This is the simplest run-to-completion interpretation: cancellation is handled
only after the current turn commits. It was rejected because it changes the
existing public distinction between close and cancel and cannot implement
discard-in-flight semantics.

### Add only one final `cancelled` boolean recheck

A final check under the commit mutex is the minimum patch for the observed
race. It was not selected as the design model because the runtime already has
several correlated lifecycle and scheduling booleans; another local branch
would fix this window without defining which combinations are legal. The
implementation may retain compatibility booleans for statistics, but the
authoritative transition must be expressed by lifecycle and worker phases.

### Put Actor resources directly in Graph nodes

This would make resource discovery convenient, but it conflicts with Graph
normalization, independent destination graphs, optimizer rewriting, and Run's
move-style Source ownership. It also risks duplicating mutable or native
resources when a Graph is copied. The selected design keeps Graph declarative
and introduces explicit runtime binding only when a concrete composition API
requires it.

### Selected tradeoff

The chosen protocol adds enum state and one commit decision inside the existing
instance mutex. It adds no allocation, worker, queue, lock, or callback under
lock. The critical section includes the state copy and accounting already
performed under that mutex, so the expected steady-state cost is one lifecycle
branch and phase updates. This is a design expectation, not a measured
performance claim; the implementation PR must compare representative Machine
throughput and lock contention if profiling shows a material regression.

The additional model complexity buys a single fact source for legal lifecycle
and worker transitions, deterministic race tests, and proof obligations that
map directly to the C critical section. Deferring a public Runtime Assembly API
avoids committing to resource-key ABI or lookup overhead before a multi-source
use case exists.

## Compatibility and migration

- No public C symbol, struct layout, error code, Graph IR, serialized format, or
  build dependency changes in this design PR.
- The following implementation changes only a previously under-specified
  concurrent race. Non-overlapping close/cancel and transition behavior remains
  compatible.
- A concurrently overlapping cancel may now observe a committed VALUE when
  commit linearizes first. This is required for a total, linearizable contract
  and must be documented in the public header when implemented.
- Existing Actor stop uses close semantics and remains commit-on-in-flight.
- Communication, supervision, persistence, restart, and distributed delivery
  guarantees remain outside this decision.

Rollback is documentation/model-only for this PR. The implementation PR can be
reverted without data migration because no persisted state or wire format is
introduced.

## Verification contract for the implementation PR

The implementation is accepted only with:

1. a deterministic barrier immediately before the commit arbitration lock;
2. an external-thread cancel-wins test proving source state preservation, no
   VALUE, and exact terminal accounting;
3. a commit-wins test proving one state update, at most one VALUE, and no later
   Event processing;
4. close-wins tests preserving the existing in-flight commit behavior;
5. repeated cancel/close and queued Event accounting tests;
6. Actor and Machine hierarchy adjacent regression tests;
7. sequential `lake build` and `lake test`, with no proof placeholders;
8. Windows Release/ASan evidence and remote Linux sanitizer/stress evidence
   when implementation code changes.
