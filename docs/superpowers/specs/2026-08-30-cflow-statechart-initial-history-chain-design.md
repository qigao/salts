# CFlow Statechart Initial-to-History Chain Design

## Context

The native Statechart validator currently requires every initial and history
default transition to target a real descendant. SCXML 1.0 additionally permits
an initial transition to target a sibling history pseudo-state. W3C assertion
579 relies on that form and requires the following executable-content order:

1. the history parent's `onentry` content;
2. the initial transition content;
3. the history default transition content, only while history is unset;
4. descendant entry content.

The runtime already keeps configuration and history as one transactional fact
source, excludes pseudo-states from published configurations, and interleaves
one pseudo-transition action after its owning state's entry actions. It does not
currently represent the second action in an initial-to-history chain.

The SCXML frontend also currently resolves null-model and CMeta `In(id)` only
for real states. A declared history ID is not an unknown state; its membership
query has a defined false result because pseudo-states are excluded from the
runtime configuration. Rejecting that query prevents W3C assertion 580 from
observing the existing invariant.

## Decision

Admit exactly one additional native shape: an initial pseudo-state may target a
shallow or deep history pseudo-state with the same parent. History default
transitions continue to require a real descendant target, so the admitted
pseudo chain is bounded to `initial -> history -> real state`.

Do not add another per-instance action table. The initial transition remains the
single pseudo transition associated with its owning state. When its target is a
history pseudo-state, configuration construction resolves either the stored
history or its real default target. Action execution then runs the initial
transition content and, only when that history slot is unset, immediately runs
the history default transition content before descendant entry continues.

This preserves the existing O(states) storage bound and public ABI. Both action
blocks still use the existing staged extended state, internal-event staging,
effect journal, first-error ownership, and all-or-nothing microstep commit.

Allow `In(id)` resolution for every declared state ID, including initial and
history pseudo-states, in both null-model and CMeta expression paths. Unknown IDs
and malformed expressions remain admission errors. The runtime's single
configuration query remains the fact source and returns false for pseudo IDs.

## Alternatives

### Keep rejecting the native shape and rewrite the W3C fixture

Rejected. Moving the history lookup behind an ordinary state changes both
configuration timing and executable-content order, so it would not preserve
assertion 579.

### Add per-owner pseudo-transition spans

Rejected for this scope. A second offset/count table would model arbitrary
pseudo chains, but the native validator intentionally rejects history-to-history
defaults. The only newly admitted chain has a fixed semantic depth of two, so
additional instance storage and migration cost provide no current capability.

### Lower the two actions into one frontend executable block

Rejected. The native runtime, not the SCXML frontend, owns stored-history state.
The frontend cannot know whether the default history content is eligible, and a
combined callback would duplicate or leak runtime state into the adapter.

## Validation and runtime rules

- A pseudo transition remains unguarded, eventless, external, and single-target.
- A real target remains a proper descendant of the pseudo-state parent.
- A pseudo target is legal only when the source is `INITIAL`, the target is
  `HISTORY_SHALLOW` or `HISTORY_DEEP`, and both have the same parent.
- Initial configuration always sees an unset history slot and enters its
  declared default real configuration.
- Later default entry through the same initial state restores stored history
  when present and does not execute history default content.
- An ordinary transition directly targeting history keeps its existing
  behavior and action ordering.
- Pseudo-states never enter working or published active configurations and
  remain false through contextual `is_active` / null-model `In(id)` queries.
- Null-model and CMeta `In(id)` resolve declared pseudo IDs instead of treating
  them as unknown or invalid real-state references.

## Compatibility, performance, and rollback

- Public types, function signatures, structure sizes, storage queries, and
  serialized data do not change.
- A definition previously rejected as `CFLOW_STATECHART_INVALID_INITIAL` may now
  be accepted only for the specified sibling-history shape. Existing accepted
  definitions retain their transition and action order.
- An `In(id)` expression naming a declared pseudo-state is newly admitted and
  deterministically evaluates false; malformed and unknown IDs remain rejected.
- Resolution adds constant-time kind/parent checks and one history-slot lookup
  only when an initial target is a history pseudo-state.
- On any action, queue, effect, allocation, or configuration failure, the
  existing staged transaction is discarded; no new compensation path exists.
- Rollback is one localized validator rule plus the runtime chain helpers; W3C
  fixtures 579/580 and the native regression identify the capability that would
  be removed.

## Verification boundary

- A native runtime regression must prove initial, unset-history, and descendant
  initial actions execute in order while pseudo-state queries remain false.
- W3C-derived test 579 must prove the first-entry order and prove that a later
  stored-history entry runs initial content but suppresses history default
  content.
- W3C-derived test 580 must check `In(history)` at child, parent, exited, and
  restored configurations.
- Focused native and SCXML tests, all adjacent CFlow SCXML tests, and the full
  configured CTest suite must pass.
