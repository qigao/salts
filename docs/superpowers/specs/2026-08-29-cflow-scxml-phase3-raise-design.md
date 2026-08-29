# CFlow SCXML Phase 3 Executable Block and Raise Design

**Issue:** [qigao/turbo-utils#122](https://github.com/qigao/turbo-utils/issues/122)

**Date:** 2026-08-29

**Reference semantics:** [W3C SCXML 1.0](https://www.w3.org/TR/scxml/)

## Scope

This increment establishes the first executable-content path for the optional
`TurboUtils::CFlowScxml` frontend. It admits bounded executable blocks under
`onentry`, `onexit`, and `transition`, compiles the SCXML `raise` element into
the native Statechart executable/action IR, and exposes the immutable native
runtime bindings owned by a compiled `cflow_scxml_program`.

The increment deliberately retains the SCXML null data model. W3C SCXML 1.0
Appendix B.1 specifies that the null data model has no accessible system
variables, location expressions, value expressions, or scripting language and
does not support `foreach` or the data-manipulation elements. Consequently this
increment does not expose `_event`, `_sessionid`, `_name`, `_ioprocessors`, or
`_x`, and does not claim completion of the Phase 3 system-variable or data-model
work. A later data-model design must define those values and their per-session
ownership before they become public.

Conditions, `if`/`elseif`/`else`, `foreach`, `log`, `assign`, `script`, `send`,
`cancel`, `invoke`, `finalize`, and extension executable elements remain stable
unsupported-feature failures. This change is progress within Phase 3; it does
not complete any Phase 3 checkbox in issue #122.

## Existing boundaries

- `cflow_statechart_executable` and the ordered state/transition action rows
  already represent executable work independently of SCXML syntax.
- `cflow_statechart_executable_fn` executes on the single-owner
  `SerialExecutor`, receives immutable input state plus distinct staged output
  state, and can copy internal Events through `cflow_statechart_raise_fn`.
- The runtime commits staged state and raised Events together. Callback failure
  discards both and stops the current microstep.
- `cflow_scxml_program` already owns the immutable Statechart and all name
  mappings, and callers already must keep it alive while a borrowed Statechart
  is used by a runtime instance.

Implementation review found that native initialization built and published the
initial configuration without executing its INITIAL transition actions or
state entry actions. That behavior contradicted the existing callback contract
and prevented standards-compatible SCXML startup. The runtime now posts this
work to the borrowed SerialExecutor, stages state, internal Events, and
completion rows together, then publishes configuration version 1 only after
all initial actions succeed. Failure leaves the public instance empty. This is
a semantic correction without a new native API; the SCXML frontend still
supplies declarations, action references, and bindings only through its
optional target.

## Public API addition

`cflow/scxml.h` includes `cflow/statechart_runtime.h` and adds:

```c
bool cflow_scxml_program_runtime_bindings(
    const cflow_scxml_program *program,
    const cflow_statechart_executable_binding **out_bindings,
    size_t *out_count);
```

On success, the function writes a borrowed contiguous binding view. A program
with no executable content returns success with `*out_bindings == NULL` and
`*out_count == 0`. Invalid arguments return `false` without publishing a
partial view. The bindings and every callback `user` pointer remain valid only
until `cflow_scxml_program_destroy()`.

To run a compiled document, the caller assigns the returned pointer/count to
`cflow_statechart_instance_config.executables` and `.executable_count`, uses
the existing program Statechart and initial-state accessors, and keeps the
program alive until instance destruction. No new wrapper owns or hides the
native CFlow instance lifecycle.

This is a source-compatible API addition but changes the installed public
header dependency from `cflow/statechart.h` alone to also include the runtime
binding type. `TurboUtils::CFlowScxml` already publicly links
`TurboUtils::CFlow`, so package linkage does not gain a new target.

## Executable block IR

One nonempty SCXML block compiles to one native
`cflow_statechart_executable`. Its binding `user` points to an immutable
program-owned block descriptor containing a contiguous sequence of compiled
steps. This increment defines one step opcode, `RAISE`, with a resolved native
event ID.

One block rather than one native executable per XML child preserves the W3C
contract that block elements run in document order and stop after the first
error. It also leaves one bounded interpreter boundary for later conditional
and data-model steps without changing the native Statechart action model.

Each nonempty `onentry` or `onexit` contributes one state-action reference. A
nonempty transition block contributes one transition-action reference for each
native transition row emitted from that XML transition. Thus a transition with
several event descriptors shares one immutable executable block across the
duplicated native rows. Initial and history transition blocks use the same
transition-action lowering and execute in the native INITIAL or HISTORY phase.

The executable declaration uses the null state type, deterministic/no-alias
properties, and stateful/may-fail effects because it stages internal Events and
can fail at the bounded queue boundary. Its callback copies the one-byte null
state witness unchanged before executing steps.

## Raise admission and event mapping

`raise` is legal only as a direct child of an executable block in this
increment. It requires exactly one unqualified `event` attribute whose value is
a nonempty XML `NMTOKEN`; it has no child elements or non-whitespace text.
Unknown unqualified attributes and unsupported executable children fail at
their own source locations. Foreign-namespace attributes remain ignorable
extension metadata, matching the Phase 2 rule.

Raised event names join transition event names in the single event occurrence
stream. Unique IDs remain assigned by first document occurrence, and the
program event-name mapping remains the sole fact source for both external and
internal Events. All raised Events use the existing `cmeta_type_bool` false
payload witness.

The existing XML node limit bounds retained block and step counts. Native
transition/action rows are additionally rejected before publication if they
exceed `CFLOW_STATECHART_MAX_ACTION_REFS`; all additions and allocation sizes
use checked arithmetic. Runtime internal-event capacity remains caller-owned
and explicit. A full queue returns the native
`CFLOW_STATECHART_RUNTIME_INTERNAL_QUEUE_FULL` failure and rolls back the
microstep; it does not allocate or drop an Event.

## Ownership and failure

The compiled program owns block descriptors, steps, binding rows, name bytes,
and the native Statechart. The runtime copies the binding rows but borrows their
`user` pointers, so program destruction is a control-plane operation permitted
only after every using instance is destroyed.

Compilation remains transactional. Any syntax, semantic, limit, allocation,
or native-IR failure frees all temporary executable storage and leaves the
output program empty. Diagnostics retain the existing stable status, exact XML
location, bounded message, and deterministic phase/document order.

## Verification

Focused tests establish:

- empty executable blocks preserve the Phase 2 zero-binding result;
- onentry, onexit, ordinary transition, initial-transition, and
  history-transition blocks lower to ordered native action references;
- several `raise` steps execute in document order and drive an independent
  native transition trace;
- raised-only event names are present in the deterministic program mapping;
- missing/empty/invalid `event`, child content, unknown attributes, and
  unsupported executable elements report stable source diagnostics;
- an undersized internal queue fails without committing state or partial
  raised Events; and
- build-tree and installed consumers can obtain bindings while SCXML-disabled
  packages still expose no frontend header or target.

Validation runs the focused SCXML test first, then native Statechart tests,
complete CFlow tests, installed-package verification with the feature both on
and off, and the repository's normal MSVC Release suite. Linux and macOS CI
provide the remaining compiler/platform evidence.
