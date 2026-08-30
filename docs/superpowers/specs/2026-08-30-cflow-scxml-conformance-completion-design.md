# CFlow SCXML conformance-completion design

## Scope

This change closes the remaining semantic gaps that block a useful selected
SCXML 1.0 `cmeta` profile:

- legal multi-target state specifications on transitions and root/default
  initial declarations;
- SCXML event-descriptor exact, token-prefix, `.*`, and `*` matching;
- the complete read-only `_event` scalar surface, retained through the
  event's following eventless microsteps, plus the supported SCXML entry in
  `_ioprocessors`;
- expression forms of `send`, `cancel`, and `invoke`, together with bounded
  scalar `content` for same-session internal sends;
- early CMeta `<data>` initialization and scalar `<donedata>` on completion;
- a machine-readable W3C-derived fixture manifest that distinguishes PASS,
  UNSUPPORTED, and N/A.

The `cmeta` data model remains a bounded scalar/reflected-state model. This
work does not add ECMAScript, XPath, BasicHTTP, arbitrary script execution, an
unbounded object graph, or a transport implementation. Tests whose assertion
requires one of those optional models or processors are N/A, not silently
approximated.

## Compatibility boundary

The existing `cflow_statechart_transition`,
`cflow_statechart_definition`, `cflow_statechart_build`, mailbox API,
SCXML v1 adapters, and SCXML v1 session initialization retain their current
layout and behavior.

Multi-target input is additive:

```c
typedef struct cflow_statechart_transition_target {
    cflow_statechart_transition_id transition;
    cflow_machine_state_id target;
    uint32_t order;
} cflow_statechart_transition_target;

typedef struct cflow_statechart_definition_v2 {
    uint32_t abi_version;
    size_t struct_size;
    cflow_statechart_definition base;
    const cflow_statechart_transition_target *transition_targets;
    size_t transition_target_count;
} cflow_statechart_definition_v2;
```

`cflow_statechart_build_v2` accepts this table. A transition either uses the
legacy `target` field or a v2 target span, never both. Existing builds are
normalized to a zero-or-one target span. New count/index queries expose the
normalized target set without changing the legacy row query; for a v2
multi-target row, the legacy `target` query remains the first document-order
target for diagnostic/source compatibility.

SCXML event additions are independently versioned. Old session entry points
still accept the event returned by `cflow_scxml_program_event` and boolean
payload views. `cflow_scxml_session_try_send_v2` adds bounded borrowed metadata
which the session copies into a private token-addressed row before mailbox
admission. Existing Event I/O and invoke adapter ABIs remain unchanged.

## Native multi-target semantics

The immutable IR owns two arrays:

- `transition_target_offsets[transition_count + 1]`;
- `transition_target_indices[transition_target_count]`.

Every index is a dense state index. Build validates nonzero IDs, referenced
transitions/states, unique `(transition, order)` and `(transition, target)`,
checked limits, and legal state specifications. No transition may mix legacy
and v2 target facts.

The transition domain is the source for an internal transition from a
compound source only when every target is a proper descendant. Otherwise it
is the least common compound ancestor of the source and all targets. A
targetless transition has no exit domain.

During entry the runtime first activates paths for every selected target,
then performs default descendant expansion once. This ordering is required:
expanding the first target immediately could enter a sibling region's default
before a later explicit target selects that region. History restoration joins
the same two-phase path-then-default procedure. Entry and exit ordering remain
derived from document order and depth.

## Event descriptors

SCXML uses a finite admitted event schema. The compiler collects concrete
event names from exact descriptors, `raise`, literal `send`, invoke completion,
and required platform events. Each transition descriptor is expanded against
that finite schema after name normalization:

- `*` matches every admitted named event;
- `x`, `x.`, and `x.*` match `x` and names beginning with `x.`;
- matching is case-sensitive and token-boundary aware;
- multiple descriptors are a union and duplicate matches emit one native row.

This is exact for the program's bounded event universe. An external event name
must still be admitted by `cflow_scxml_program_event`; the v2 named-event
session API supplies metadata but does not extend the runtime schema.

## SCXML event envelope and system variables

SCXML program events retain their existing trivially-copyable payload type.
The session owns a bounded metadata-row registry containing:

- event name;
- `type` (`platform`, `internal`, or `external`);
- `sendid`, `origin`, `origintype`, and `invokeid`;
- scalar data represented as bounded UTF-8 text.

Lengths and token arithmetic are validated before publication. No retained
field contains caller-owned pointers. Metadata rows are reserved before
mailbox admission and released when their tagged event is observed or when
admission/effect staging rolls back.

The session's `last_event` is the single system-variable fact source. A
runtime v2 observation hook copies the selected envelope before guard/action
evaluation. Eventless microsteps do not clear it. A later selected event
replaces it. Initial eventless stabilization has no event value. Internal
`raise`, internal `send`, adapter/platform error, invoke return, and state
completion construct their correct event type and metadata at their existing
semantic boundary.

The CMeta evaluator exposes `_event.name`, `.type`, `.sendid`, `.origin`,
`.origintype`, `.invokeid`, and `.data` as read-only scalar/string values.
Missing optional string fields are valid empty strings. `_ioprocessors.scxml`
is a read-only generated session location string.
Bare object reads and all system-variable writes remain invalid.

## Dynamic executable content

Literal and expression attributes are mutually exclusive as required by
SCXML. CMeta expressions are compiled at program admission and evaluated once
against the staged state immediately before the owning operation:

- `eventexpr`, `targetexpr`, `typeexpr`, and `delayexpr` for `send`;
- `sendidexpr` for `cancel`;
- `typeexpr` and `srcexpr` for `invoke`.

String attributes require string results. Delay accepts a nonnegative integer
number of milliseconds or the existing literal duration syntax.

This selected profile admits one `content expr` scalar only for immediate
same-session internal sends; it is rendered into bounded UTF-8 `_event.data`.
`idlocation`, `namelist`, `param`, external payload adapters, and text/object
content remain explicit unsupported features. Expression or capacity failure
raises `error.execution` and aborts the current executable block without
committing its remaining effects.

## Data initialization and donedata

`<data id="location" expr="value"/>` compiles through the existing CMeta
location and value machinery. `src` and mixed/text object initialization are
unsupported with a source diagnostic because this profile has no resource
loader or general object parser.

For omitted or `binding="early"`, root and state data initializers execute in
document order on a cloned initial CMeta state before the native instance is
published; failure destroys the clone and leaves the caller state untouched.
`binding="late"` remains explicitly unsupported because this change does not
add per-state initialization markers or a re-entry transaction protocol.

`<donedata>` on a final state uses the same scalar payload builder as `send`.
The SCXML completion observation hook names native completion as
`done.state.<parent>`, evaluates the active final state's scalar payload, and
binds it to `_event.data` before completion guards run. Exact, prefix, and
wildcard descriptors are expanded to native completion triggers; native CFlow
completion records remain format-neutral.

## Failure, capacity, and shutdown

Every growable compile-time count uses checked arithmetic and existing SCXML
limits. Runtime payload entry count, text bytes, effect journal rows, delayed
sends, invocation rows, mailbox slots, and event metadata rows all have hard
bounds. Full, invalid-type, closed, cancelled, expression, and adapter failures
remain distinguishable at their current API boundaries.

Prepared adapter effects remain move-only: exactly one commit or discard.
Event metadata is copied into bounded session-owned rows and released after
observation or rollback. Session destroy remains quiescent-only.

## Corpus and conformance claim

`tests/w3c/manifest.tsv` is the test corpus fact source. Each row records test
ID, local fixture, applicability (`MANDATORY` or `OPTIONAL`), status (`PASS`,
`UNSUPPORTED`, or `N/A`), feature, upstream URL, and rationale. The harness
parses it strictly, rejects duplicate IDs/files and unknown statuses, and
requires every PASS fixture to exist. Named regression cases execute all
current PASS fixtures.

The README prose mirrors the manifest facts. A
PASS means the named transformed assertion passes; it is not W3C
certification. UNSUPPORTED names a concrete missing selected-profile feature.
N/A is reserved for optional data models/processors that this implementation
does not claim.

## Migration and rollback

Existing native and SCXML callers require no changes. Callers use the v2
session entry point only when they need event metadata; existing adapter ABIs
remain unchanged. Disabling `CFLOW_ENABLE_SCXML` still removes the entire frontend.

The implementation is separable by layer: v2 target rows, private envelope,
dynamic content, data/donedata, and corpus manifest. Each layer has focused
tests and can be reverted independently without changing stored user data or
wire formats.
