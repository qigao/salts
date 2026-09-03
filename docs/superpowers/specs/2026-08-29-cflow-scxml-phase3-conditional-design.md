# CFlow SCXML Phase 3 Conditional Executable Content Design

**Issue:** [qigao/salts#122](https://github.com/qigao/salts/issues/122)

**Date:** 2026-08-29

**Reference semantics:** [W3C SCXML 1.0](https://www.w3.org/TR/scxml/)

## Scope

This increment adds conditional executable content to the optional
`Salts::CFlowScxml` frontend while retaining the SCXML null data model.
It admits nested `if` elements partitioned by zero or more `elseif` markers and
an optional trailing `else`, resolves null-data-model `In(id)` conditions at
compile time, and executes only the first matching partition in document order.

The increment does not add a general expression engine. W3C SCXML 1.0 Appendix
B.1 defines the null data model's boolean expression language as the `In(id)`
predicate only. It also makes system variables inaccessible and leaves
`foreach`, assignment, value expressions, location expressions, and scripting
unsupported. Those Phase 3 items remain fail-fast unsupported features.

Transition `cond` uses the same condition language but participates in
transition selection rather than executable-block evaluation. This increment
establishes a reusable configuration-query boundary but keeps transition
condition lowering out of scope so that the issue's conditional executable
content checkbox remains one atomic change.

## Standards-derived behavior

The following are facts from the W3C recommendation:

- `if` owns a first partition, zero or more `elseif` partitions, and at most
  one `else` partition; `else` must follow every `elseif`.
- The processor executes the first condition-bearing partition whose condition
  is true, otherwise the `else` partition, otherwise no child content.
- Partitions may be empty, nested `if` elements are legal, and executed content
  retains document order.
- During state exit, an `onexit` block runs before that state is removed from
  the current configuration. During entry, the state is added before its
  `onentry` block runs. Initial-transition content follows the owning state's
  `onentry` block.
- A syntactically ill-formed expression may be rejected at document load time.
  This frontend chooses deterministic compile-time rejection because the null
  expression grammar is closed and every state identifier is known.

Consequently `In(id)` cannot be evaluated from the final target configuration
or from a program-owned mirror. It must observe the runtime's action-time
configuration, including the incremental removal and addition of states.

## Existing boundaries and evidence

- `cflow_statechart_instance` is the sole mutable fact source for active
  configuration, history, queues, extended state, and first error.
- The existing `cflow_statechart_executable_fn` receives phase, owner, extended
  state, Event, staged output, and bounded internal-raise access. It receives no
  configuration view or query.
- `cflow_scxml_program` owns immutable block steps and callback users and may be
  shared by multiple runtime instances. It therefore cannot retain a pointer to
  one instance or maintain per-instance active-state bits.
- The runtime currently builds a complete staged target configuration before
  entry actions and invokes initial/history transition actions before all entry
  actions. That is sufficient for the existing `raise` subset but cannot expose
  exact W3C `In(id)` observations during entry or initial content.

## Alternatives

### Mirror active states in the SCXML null extended state

Rejected. Synthetic entry/exit callbacks could update a bitset in the extended
state, but the bitset would duplicate the instance-owned configuration. States
without user executable content would require hidden actions, initial/history
ordering would still be wrong, and rollback would have two independently
maintained representations of the same semantic state.

### Let the SCXML frontend call public instance snapshot APIs

Rejected. Binding rows are created before an instance exists, program bindings
may be shared by several instances, and initialization callbacks run before the
instance handle is published. Calling a locking public instance API from the
single-owner callback would also cross the runtime's control/data boundary.

### Add a generic string condition evaluator

Rejected for the null data model. It introduces an unnecessary expression
language, still lacks exact active-configuration access, and would move
statechart semantic state into an adapter. A later non-null data model may add a
versioned evaluator for its own language without changing the `In(id)` fact
source.

### Add a contextual executable binding

Chosen. Preserve the legacy callback and add an optional contextual callback
to `cflow_statechart_executable_binding`. The runtime supplies a call-scoped
context containing the existing action inputs plus a read-only `is_active`
function over its action-time working configuration. Exactly one callback kind
must be present in each binding.

This is the smallest design that keeps configuration ownership in the runtime,
keeps CFlow independent of XML, and gives later frontends a format-neutral
configuration predicate.

## Public native contract

The public Statechart runtime header gains a call-scoped query and contextual
callback without changing the signature of existing callbacks:

```c
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

typedef bool (*cflow_statechart_contextual_executable_fn)(
    void *user,
    const cflow_statechart_executable_context *context,
    const char **out_error);
```

`cflow_statechart_executable_binding` appends a contextual callback field after
the existing `id`, `fn`, and `user` fields. Existing three-field source
initializers therefore retain their meaning and initialize the new field to
NULL. Runtime binding admission requires exactly one of `fn` and the contextual
callback to be non-NULL. The struct-size change is an ABI change for binaries
compiled against the old header, so the release notes and ABI checks must make
that risk explicit even though source compatibility is preserved.

The context and both callback users are borrowed only until callback return.
`is_active` returns true only for a known real state present in the action-time
configuration. The frontend resolves state names during compilation, so an
unknown or pseudo ID indicates a contract violation rather than a recoverable
SCXML expression error.

## Action-time configuration and ordering

The instance remains the only configuration owner. During a microstep it
maintains a bounded working bitset derived from the published configuration:

1. each exiting state's `onexit` span observes that state as active; the
   runtime clears it after the span completes;
2. transition content observes the configuration after all selected exits;
3. immediately before one entering state's action span, the runtime marks that
   state active;
4. after that state's entry span, the runtime executes any selected default
   initial/history transition content owned by the state;
5. later entering states observe all earlier entries but not future entries.

Initial stabilization starts with an empty action-time configuration and uses
the same entry procedure. A fixed state-indexed pseudo-transition lookup avoids
an O(states * pseudo-transitions) scan. Its bytes are included in checked
instance storage requirements and the existing configurable storage ceiling.

The final staged configuration remains the commit artifact. The working bitset
is callback-observation state only; it is rebuilt from existing canonical
inputs and never published independently. Callback or bounded-queue failure
still discards staged state, configuration, history, completion rows, and
raised Events at the existing transaction boundary.

## Conditional block IR

The SCXML compiler extends its immutable block IR with structured steps:

- `RAISE(event_id)` retains its current behavior;
- `IF(first_branch, branch_count)` selects one branch;
- each branch contains either a resolved real-state ID for `In(id)` or an
  unconditional `else` marker and a contiguous child-step span.

Nested blocks are represented by indices rather than retained XML nodes or raw
pointers. Counts, indices, and allocation products use checked arithmetic and
remain bounded by the XML node limit, native action-reference limit, and the
existing program allocation path. Execution uses a recursive walk whose depth
is checked against the program-owned maximum conditional depth derived during
admission. That maximum is itself bounded by the admitted XML depth, so no
unbounded executable-content recursion is accepted.

The compiler accepts the null grammar `In(id)` with optional XML whitespace
around tokens. The state argument is one XML NCName naming a declared real
state. Quoted arguments are not part of Appendix B.1's null grammar and remain
rejected; they belong to expression languages such as ECMAScript. Empty,
malformed, unknown, or pseudo-state arguments fail at the owning `cond`
attribute with `CFLOW_SCXML_INVALID_STRUCTURE` or `CFLOW_SCXML_UNKNOWN_TARGET`,
as appropriate.

## XML structure and diagnostics

`if` is executable content. `elseif` and `else` are empty partition markers
legal only as direct children of `if`. Admission requires:

- `if` and every `elseif` have exactly one nonempty unqualified `cond`;
- `else` has no unqualified attributes;
- at most one `else`, placed after all `elseif` markers;
- markers have no children or non-whitespace text; and
- every partition contains only currently supported executable content.

The first deterministic failure is reported at the offending element or
attribute. Foreign-namespace attributes retain the existing extension-metadata
rule. No malformed condition is silently converted to false because the
frontend chooses the specification's compile-time rejection option.

## Compatibility and migration risks

- **HIGH — public ABI:** appending a field changes
  `sizeof(cflow_statechart_executable_binding)`. Source initializers remain
  compatible, but previously built consumers must relink. Verify public-header
  compilation, installed consumers, and ABI metadata/release notes.
- **HIGH — callback-visible ordering:** interleaving initial/history actions
  with entry actions corrects behavior toward the existing W3C-derived
  Statechart contract, but native callers may have observed the current order.
  Add exact trace tests before changing implementation and document the
  corrected order.
- **MED — storage:** one additional bounded working bitset and one state-indexed
  lookup increase instance bytes by O(states). Include both in storage queries,
  max-storage rejection tests, and overflow tests.
- **MED — parser strictness:** accepting only Appendix B.1 `In(id)` rejects
  quoted or compound expressions accepted by optional data models. Diagnostics
  must state that the compiled program uses the null data model.
- **LOW — performance:** each `In(id)` query performs a binary state-ID lookup
  followed by one bit test, O(log states). Conditions are executable control
  flow rather than the transition-selection hot loop; optimize only if measured.

## Verification

Native Statechart tests must establish legacy-binding compatibility, contextual
binding admission, call-scoped `is_active`, exact exit/transition/entry
observations, initial/history interleaving, rollback, storage accounting, and
unknown-state behavior. Existing hierarchy adapters must remain unchanged.

Focused SCXML tests must establish first-true selection, `else`, no-match,
empty partitions, nested conditions, document order, use under every supported
block owner, state observations during exit/transition/entry/initial/history,
malformed structure and condition diagnostics, and queue-full rollback from a
selected branch. Existing `raise` fixtures must remain green.

Validation order is focused SCXML, native Statechart runtime, complete CFlow,
installed consumers with SCXML enabled and disabled, then the repository MSVC
Release suite. Linux and macOS CI remain required before the issue checkbox is
updated.
