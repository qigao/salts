# CFlow SCXML CMeta Data Model Selection

Status: Accepted for Increment C foundation

## Context

Issue #122 leaves the first non-null SCXML data model as an explicit design
decision. The SCXML Recommendation requires a conforming processor to support
the null data model and permits additional named models. A non-null model must
define its boolean expression language (including `In(state-id)`), location and
value expressions, script behavior, assignment behavior, system variables, and
`foreach` semantics.

The current implementation already has a strict null model. Its native CFlow
Statechart stores one typed extended-state value in a two-buffer transaction,
but currently admits only trivial byte-copyable state. CMeta describes typed C
data and lifecycle traits; CSerde and CBind translate structured external data
into those types. None of those modules is independently an expression engine.

This decision affects CFlow, CFlowScxml, CMeta, and the CSerde/CBind adapter
boundary, so it is recorded separately from the remaining Phase 3 roadmap.

## Decision

Add a TurboUtils extension named `datamodel="cmeta"`. Keep omitted or explicit
`datamodel="null"` unchanged. Do not claim ECMAScript, XPath, or W3C Basic data
model conformance for the extension.

The CMeta model uses one application-supplied C object as the session data
store. Its `cmeta_type_desc` defines copy/move/destroy ownership and its
`cmeta_data_desc` defines reflected fields and containers. The published native
Statechart state is the sole fact source; there is no SCXML-side variable map or
side table. Every microstep copies the published object into a staged object,
executes all mutations there, and either publishes the complete object or
destroys it on rollback.

### Language contract

Version 1 of the model defines these languages:

- Boolean expressions: boolean literals, reflected scalar locations,
  comparison operators, `&&`, `||`, `!`, parentheses, and
  `In("state-id")`. Evaluation is side-effect free and bounded by compiled
  instruction and stack limits.
- Location expressions: an identifier followed by reflected field selectors
  and bounded sequence indices, for example `order.total` or `items[2].price`.
  Map-key and pointer traversal are not admitted in version 1.
- Value expressions: null, boolean, signed integer, unsigned integer, floating
  point, and string literals; reflected location reads; and bounded array
  literals whose element type is determined by the destination.
- Script language: none. `<script>` is rejected at build time for the CMeta
  model. Executable content remains the structured SCXML actions supported by
  the adapter.

Expressions compile once into immutable bounded programs. Runtime evaluation
does not parse text or allocate an unbounded container. Invalid syntax,
unresolved fields, type mismatches, arithmetic overflow, and configured limit
violations fail fast.

### Assignment, iteration, and system variables

`<assign>` resolves its location against the reflected staged state and commits
only if conversion to the exact destination type succeeds. An assignment error
raises internal `error.execution` and aborts the enclosing executable-content
block without publishing partial state.

`<foreach>` accepts reflected sequence values only. It snapshots the sequence
length and stable element order at entry, shallow-copies each element into the
declared item location, writes the optional index location, and executes the
body transactionally. The configured iteration limit is a hard ceiling. An
invalid collection, item/index location, or conversion raises
`error.execution` and aborts the enclosing block.

The SCXML system variables are read-only views owned by the session runtime:

- `_event` is projected from the current borrowed event and is unavailable
  outside event-triggered work.
- `_sessionid` and `_name` are immutable session strings.
- `_ioprocessors` is a read-only reflected view of registered I/O processors.

User assignments to system variables are rejected. All borrowed system views
expire when the enclosing guard or executable callback returns.

### CSerde and CBind boundary

CSerde is the format-neutral token protocol and CBind is the typed decode
adapter. They may decode `<data>` or `<content>` structured literals into a
known reflected destination. They do not own the data model, evaluate
expressions, resolve locations, or maintain session state. Parser-specific DOM
types and errors must not cross into CFlow or CMeta.

## Alternatives

### ECMAScript

Rejected for this increment. The SCXML ECMAScript model requires a complete
ECMAScript environment and script semantics. TurboUtils has no such runtime;
adding one would introduce a new dependency, execution sandbox, resource
accounting, and deployment surface substantially larger than Issue #122.

### XPath

Rejected for this increment. The public XML parser facade exposes an immutable
parse tree, while the vendored cxml XPath implementation relies on private
mutable types and global parser state. The W3C XPath data-model document is a
Working Group Note rather than the Recommendation's required model. Exposing
that implementation would couple the Statechart runtime to a parser vendor and
would not provide the required thread-safe mutable store.

### Dynamic SCXML-owned value map

Rejected. It would duplicate CMeta's type facts, make host C state and SCXML
state independently writable, and require a synchronization or conversion step
on every callback. Rollback would also span two owners instead of the native
Statechart transaction.

### Trivial fixed-capacity byte arena

Deferred. An offset-based arena could reuse the current `memcpy` transaction,
but it would create a second dynamic type system and complicate reclamation of
reassigned strings and arrays. Managed CMeta state reuses the lifecycle protocol
already exercised by CFlow streams and compiled plans.

## Compatibility and dependency impact

- Existing null-model documents, errors, and generated bindings remain
  unchanged.
- `datamodel="cmeta"` is opt-in and exact; unsupported model names continue to
  fail at build time.
- CFlow gains managed extended-state ownership, but Event payload admission
  remains trivial until a separate mailbox-lifecycle change.
- The existing byte-copy snapshot API remains valid for trivial state and
  returns false for managed state. No raw shallow copy of a managed object is
  exposed. A later executor-owned snapshot API requires a separate public API
  decision.
- CFlowScxml may depend on CMeta, CSerde, and CBind through TurboUtils targets;
  CFlow and CMeta must not depend on parser modules.

## Failure and rollback protocol

The published state slot is authoritative. The alternate state slot and action
scratch slot are owned transaction resources. Before a microstep, the runtime
copy-constructs the published value into the alternate slot. Each successful
action fully constructs its output; the previous action value is destroyed
exactly once when its slot is reused. Failed callbacks must leave their output
uninitialized. On action, copy, cancellation, or completion-queue failure, all
live transaction slots are destroyed and the published slot is unchanged. On
commit, exactly one staged value becomes published. Instance destruction
destroys every live slot exactly once.

## Verification

The foundation must cover:

- managed initial-state copy success and copy failure;
- multiple actions alternating between staged and scratch slots;
- action failure, cancellation-before-commit, and instance destruction;
- unchanged trivial-state snapshots and null-model regression tests;
- ASan-enabled Windows development tests and the existing adjacent CFlow and
  CFlowScxml test targets.

References:

- SCXML Recommendation: <https://www.w3.org/TR/scxml/>
- SCXML XPath Data Model Working Group Note:
  <https://www.w3.org/TR/scxml-xpath-dm/>
