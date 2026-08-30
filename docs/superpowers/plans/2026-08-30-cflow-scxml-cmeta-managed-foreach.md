# CFlow SCXML CMeta Managed `<foreach>` Implementation Plan

> **Execution mode:** inline, with focused TDD checkpoints.

## Goal

Extend the transactional CMeta `<foreach>` implementation from trivially
copyable elements to managed elements with explicit copy, move, and destroy
traits, without changing the public SCXML API or retaining Range borrows.

## Ownership and failure contract

- A managed Range must advertise `CMETA_RANGE_CONSTRUCTS_VALUES`; each
  successful `next()` constructs one independently owned element in empty,
  correctly aligned storage.
- One invocation-local scratch slot is allocated before the loop. Its exact
  bound is the validated element size plus alignment padding; checked
  arithmetic rejects an impossible allocation before iteration starts.
- On each iteration, Range output first constructs the scratch value. Only
  after success is the previous staged `item` destroyed and the scratch value
  move-constructed into that field. The moved-from scratch is destroyed and
  returned to the empty state before the child executable range runs.
- Range failure leaves scratch empty by the CMeta Range contract. Allocation,
  Range, or child-executable failure raises `error.execution`; the existing
  root transaction remains the sole rollback mechanism.
- The Range borrows the staged sequence. The scratch slot owns only the current
  copied element and is never retained across child callbacks or execution
  suspension.

## Admission and compatibility

- Trivial elements keep the existing memcpy path.
- Non-trivial elements require exact item/element type equality and complete
  `COPY | MOVE | DESTROY` traits. No new fallback or partial trait mode is
  admitted.
- Existing XML, compile/session options, serialized state, and trivial
  behavior remain unchanged. The only public behavior change is that schemas
  previously rejected solely for managed element lifecycle are now accepted.

## TDD checkpoints

1. Add a managed Vec element with independently owned test storage and verify
   compile, declared-order iteration, final item value, and balanced lifetime.
2. Run the focused test and record the expected RED type-mismatch failure.
3. Add the private scratch-slot lifecycle and managed admission checks.
4. Route the runtime loop through one scratch slot with a single cleanup path.
5. Run the focused target, adjacent CMeta/SCXML tests, Debug ASan, and Release
   regression tests; finish with CodeGraph affected analysis and
   `git diff --check`.

## Rollback

Reinstating the trivial-trait admission check and removing the private scratch
slot restores the prior behavior. No persisted data or public structure layout
is changed.
