# CFlow SCXML Transition Condition Design

**Issue:** [qigao/turbo-utils#122](https://github.com/qigao/turbo-utils/issues/122)

**Date:** 2026-08-29

**Reference semantics:** [W3C SCXML 1.0](https://www.w3.org/TR/scxml/)

## Scope

This increment admits the SCXML null-data-model transition condition
`cond="In(id)"`. It resolves the named state during compilation, lowers each
conditioned transition row to a native Statechart guard, and evaluates that
guard against the immutable published configuration used for deterministic
transition selection.

The increment does not add a general expression evaluator, system variables,
assignment, iteration, logging, or a non-null data model. Initial and history
default transitions remain unconditional because the native pseudo-transition
contract requires a zero guard and does not run ordinary selection.

## Existing boundaries and evidence

- `cflow_statechart_instance` is the sole mutable source of active
  configuration state.
- Native selection already evaluates every declared guard against the same
  published extended-state snapshot before conflict filtering.
- The legacy guard callback receives extended state and an optional Event but
  cannot query the active configuration.
- The runtime already exposes a call-scoped `cflow_statechart_is_active_fn` to
  contextual executable callbacks; it can be reused without exposing a bitset.
- `cflow_scxml_program` owns immutable executable bindings and their callback
  users. The same lifetime boundary can own generated guard bindings.
- The compiler already parses and resolves the exact null grammar `In(id)` for
  `if` and `elseif` executable content.

## Alternatives

### Mirror configuration in the null extended state

Rejected. It creates a second fact source, requires hidden actions for every
state, and can diverge from selection-time configuration during failure or
rollback.

### Make the native runtime understand SCXML conditions

Rejected. XML syntax and state names belong to the optional frontend. The
format-neutral runtime should receive resolved numeric IDs and callbacks only.

### Add a contextual guard callback

Chosen. The runtime supplies a borrowed context containing the existing guard
inputs and a read-only query over the published configuration. The SCXML
frontend resolves names and provides a program-owned callback user containing
only the target numeric state ID.

## Public native contract

The Statechart runtime header adds:

```c
typedef struct cflow_statechart_guard_context {
    const void *state;
    const cflow_event_view *event;
    cflow_statechart_is_active_fn is_active;
    void *configuration_user;
} cflow_statechart_guard_context;

typedef bool (*cflow_statechart_contextual_guard_fn)(
    void *user,
    const cflow_statechart_guard_context *context,
    bool *out_enabled,
    const char **out_error);
```

`cflow_statechart_guard_binding` appends `contextual_fn` after the existing
`id`, `fn`, and `user` fields. Admission requires exactly one callback kind.
Existing three-field source initializers retain their meaning and zero the new
field, but the row-size change requires every binary consumer to recompile and
relink.

The context and its members are borrowed only for the callback. `state` is the
published extended-state value. `event` is non-NULL only for an Event trigger.
`is_active` returns true only for a known real state in the same published
configuration used by the current selection pass; unknown and pseudo IDs
return false. A contextual callback obeys the existing success, enabled, error,
effect, and no-retention rules.

## SCXML lowering and ownership

`validate_element_attributes` admits `cond` on ordinary transitions. During
analysis, a present condition must parse as the exact null grammar `In(id)`;
malformed expressions fail at the condition attribute. Conditions on initial
or history default transitions fail as invalid structure.

After state-name normalization, lowering resolves the condition to a declared
real state. Unknown names fail with `CFLOW_SCXML_UNKNOWN_TARGET`; initial and
history pseudo-state names fail with `CFLOW_SCXML_INVALID_STRUCTURE`.

Each native row produced from a conditioned SCXML transition gets one guard ID,
declaration, callback binding, and immutable callback user containing the
resolved state ID. Multi-event transitions therefore have one bounded row per
event token, matching the existing transition expansion. Guard counts never
exceed admitted transition rows and remain subject to native Statechart limits
and checked allocation arithmetic.

The native guard declaration uses the null state type, pure effects, and stable
non-aliasing properties. The callback copies no configuration and performs one
numeric state lookup plus one bit test through the runtime query.

The owning program retains guard bindings and callback users until
`cflow_scxml_program_destroy()`. The new accessor is:

```c
bool cflow_scxml_program_guard_bindings(
    const cflow_scxml_program *program,
    const cflow_statechart_guard_binding **out_bindings,
    size_t *out_count);
```

It follows the executable-binding accessor: returned rows and callback users
are borrowed, invalid arguments do not modify outputs, and the program must
outlive every configured instance.

## Selection and error semantics

All guards in one selection attempt observe the same published configuration
and extended state. A false `In(id)` skips that transition and permits the
existing leaf-to-ancestor and priority-ordered search to continue. A true
condition participates in existing conflict filtering without changing
priority or document order.

Eventless, Event, and completion transitions use the same condition callback.
Callback failure retains the existing terminal `GUARD_FAILED` path, although
the generated SCXML callback has no expected runtime failure. Compile-time
syntax/name failures never become runtime false values.

## Compatibility and risks

- **HIGH — public ABI:** `sizeof(cflow_statechart_guard_binding)` changes.
  Source initializers remain compatible; previously built consumers must
  recompile and relink. Verify public headers and installed consumers.
- **MED — selection semantics:** accepting `In(id)` changes a previously
  rejected SCXML document into an executable program. Verify false guards fall
  through to later candidates and true guards preserve priority.
- **MED — ownership:** generated binding users are borrowed from the program.
  Verify destruction boundaries and document that instances cannot outlive the
  program.
- **LOW — resource cost:** conditioned rows add O(conditioned transitions)
  declarations and binding users. Counts and byte products remain bounded and
  checked.

## Verification

Native runtime tests cover legacy three-field bindings, contextual-only
admission, neither/both rejection, event/eventless/completion contexts,
published configuration queries, unknown/pseudo IDs, and legacy failure
behavior.

SCXML tests cover exact and whitespace `In(id)`, malformed/quoted/unknown/
pseudo conditions, forbidden pseudo-default conditions, guard accessors,
multi-event expansion, true selection, false fallthrough, ancestor fallback,
parallel active regions, completion, eventless stabilization, and program
lifetime documentation. Package tests compile and run an installed consumer
with generated guard bindings. Focused tests precede complete CFlow and
repository Release verification.
