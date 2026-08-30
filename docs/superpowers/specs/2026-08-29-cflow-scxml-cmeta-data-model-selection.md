# CFlow SCXML CMeta Data Model Selection

Status: Accepted for Increment C foundation

## Implementation status

The managed CMeta extended-state lifecycle is implemented in CFlow.
CFlowScxml now publicly admits exact `datamodel="cmeta"` documents through
versioned compile and session provider structs. The immutable program borrows
the root schema and owns bounded QueryVM programs for transition `cond`
expressions; each session's initial object is copied into the native managed
Statechart state and is not retained. Existing null compile/session entry
points remain null-only.

The scalar-expression implementation compiles Boolean, numeric, enum,
borrowed string, reflected struct-field, comparison, logical, and
`In("state")` expressions and evaluates them without runtime allocation.
String literals are retained by the compiled program; string locations use the
public bounded CMeta borrowed-read contract and byte-length-aware comparisons.
Logical conjunction and disjunction short circuit. SCXML condition attributes
are decoded from bounded XML entity/character references before compilation.

Transactional `<assign>` now compiles one bounded scalar expression and one
dotted reflected struct-field destination. It mutates only the native staged
state, requires exact Boolean/numeric/enum/string conversion, and maps runtime
evaluation, conversion, or provider failures to internal `error.execution`.
The enclosing executable block then restores its complete entry snapshot and
stops, so no earlier assignment in that block is partially published.

CMeta `_name` and `_sessionid` are now implemented as bounded read-only scalar
strings. Ordinary onentry, onexit, and transition `<if>`/`<elseif>` conditions
compile through the same bounded Boolean evaluator, observe staged state, and
select the first true partition. A runtime condition error queues
`error.execution`, treats that condition as false, and continues selection.
CMeta conditions inside invocation `<finalize>`, `_event`, `_ioprocessors`,
sequence-index expressions, array literals, and structured CBind/CSerde input
remain separate implementation increments and continue to fail fast.

A private reflected-sequence bridge now pairs the abstract CMeta sequence data
descriptor with the concrete `Struct` layout field generated for
`TYPE(Container, Element)`. It compiles the root-relative container offset and
declared element type without retaining location text. Runtime opening requires
an exact sequence semantic projection, a valid unary container type
application, the declared element type, and a `SIZED | ORDERED` default Range.
It snapshots only the reported length; the Range continues to borrow the staged
root container and detects invalidating mutation through its provider version
contract. Ordinary CMeta executable blocks now expose that bridge through
`<foreach>` for exact element/item types with either trivial storage or complete
`COPY | MOVE | DESTROY` lifecycle traits, plus an optional exact `size_t`
index. The public positive `max_iterations` option bounds every invocation;
legacy v1 option prefixes receive the named default. No private TurboSTL
storage convention is inferred by CFlowScxml.

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
length and stable element order at entry, replaces the declared item with each
exact element value, writes the optional index location, and executes the body
transactionally. Managed Range values are independently constructed in one
invocation-local aligned scratch slot and moved into staged state before the
body executes. The configured iteration limit is a hard ceiling. An invalid
collection, item/index location, allocation, or conversion raises
`error.execution` and aborts the enclosing block.

The current implementation admits `<foreach>` only in ordinary transactional
blocks. It resolves array/item/index locations at compile time, validates the
exact `SIZED | ORDERED` Range and length at invocation entry, assigns each
trivial or lifecycle-managed element and zero-based optional index into staged
state, and executes the body in document order. Length overflow, scratch
allocation, provider failure, or child failure raises `error.execution` and
rolls back the whole block. `finalize` iteration remains fail-fast.

The target SCXML system variables are read-only views owned by the session
runtime:

- `_event` is projected from the current borrowed event and is unavailable
  outside event-triggered work.
- `_name` is copied from the optional root `name` NMTOKEN; an omitted name is
  represented by the empty string. Its source bytes participate in the
  program's `max_name_bytes` budget.
- `_sessionid` is one canonical UUID v4 generated before session attachment.
  UUID provisioning failure aborts initialization without publishing the
  session handle.
- `_ioprocessors` is a read-only reflected view of registered I/O processors.

User assignments to system variables are rejected. All borrowed system views
expire when the enclosing guard or executable callback returns.

The current CMeta profile implements `_name`, `_sessionid`, and only the
`_event.name` string field while a native contextual callback carries an
Event. It does not yet retain the last Event into later eventless microsteps or
admit `_event.type`, `sendid`, `origin`, `origintype`, `invokeid`, `data`, or
`_ioprocessors`.
The owning session copies `_name`, stores `_sessionid` inline, and keeps both
stable until successful destruction. Program-level low-level bindings can read
the retained `_name`, but do not have an owning session and therefore fail an
attempt to evaluate `_sessionid`; full system-variable semantics require
`cflow_scxml_session_init_cmeta()`.

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
- CFlowScxml publicly depends on CMeta because `<cflow/scxml.h>` exposes the
  schema provider type. CSerde/CBind are not part of this provider boundary;
  CFlow and CMeta do not depend on parser modules.
- CFlowScxml reuses its existing private TurboUtils Core dependency for UUID
  generation; no public link dependency is added.

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
