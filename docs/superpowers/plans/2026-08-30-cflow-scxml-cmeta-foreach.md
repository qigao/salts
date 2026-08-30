# CFlow SCXML CMeta `<foreach>` Implementation Plan

> **Execution mode:** inline, with focused TDD checkpoints.

## Goal

Add bounded transactional `<foreach>` support to ordinary CMeta executable
blocks while keeping `finalize` fail-fast and preserving callers compiled
against the original v1 compile-options prefix.

## Contract

- `array` resolves at compile time to a declared unary CMeta sequence.
- `item` resolves at compile time to a writable field whose concrete storage
  type is semantically equal to the sequence element type.
- Optional `index` resolves to an exact `size_t` field.
- This increment admits only element/item types with trivial copy and destroy
  traits. That makes Range output into an already-live staged field safe. A
  future managed-value increment must add an aligned temporary-value lifetime
  protocol before broadening admission.
- At execution start the borrowed Range and its `SIZED | ORDERED` length are
  validated once. The length is the iteration-count snapshot; current CMeta
  executable content cannot replace or resize a sequence field.
- `max_iterations` is a positive hard bound. A collection above the bound,
  Range failure, or child executable error raises `error.execution`, aborts the
  containing block, and relies on the existing whole-root transaction rollback.
- Empty collections execute no child steps. Each successful iteration writes
  `item`, then optional zero-based `index`, then executes the child range.
- The Range, cursor, and element views are invocation-local borrows and are
  never retained across callbacks or after the executable returns.

## Public options compatibility

Append `size_t max_iterations` to
`cflow_scxml_cmeta_compile_options_v1`. Accept either the exact legacy prefix
size ending at `max_string_bytes`, or a complete current/future structure.
Reject partial tail structures. Legacy-prefix callers receive the named default
limit; current callers must provide a positive value.

## TDD checkpoints

1. Add integration tests for ordered item/index assignment, empty sequences,
   old-prefix options, iteration-limit rollback, and invalid locations/types.
2. Run the focused target and record the expected RED failures.
3. Add a shared private CMeta location resolver and refactor assignment to use
   it without behavior changes.
4. Add the foreach compiler/runtime bridge and SCXML analysis/emission rows.
5. Re-run focused tests, adjacent CMeta/SCXML tests, Debug ASan, and Release
   regression tests; finish with CodeGraph sync and `git diff --check`.

## Compatibility and rollback

The XML surface change is additive for `datamodel="cmeta"`; the null model and
`finalize` continue to reject `<foreach>`. Removing the new step, descriptor,
and tail option restores the prior behavior without changing serialized state
or repository data.
