# CFlow SCXML Phase 3 Remaining Features Design

**Issue:** [qigao/turbo-utils#122](https://github.com/qigao/turbo-utils/issues/122)

**Status:** Accepted. Increment A (`send`, `cancel`, recoverable adapter errors,
and owning sessions) and Increment B (`invoke`, restricted `finalize`, stable
lifecycle, returned Events, and autoforward) are implemented. Increments C-E
remain design scope.

**Date:** 2026-08-29

**Reference:** [W3C SCXML 1.0](https://www.w3.org/TR/scxml/)

## Decision summary

Continue the existing inline executable-content design: one SCXML executable
block lowers to one native Statechart executable, and the owning
`cflow_scxml_program` retains immutable index-based steps. Extend that bytecode
instead of emitting one native callback per XML element or retaining XML nodes.

Split the remaining work into two semantic profiles:

1. complete the null-data-model profile with recoverable error events and
   literal `send`, `cancel`, `invoke`, and `finalize` through bounded,
   versioned adapters; then
2. select and implement an optional non-null data model before admitting
   `assign`, `foreach`, value/location expressions, or accessible system
   variables.

`assign` and `foreach` must remain rejected by the null frontend. SCXML 1.0
Appendix B.1 defines no null-model location/value language, makes system
variables inaccessible, and explicitly excludes `foreach` plus the data
manipulation elements. Implementing null-specific substitutes would be a
non-standard extension, not progress toward conformance.

External effects must not be published directly while a native Statechart
microstep is still rollback-capable. Adapter admission therefore uses a
bounded `prepare -> commit | discard` reservation protocol. `prepare` has no
externally visible effect; a successful ticket has exactly one terminal call;
`commit` is nonblocking and cannot fail because capacity was reserved.

## Evidence and current boundaries

### Repository facts

- `cflow-scxml/src/scxml.c` owns immutable block, step, branch, Event I/O, and
  invocation descriptors. Supported inline steps include `RAISE`, `IF`,
  `LOG`, `SEND`, `CANCEL`, and internal invocation enter/exit intents.
- `execute_scxml_block()` is one contextual native executable callback. It
  copies the staged state, walks an immutable step range, uses the native
  `raise_internal` staging function, and observes the action-time
  configuration through `is_active`.
- `cflow/src/statechart_runtime.c` commits staged internal events,
  completions, extended state, and configuration together. A callback that
  returns false currently causes a terminal Statechart action failure.
- The native runtime now exposes optional versioned, format-neutral stable and
  external-preprocess hooks plus bounded adapter-internal Event ingress.
  Callbacks execute on the SerialExecutor outside the runtime mutex and see
  only call-scoped borrowed configuration/Event views.
- `cflow_scxml_program_runtime_bindings()` returns program-owned binding rows;
  their `user` values are shared by all instances and therefore cannot hold
  per-session adapter state.
- CSerde and CBind are format-neutral TurboUtils token/binding layers.
  TurboParser DataBind 2.0, ABI 8, provides owned dynamic values, traversal,
  clone, and serialization, but its public API does not provide a mutable
  SCXML location-expression store or expression evaluator.
- TurboUtils must not depend on TurboParser. A future DataBind integration can
  live in TurboParser and consume an installed TurboUtils SPI, but CFlowScxml
  cannot link `TurboParser::DataBind`.

### Standards facts

- Executable elements in a block run in document order. Raising an error stops
  the rest of that block, not unrelated later blocks.
- `error.execution` and `error.communication` are internal Events, not
  terminal interpreter failures.
- `send` evaluates its arguments when encountered. Delayed delivery returns
  immediately; an invalid/unsupported target or type produces
  `error.execution`, while inability to dispatch produces
  `error.communication`.
- `cancel` affects only delayed sends created by the same session and is
  best-effort when delivery has already won.
- `invoke` starts only after the Statechart reaches a stable configuration.
  Leaving the owning state cancels the invocation.
- `finalize` preprocesses only Events returned by its invocation, before
  transition selection. Its content cannot contain `raise`, `send`, or other
  external actions.

## Scope matrix

| Feature | Null profile | Required architecture | Planned increment |
|---|---|---|---|
| `error.execution`, `error.communication` | Required and usable, but not expression-accessible | recoverable block abort plus internal adapter inbox | A |
| literal `send` and `cancel` | Supported without payload expressions | SCXML session, Event I/O adapter, bounded tickets and send registry | A |
| literal `invoke` and null-safe `finalize` | Supported without data expressions | stable hook, invoke adapter, invocation registry and pre-event hook | B |
| `assign` | Unsupported by the null model | selected non-null model, managed transactional state, location/value evaluator | C/D |
| `foreach` | Unsupported by the null model | iterable snapshot protocol, bounded loop budget and assignment support | C/D |
| accessible system variables | Inaccessible in the null model | read-only bindings supplied by the selected non-null model | C/D |
| conformance claim | Exact null/core claim only | W3C-derived corpus and documented optional capabilities | E |

The #122 iteration and assignment checkboxes remain open until a non-null data
model implements their normative semantics. They must not be checked merely
because the null profile correctly rejects those elements.

## Architecture

```text
SCXML source
    |
    v
cflow_scxml_program (immutable, shared)
  - native Statechart IR
  - inline block bytecode
  - literal strings / expression handles
  - invoke/finalize declarations
  - feature requirements
    |
    v
cflow_scxml_session (mutable, one semantic owner)
  - native Statechart instance -------------------- configuration fact source
  - per-session bindings and system context
  - bounded prepared-effect journal
  - delayed-send and invocation registries
  - bounded adapter-result ingress
    |                         |
    v                         v
Event I/O adapter v1      Invoke adapter v1
```

The program remains reusable and thread-safe after publication. A session is
the only owner of mutable SCXML session state. Native Statechart configuration
remains the sole active-state fact source; the session queries it and never
maintains a second configuration mirror.

Adapter-free documents keep the existing low-level construction path and
`cflow_scxml_program_runtime_bindings()`. A program requirements query will
identify documents that require a `cflow_scxml_session`. Requesting the legacy
binding path for such a newly admitted document must fail before instance
initialization, rather than fail later inside an action.

## Candidate comparison

| Candidate | Correctness | Complexity and performance | Decision |
|---|---|---|---|
| Extend the current inline IR and add an owning session plus generic native hooks | Preserves one configuration fact source, block semantics, rollback, and stable invoke timing | Adds bounded session/runtime protocols; one indexed dispatch per step | Chosen |
| Call transport/invoke adapters directly from `execute_scxml_block()` | Preserves immediate document order but publishes effects before the microstep commits | Smallest patch, but later queue/action failure cannot roll back an already visible effect | Rejected |
| Lower every XML executable element to an independent native executable row | Uses existing native callbacks but loses one natural boundary for conditional/foreach block abort and effect journaling | Multiplies action rows and callback dispatch; nested control flow still needs a second IR | Rejected |
| Put configuration, data model, and invocation state in an SCXML-only side runtime | Avoids native hooks initially | Duplicates Statechart facts and requires bidirectional synchronization during rollback, close, and cancel | Rejected |
| Link DataBind/CSerde directly as the SCXML data model | Supplies value codecs but not normative expressions or mutable locations | DataBind would reverse the TurboUtils/TurboParser dependency; CSerde is a token protocol rather than a store | Rejected |

The chosen design has more control-plane surface than direct callbacks, but
the added protocols correspond to existing semantic boundaries: native
microstep commit, macrostep quiescence, pre-selection Event processing, and
external adapter lifecycle. It does not add another interpreter fallback,
worker thread, or unbounded queue.

## Inline executable IR

The step stream grows by kind and indexes into program-owned typed tables:

- `RAISE(event_id)`;
- `LOG(string_offset)`;
- `IF(first_branch, branch_count)`;
- `ASSIGN(location_handle, value_handle)`;
- `FOREACH(array_handle, item_handle, optional_index_handle, child_span)`;
- `SEND(send_descriptor_index)`; and
- `CANCEL(cancel_descriptor_index)`.

`invoke` is not an ordinary step. It belongs to a state declaration and is
activated only at the stable macrostep boundary. Each invoke declaration owns
an optional finalize block index.

Steps should store offsets/indices rather than retained XML pointers or raw
adapter pointers. Program-owned tables retain decoded strings, source
locations, compiled expression handles, send descriptors, and invocation
descriptors. Every count, span, byte total, and allocation product uses checked
arithmetic. Existing `max_name_bytes` remains the combined retained-string
budget; new public limits separately bound steps, runtime operations, prepared
effects, delayed sends, invocation instances, and adapter-result ingress.

For a block without dynamic iteration, execution is O(executed steps). With
`foreach`, work is O(snapshot item count times selected child steps) and must
also consume a per-block operation budget. Exceeding the budget is a
deterministic resource failure; it must not become an unbounded loop or hidden
allocation.

## Error semantics

The inline interpreter needs an internal result with three outcomes:

- `CONTINUE`: execute the next step;
- `BLOCK_ABORTED`: a recoverable SCXML error Event was staged; preserve prior
  successful assignments/effects, skip the rest of this executable block, and
  return native action success; and
- `FATAL`: an invariant, storage, executor, or bounded-queue failure prevents
  representing the required semantics; return native action failure.

Program admission adds `error.execution` or `error.communication` to the
native Event schema only when a compiled feature can produce it. Reserved
names are deduplicated with author-declared transitions and appended after
existing first-occurrence event IDs so that current event mappings do not
shift.

Within one callback, the interpreter first builds a bounded journal of
internal raises and prepared external tickets. It then stages all internal
Events through the native transactional `raise_internal` API. Only after every
required internal Event is accepted may it commit prepared tickets in document
order. If native staging fails, every ticket is discarded and no external
effect is published. A normal SCXML step error records the corresponding error
Event and stops only the owning block.

Asynchronous adapter failure enters a bounded session result inbox. A new
format-neutral native adapter ingress is required to publish the mapped error
as an internal Event ahead of the next external Event. Re-labeling it as an
external Event would violate SCXML queue priority and is rejected.

## Versioned adapter protocol

The public adapters use pure C structs with an ABI header containing
`abi_version`, `struct_size`, and capability bits. Session initialization
copies the small ops tables and borrows adapter contexts until quiescent
session destruction. Unknown versions, short structs, missing required
callbacks, duplicate capabilities, and zero capacities fail initialization.

### Event I/O adapter v1

The v1 operations cover:

- prepare one literal send request into a move-only ticket;
- commit or discard that ticket exactly once;
- prepare/commit/discard a same-session delayed-send cancellation; and
- close, query quiescence, and release adapter-owned resources.

`prepare` runs on the session's SerialExecutor, performs validation and bounded
capacity reservation, and copies every request field it needs after return.
It returns one of: accepted, `error.execution`, `error.communication`, full,
closed, or invalid contract. Full/closed are explicit resource outcomes; the
session never retries or allocates an unbounded fallback queue.

The default SCXML Event I/O capability must support literal `#_internal`
delivery without an external adapter by lowering it to the internal Event
journal. The Recommendation also uses `_internal` in the send attribute table,
event-type rules, and examples; admission should accept it as a documented
spec-text compatibility spelling with identical semantics. Other targets and
types require a matching adapter capability.

Delayed sends use a session-owned ID registry. An author-supplied `id` is
retained literally; generated IDs are required only when a selected non-null
data model can expose `idlocation`. Cancellation is scoped to that registry,
and delivery/cancel races expose one authoritative winner.

### Invoke adapter v1

The invoke adapter uses the same ticket protocol for start and cancel. One
session-owned invocation row records invoke ID, owning state, adapter token,
autoforward flag, lifecycle, and finalize block.

Entry actions record a pending start but do not publish it. At native
macrostep quiescence, the session starts only invocations whose owner is still
active. Exit processing records cancellation; a committed exit cancels the
matching live invocation exactly once. A discarded microstep neither starts
nor cancels an external service.

An adapter completion/event enters the session with its nonzero execution
token. The session validates it at admission and again before selection, runs
only that invocation's finalize block, performs declaration-ordered
autoforwarding, and then offers the Event for normal transition selection.
Late results from a canceled or destroyed invocation are rejected or dropped
and counted, never rebound to a new invocation.

## Native Statechart hooks

The SCXML session requires small format-neutral runtime hooks rather than
duplicating the Statechart driver:

1. a bounded adapter-internal Event ingress whose Events are drained after the
   current microstep commit and before external admission;
2. a quiescent-macrostep callback on the SerialExecutor for deferred invoke
   activation; and
3. an external-Event preprocessing callback before guard selection for
   finalize and autoforward handling.

These hooks must be optional and preserve all existing Statechart behavior
when absent. They may not expose mutable configuration arrays, execute while
the runtime mutex is held, or permit a callback to retain borrowed Event or
configuration views. Hook failure must map to an explicit native runtime
status and stable first error.

The hooks are implemented with native ABI/version, ordering, tagged-FIFO,
failure, and compatibility tests; absent hooks preserve existing runtime
behavior. Because the hook pointers append to the public native instance
configuration, CFlow component version `3.0.0` / shared ABI identity `3`
requires existing binary consumers to rebuild and relink.

## Non-null data model boundary

No non-null model is selected by this document. A follow-up ADR must compare
at least a CMeta-native model and an external standards-compatible evaluator.
The chosen model must define condition, location, value, and script languages;
include `In(id)`; and document its exact SCXML model name and conformance claim.

The data model needs two lifetimes:

- immutable compiled expression/location handles owned by the program; and
- mutable per-session data owned by the session and transactionally staged
  with the native Statechart microstep.

State mutations cannot live in an independent side table. Either the native
Statechart gains managed CMeta state copy/move/destroy support, or an equally
strong prepare/commit/discard hook makes the session store part of the same
microstep transaction. The former is preferred because guards and later
actions already consume the staged native extended state.

`foreach` evaluates its array once into an owned shallow snapshot. The snapshot
has a hard item/byte bound and remains valid through all child steps. Item and
index bindings are assigned before each iteration; a child error stops the
loop and the enclosing block. No borrowed DataBind/CSerde view may cross an
append, assignment, callback return, or coroutine/executor suspension.

System variables are read-only provider bindings:

- `_sessionid` and `_name` are fixed for one session;
- `_event` changes only when an internal or external Event is selected and
  remains bound until the next selected Event;
- `_ioprocessors` is derived from the installed adapter capabilities; and
- `_x` contains only platform-specific provider state.

CSerde/CBind can encode/decode typed values at Event I/O boundaries. They do
not implement expressions or mutable locations. TurboParser DataBind can be an
optional downstream codec/owning-value adapter after checking ABI 8 and schema
identity, but it cannot become a TurboUtils dependency or a second mutable
Statechart fact source.

## Compatibility and migration

- Existing null/core documents and direct program bindings retain current
  behavior and IDs.
- Newly accepted adapter-dependent documents require the owning session API.
- Existing `cflow_scxml_compile()` remains the null-profile entry point. A new
  options-based compile entry is required only when a non-null provider is
  selected; there is no implicit provider fallback.
- Native CFlow remains free of XML, tlog, CSerde, CBind, DataBind, and transport
  knowledge. Optional generic runtime hooks depend only on CFlow Event and
  Statechart types.
- The SCXML optional target does not gain a TurboParser dependency. Event data
  codec bridges are separate targets following the existing
  `JsonCSerdeAdapter` pattern.
- Every versioned public struct change requires C/C++ header consumers,
  installed-package tests, ABI/version checks, and release notes.

## Risk assessment

- **HIGH — external side effects:** direct adapter calls before microstep
  commit can publish effects that survive rollback. The reservation protocol
  and exact ticket terminal state are mandatory.
- **HIGH — data ownership:** placing non-null mutable values in a session side
  table would create a second fact source and break rollback. Managed native
  state or an equivalent shared transaction is required before `assign`.
- **HIGH — invocation ordering:** starting an invoke during `onentry` violates
  the stable-configuration rule and can leak services from transient states.
  Activation must use the quiescent hook.
- **HIGH — error queue priority:** adapter errors delivered through the
  external mailbox change transition selection order. They require bounded
  internal ingress.
- **MED — public ABI:** session, adapter, and native hook structs are additive
  source APIs but create new ABI contracts. Version/size validation and
  installed consumers are required.
- **MED — retained resources:** delayed sends, prepared tickets, invocation
  rows, iterable snapshots, and adapter completions all need independent hard
  capacities, peak accounting, and deterministic close/drain behavior.
- **MED — dependency direction:** linking TurboParser DataBind from
  TurboUtils would create the forbidden package reversal. Any such bridge must
  remain downstream.
- **LOW — interpreter cost:** indexed inline dispatch adds one branch per
  executed step. It is not a demonstrated hot-path problem; optimize only
  after profiling.

## Implementation increments

### A. Session, recoverable errors, literal `send` and `cancel`

**Implementation status:** complete.

- Add internal program requirements and per-session binding ownership.
- Add bounded internal adapter ingress and the v1 Event I/O reservation API.
- Admit literal-only null-model send/cancel forms, including canonical
  `#_internal` and the documented `_internal` compatibility spelling.
- Implement block-local abort and exact error Event ordering.
- Preserve the legacy adapter-free runtime path.

### B. `invoke` and `finalize`

**Implementation status:** complete for the literal null-data-model profile.

- Add native quiescent and external-preprocess hooks.
- Add the versioned invoke adapter and bounded invocation registry.
- Start only stable invocations; cancel committed exits; route returned Events
  through the owning finalize block before transition selection.
- Reject finalize content that can raise Events or invoke external effects.

### C. Data model prerequisite

- Write the separate model-selection ADR.
- Add managed Statechart state/Event support or prove an equivalent unified
  transactional store.
- Define expression compilation, runtime evaluation, ownership, and system
  variable bindings without linking TurboParser.

### D. `assign`, `foreach`, and system variables

- Admit them only for the selected non-null model.
- Add transactional assignment and owned shallow iterable snapshots.
- Enforce step/item/byte budgets and precise `error.execution` behavior.
- Add CSerde/CBind or downstream DataBind bridges only at explicit data
  boundaries.

### E. Conformance and claim

- Run a W3C-derived corpus per advertised data model and Event I/O capability.
- Document mandatory, optional, and unsupported features separately.
- Keep the full-conformance checkbox open until the selected corpus and exact
  processor claim pass on Windows, Linux, and macOS.

## Verification plan

Focused tests must prove:

- document-order execution and block-only abort for both error names;
- earlier assignment/effect preservation and absence of later block steps;
- every prepared ticket reaches exactly one commit/discard terminal state;
- queue-full/cancel races publish no uncommitted external effect;
- `#_internal`/`_internal`, delayed send, cancellation scope, ID uniqueness,
  and ordering;
- invoke activation only after stability, cancel-on-exit, late-result rejection,
  finalize-before-guard ordering, and parallel invocation isolation;
- managed state/event rollback and exactly-once destruction;
- foreach shallow-copy behavior, stable iteration order, limits, and child
  error termination;
- system-variable binding timing and read-only enforcement;
- existing raise/conditional/log fixtures and native Statechart tests;
- SCXML enabled/disabled installed consumers and C/C++ public headers; and
- complete MSVC Release plus Linux/macOS CI before issue checkboxes change.

## Rollback strategy

Each increment is independently gated by admitted syntax. Reverting an
increment restores compile-time rejection for its elements without changing
the legacy native Statechart or adapter-free SCXML path. No migration of
persisted data is involved. A versioned adapter capability remains disabled
unless the session config supplies it explicitly; there is no automatic
fallback to a different transport, data model, or serializer.
