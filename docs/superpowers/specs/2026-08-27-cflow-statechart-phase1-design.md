# CFlow Format-neutral Statechart Phase 1 Design

**Issue:** qigao/salts#122

**Date:** 2026-08-27
**Reference semantics:** [W3C SCXML 1.0 core constructs and interpretation algorithm](https://www.w3.org/TR/scxml/)

## Scope

Phase 1 adds a format-neutral Statechart IR and runtime. It supports compound,
parallel, initial, final, shallow-history, and deep-history nodes; ordered
exit/transition/entry executable actions; eventless and completion-triggered
transitions; internal and external queues; deterministic conflict resolution;
configuration snapshots; and configuration-scoped timers.

This phase does not parse XML, implement an SCXML data model, provide external
communication, or claim SCXML processor conformance. `send`, `invoke`,
`finalize`, durable workflow state, recovery, and distributed coordination stay
outside this module.

Existing `cflow_machine`, `cflow_machine_instance`, and
`cflow_machine_hierarchy_instance` declarations and behavior remain unchanged.
The new Statechart API is additive. The exclusive hierarchy can be compiled to
the new IR by an explicit compatibility adapter after the native engine is
verified; the existing hierarchy runtime is not silently redirected in this
phase.

## Evidence and architectural problem

- **Fact:** `cflow_machine_instance` owns one current state ID and one copied
  state value. Its transition lookup is keyed by `(current_state, event)`.
- **Fact:** `cflow_machine_hierarchy` lowers every leaf/ancestor candidate to a
  flat Machine row and its runtime delegates the current state to the flat
  Machine instance.
- **Fact:** scoped timer admission currently tests one active leaf-to-root
  ancestry chain.
- **Inference:** parallel regions cannot be represented by adding another
  transition kind to the flat IR. They require a configuration set, set-valued
  exit/entry domains, and conflict selection before any state commit.

## Alternatives

### A. Extend flat Machine with several current-state IDs

Rejected. The flat transition table has already erased compound/parallel
domains, initial/history pseudo-nodes, and document order needed by selection.
Reconstructing them in the runtime would duplicate hierarchy metadata and make
the flat Machine cease to be a canonical IR.

### B. Replace the existing hierarchy runtime in place

Rejected for Phase 1. It would change public behavior, callback timing, timer
scope rules, and terminal accounting for existing users before the richer
engine has differential evidence.

### C. Add a native Statechart IR and runtime beside Machine

Chosen. The new opaque objects express the richer semantics directly, preserve
old APIs, and provide a future compilation target for both the C hierarchy
adapter and the optional SCXML frontend.

## Public declaration model

`cflow/statechart.h` introduces opaque owning handles and copied declaration
rows. Numeric declaration order is explicit, so a parser is not required to
preserve deterministic ordering.

```c
typedef uint32_t cflow_statechart_transition_id;
typedef uint32_t cflow_statechart_executable_id;

typedef enum cflow_statechart_state_kind {
    CFLOW_STATECHART_ATOMIC,
    CFLOW_STATECHART_COMPOUND,
    CFLOW_STATECHART_PARALLEL,
    CFLOW_STATECHART_INITIAL,
    CFLOW_STATECHART_FINAL,
    CFLOW_STATECHART_HISTORY_SHALLOW,
    CFLOW_STATECHART_HISTORY_DEEP
} cflow_statechart_state_kind;

typedef struct cflow_statechart_state {
    cflow_machine_state_id id;
    cflow_machine_state_id parent;
    cflow_statechart_state_kind kind;
    uint32_t document_order;
} cflow_statechart_state;
```

Initial and history nodes are pseudo-nodes and never appear in an active
configuration. Each has exactly one target-bearing, eventless default
transition. A compound state has exactly one direct initial pseudo-child. A
parallel state enters every direct non-pseudo child. An atomic or final state
has no children. The root is a compound or parallel state, or the sole
childless final state.

Transition triggers are typed instead of encoded as strings:

```c
typedef enum cflow_statechart_trigger_kind {
    CFLOW_STATECHART_TRIGGER_EVENTLESS,
    CFLOW_STATECHART_TRIGGER_EVENT,
    CFLOW_STATECHART_TRIGGER_COMPLETION
} cflow_statechart_trigger_kind;

typedef enum cflow_statechart_transition_kind {
    CFLOW_STATECHART_TRANSITION_EXTERNAL,
    CFLOW_STATECHART_TRANSITION_INTERNAL
} cflow_statechart_transition_kind;
```

An EVENT trigger references one declared `cflow_event_id`. A COMPLETION
trigger references the completed compound/parallel state ID. The future SCXML
adapter maps that trigger to `done.state.<id>` without putting string naming in
the core runtime. A target ID of zero denotes a targetless transition.

Statechart guards and executables use Statechart-specific declarations and
callbacks because entry/exit, completion, and eventless execution may not have
a user Event. Both receive a nullable `cflow_event_view`; an EVENT trigger
passes its typed view while eventless/completion triggers pass NULL. The later
exclusive adapter may wrap an existing Machine guard only for matching EVENT
triggers.

```c
typedef enum cflow_statechart_action_phase {
    CFLOW_STATECHART_ACTION_EXIT,
    CFLOW_STATECHART_ACTION_TRANSITION,
    CFLOW_STATECHART_ACTION_ENTRY,
    CFLOW_STATECHART_ACTION_INITIAL,
    CFLOW_STATECHART_ACTION_HISTORY
} cflow_statechart_action_phase;

typedef bool (*cflow_statechart_executable_fn)(
    void *user,
    cflow_statechart_action_phase phase,
    cflow_machine_state_id owner,
    const void *state,
    const cflow_event_view *event,
    void *out_state,
    cflow_statechart_raise_fn raise_internal,
    void *raise_user,
    const char **out_error);

typedef bool (*cflow_statechart_is_active_fn)(
    void *user, cflow_machine_state_id state);

typedef struct cflow_statechart_executable_context {
    cflow_statechart_action_phase phase;
    cflow_machine_state_id owner;
    const void *state;
    const cflow_event_view *event;
    void *out_state;
    cflow_statechart_raise_fn raise_internal;
    void *raise_user;
    cflow_statechart_is_active_fn is_active;
    void *configuration_user;
} cflow_statechart_executable_context;
```

Executable rows declare one state type and effect/property contract. State
actions and transition actions are separate ordered reference rows. Every
referenced executable has one exact runtime binding. A binding supplies exactly
one legacy callback or contextual callback. Contextual callback arguments and
the active-state query are borrowed for the callback only; the query reads a
bounded executor-owned working configuration and never exposes or duplicates
the published configuration. Appending the contextual callback preserves
three-field source initializers but changes the binding row ABI size, so old
consumers must relink. The runtime admits only
the existing ABI-safe trivial state/Event fragment during Phase 1. Managed
values remain a later admission extension, not a byte-copy fallback.

## Build validation and normalized IR

Build copies all rows, checked-multiplies every allocation, sorts lookup tables,
and publishes only after complete validation. It rejects:

- zero, duplicate, unknown, or out-of-limit IDs;
- parent cycles, multiple roots, duplicate document order, and illegal child
  kinds;
- document order that is not a hierarchy-compatible depth-first preorder:
  parents must precede descendants and each subtree must be one contiguous
  interval (otherwise ancestry-first and document-order action rules can form
  a comparator cycle);
- missing or multiple initial pseudo-children for compound states;
- initial/history defaults with missing, multiple, guarded, event-triggered, or
  targetless transitions;
- history nodes outside compound or parallel parents;
- transitions whose source is initial/history except their one default row;
- completion triggers that name a state that cannot complete;
- type/guard/executable contract mismatches;
- ambiguous same-source trigger priorities;
- any normalized count or byte size that exceeds configured limits.

The normalized IR stores dense state indices, parent/depth/document-order
tables, child spans, transition spans, action spans, initial/default targets,
and precomputed transition domains. It does not allocate during selection or
execution.

## Active configuration and history representation

One instance owns two fixed-capacity configuration buffers. Each buffer has a
bitset over normalized states and an ordered list of active real states. The
published buffer is immutable until the next successful microstep swaps the
active/staged roles under the instance mutex. Pseudo-states never enter either
buffer.

Legal configuration invariants are:

1. every active atomic/final state has all real ancestors active;
2. every active compound state has exactly one active direct child;
3. every active parallel state has every direct real child active;
4. no initial/history pseudo-state is active;
5. list order is document order and contains no duplicate;
6. a nonterminal configuration contains at least one atomic or final leaf.

History is instance-owned bounded storage indexed by history pseudo-node. A
shallow slot records the active immediate children of its parent. A deep slot
records active atomic/final descendants. History is staged from the published
configuration before exit actions and committed with the new configuration.
An unset history target follows its declared default transition. Restoring a
shallow slot applies default entry below each remembered child; restoring a
deep slot reconstructs the recorded descendants and their ancestors.

## Deterministic selection

For one trigger, active atomic/final leaves are visited in document order. At
each leaf, the engine examines the leaf and then its proper ancestors. The
first guard-enabled transition in declaration priority/order is that leaf's
candidate. Duplicate candidate IDs are removed while retaining first
selection order.

Candidates conflict exactly when their computed exit sets intersect.
Filtering is deterministic:

1. a candidate whose source is a proper descendant preempts an already chosen
   ancestor-source candidate;
2. otherwise the candidate selected by the earlier active leaf wins;
3. targetless transitions have empty exit sets and do not conflict.

The selected ordered set is immutable for the microstep. Guards are evaluated
against the same published extended-state snapshot. No transition action runs
until conflict filtering finishes.

Contextual guard bindings may additionally query the same immutable published
configuration used by selection. The query is call-scoped, returns false for
unknown and pseudo-state IDs, and cannot expose or retain the instance-owned
configuration bitset. Legacy guard callbacks retain their existing signature
and behavior.

## Microstep and macrostep

A microstep executes one selected transition set in three global phases:

1. compute the union exit set, save affected history, and run exit actions in
   descendant-first/reverse-document order;
2. run selected transition actions in selection order;
3. compute effective targets, initial/history/default descendants and required
   ancestors, then enter in ancestor-first/document order.

Callbacks observe the W3C action-time configuration through contextual
bindings. Each exit action span runs before its owner is removed; transition
content sees every selected exit removed; each entering state is added before
its entry span; and selected initial/history default transition content runs
after its owning state's entry span but before later descendant entries.
Initial stabilization starts from an empty working configuration. The final
staged configuration remains the only commit artifact; callback or bounded
queue failure discards the working view with all other staged state.

The build-time document-order invariant has two parts. Unique order plus
ancestor-before-descendant makes the current ancestry/document comparators
equivalent to reverse document order for exit and document order for entry,
which makes both strict total orders. Contiguous subtree intervals are a
stronger normalized-IR contract: they prevent a closed subtree from being
reopened and support interval-oriented traversal, but are not independently
required for comparator totality.

Executable actions update one staged extended-state value sequentially. The
next action observes the prior action's staged value. A callback may enqueue a
typed internal Event through the bounded raise function. Raised Events first
enter a fixed microstep-local staging area and are appended to the internal
FIFO only at commit. Queue-full or callback failure owns the first error and
prevents publication of staged state, configuration, history, raised Events,
and generated completion Events. External callback
side effects already performed cannot be rolled back and remain the
application's idempotency/compensation responsibility.

After successful entry, completion is computed bottom-up. Entering a final
child completes its compound parent; a parallel completes only when every
direct region is in a final configuration. Newly completed states enqueue one
deduplicated internal COMPLETION trigger. The latch is once per active entry:
exiting a compound/parallel state clears its staged latch before bottom-up
completion detection, so immediate exit/reentry generates a fresh trigger.
Then staged extended state, history, configuration, and queue writes are
committed exactly once under the mutex.

One macrostep consumes at most one external Event and then runs to quiescence:

1. repeatedly select eventless transitions;
2. if none, consume one internal Event FIFO item;
3. if none, consume one completion FIFO item;
4. execute an enabled transition set and repeat;
5. stop when all three semantic sources are quiescent;
6. only then admit the next external Event to execution.

`microstep_limit` is a required positive hard bound per macrostep. Exceeding it
is a terminal first error; there is no silent truncation or fallback. This
turns eventless cycles into a diagnosable bounded failure.

## Data-path protocol

| Concern | Contract |
|---|---|
| Data unit | copied typed external/internal Event, completion trigger, or one selected transition set |
| Fact source | one `cflow_statechart_instance` owns configuration, history, extended state, queues, counters, and first error |
| Ownership | definition copies rows; instance copies state/Event bytes and binding rows; descriptors/executor/callback users are borrowed |
| Lifetime | borrowed objects outlive instance destroy; snapshots are caller-owned copies; no borrowed configuration view crosses a commit |
| Topology | external admission is MPSC; exactly one non-manual SerialExecutor callback consumes and mutates semantic state |
| Ordering | external/internal/completion FIFO; eventless, then internal, then completion, then at most one external per macrostep; document-order selection/action rules |
| Capacity | configured external/internal queue capacities, state count, selected-transition count, action count, timer capacity, and microstep limit |
| Backpressure | external/internal FULL, CLOSED, trigger/type mismatch, and limit exceeded are distinct results; nothing is dropped or expanded |
| Failure | first owned error; failed microstep publishes no staged semantic state; accepted external Event is settled failed exactly once |
| Shutdown | stop admission, preserve a commit that linearized first, cancel queued work, detach waits, wait executor idle, then destroy |
| Observability | configuration version, queue counts, macro/microstep counts, action counts, timer counts, terminal flags, and first error |

All storage is allocated at build/init. Selection, microstep, timer scope
cancellation, and snapshot copy perform no unbounded allocation. The runtime
uses a mutex for control/state publication and does not introduce a lock-free
configuration structure. External mailbox transfer and accepted-Event
accounting linearize under that instance mutex. If both locks are needed, the
only order is instance mutex then private mailbox mutex. The runtime never arms
the private mailbox waker. Terminal paths cancel the mailbox and detach any
waker while both state changes are linearized, then release the instance mutex
before invoking the detached callback or posting Executor work. Every live
stats snapshot therefore satisfies
`accepted = sat(completed + failed + cancelled + pending + in_flight)`, with
all counters and intermediate additions saturating at `UINT64_MAX`.

## Query and compatibility API

`cflow_statechart_instance_copy_configuration()` first reports the required
state count. If caller capacity is insufficient, it returns
`CFLOW_STATECHART_SNAPSHOT_TOO_SMALL`, writes no partial list, and reports the
required count. Success copies document-ordered active real state IDs and a
monotonic configuration version.

`cflow_statechart_instance_current_state()` returns the sole active atomic or
final leaf for the exclusive fragment and zero for a configuration with more
than one active leaf. Existing `cflow_machine_instance_current_state()` is not
changed.

A later explicit hierarchy adapter may differential-test exclusive declarations
against both engines. It must not introduce a second mutable current-state
cache.

## Scoped timers

Statechart timer scope admission succeeds only when the scope bit is present in
the published configuration. A successful microstep cancels pending timers for
every exited state before the new configuration becomes observable. A timer
already claimed for firing retains the existing FIRE_WON rule. Timer events
enter the external queue and therefore never bypass run-to-completion internal
processing. Close/cancel rejects schedules and settles pending timer slots.

## Error and lifecycle semantics

- Build/init failure leaves the owning handle empty.
- Guard/executable failure, queue overflow during internal raise/completion,
  invalid runtime phase, or microstep-limit exhaustion stores one stable first
  error and cancels queued work.
- Exactly one terminal outcome wins under the instance mutex: clean root
  completion, explicit close, explicit cancel, or error. That winner owns all
  terminal flags, first status/error, and external settlement; later control or
  Executor cancellation is a no-op and cannot replace the cause.
- `close` rejects new external events and timers, lets a commit that already
  won finish, then settles queued events canceled and exposes DONE.
- `cancel` rejects admission and discards an uncommitted staged microstep. A
  commit that linearized first remains visible exactly once. If cancel wins
  after commit but before the external macrostep reaches quiescence, the
  committed state remains visible while that Event settles `cancelled`, not
  `completed`; no rollback is attempted.
- Initialization stabilization may invoke callbacks before init returns. The
  public instance handle is not published yet, so public instance control APIs
  are unavailable from those callbacks.
- Destroy is a quiescent control-plane operation. It waits for the borrowed
  executor to become idle before freeing callback-visible storage.

## Formal model

Lean adds `CFlow.Statechart` and `Proofs.Statechart` rather than overloading the
single-leaf hierarchy model. The model defines trees, legal configurations,
candidate selection, exit-set conflict filtering, ordered exit/entry lists,
history restore, completion triggers, and macrostep traces.

Required theorems:

- selection produces a conflict-free ordered set and is deterministic;
- every successful microstep from a legal configuration produces a legal
  configuration;
- exit order is descendant-first and entry order ancestor-first;
- shallow/deep restore satisfies their recorded-child/descendant predicates;
- the exclusive fragment projects to the existing hierarchy candidate and
  route semantics;
- a C conformance trace row refines one Lean microstep/macrostep row.

The proof boundary excludes C allocation, callback correctness, the C memory
model, compiler behavior, and external side effects. C differential tests and
sanitizers cover those boundaries.

## Verification and migration

Focused C tests cover build rejection, initial descent, parallel entry, legal
configuration snapshots, compatible/conflicting transitions, exact action
trace order, targetless/internal/external transitions, shallow/deep history,
completion/eventless stabilization, internal-before-external ordering,
capacity failure, microstep cycles, close/cancel races, and configuration-scoped
timers.

Adjacent regressions cover Machine, hierarchy, runtime, Actor, Mailbox,
executor, timer, Source/Run, C/C++ aggregate headers, and installed headers.
Lean focused files and `lake test` must pass without `sorry` or `admit`.
Windows Release and ASan run locally; Linux GCC/ASan runs remotely through
`root@eu`. A merge can be rolled back by removing the additive Statechart
module because existing APIs are not redirected.

The Phase 1 implementation was verified on 2026-08-27. Windows Release passed
149/149 full tests and its installed-package consumers; Windows ASan passed the
eight focused binaries. Remote Linux GCC Release passed 154/154 full tests and
Linux ASan passed the same eight focused targets. Lean passed the focused proof,
the aggregate Phase A import, and all 81 `lake test` build targets without
`sorry` or `admit`.

## Compatibility risks

- **HIGH:** prematurely redirecting hierarchy execution would change callback
  ordering and terminal semantics. Mitigation: keep the adapter explicit.
- **HIGH:** publishing a partial configuration after action failure would make
  state/history disagree. Mitigation: staged buffers and one commit point.
- **MED:** a too-small internal queue or microstep bound can reject a valid but
  large macrostep. Mitigation: explicit configurable limits, stats, and exact
  failure; no fallback.
- **MED:** numeric document order supplied by native callers can be wrong.
  Mitigation: build validates unique order and a hierarchy-compatible preorder
  (ancestor-before-descendant plus contiguous subtree intervals), and exact
  ordering tests use IDs that disagree with document order. This additive
  Statechart admission has not been published, so rejecting malformed IR adds
  no compatibility cost to a released API. The future frontend generates
  document order from source order.
- **LOW:** the additive installed header increases public API surface.
  Mitigation: opaque objects, C++ header compilation, examples, and install
  consumer tests.
