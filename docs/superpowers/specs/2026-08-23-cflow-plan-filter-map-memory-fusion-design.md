# CFlow Plan Filter/Map Memory Fusion Design

## Background

The predecoded Plan improves the fixed Filter/Map workload by 2.99-4.21 times,
but its materialized evaluator still copies the entire input and allocates one
full result buffer per Map callback. On Win64 the 1,024-item benchmark currently
requests three allocations totalling 10,240 bytes, copies 4,096 input bytes,
materializes 6,144 Map-output bytes and reaches 6,144 owned live bytes.

The fixed pipeline contains only a leading Filter followed by a pure, stable,
total Map chain. Its output cardinality cannot exceed its input cardinality, and
the borrowed array remains valid for the complete evaluation call.

## Decision

Compile an internal fused-value eligibility record for complete Plans whose
instructions consist only of zero or more leading Filters followed by zero or
more Maps, with at least one instruction. Every callback must declare:

- `CMETA_EFFECT_PURE`;
- `CMETA_PROP_DETERMINISTIC`;
- `CMETA_PROP_TOTAL`; and
- `CMETA_PROP_NO_ALIAS`.

Ineligible Plans continue through the existing materialized evaluator. There is
no runtime failure fallback: eligibility is a compile-time semantic decision,
and a failure inside the selected evaluator fails the evaluation transaction.

Eligible evaluation uses bounded stage passes:

1. Evaluate each leading Filter over the currently selected borrowed input
   elements. A one-bit-per-input selection vector records survivors. Processing
   one complete Filter at a time preserves the existing Filter-stage order.
2. Allocate each Map output for exactly the selected count and evaluate one
   complete Map stage at a time. The previous owned intermediate is released
   after the next stage commits. A Filter-only Plan copies selected values
   directly to its exact owned result.

An experimental per-item Map chain reduced allocations further but changed the
indirect callback target for every item and regressed measured Plan throughput
by 35-47%. Stage-by-stage evaluation retains a stable indirect target in each
hot loop. Stateful, fallible, IO, async, unknown or merely-pure callbacks remain
on the materialized path.

## Data and Ownership Protocol

- **Producer/consumer topology:** one synchronous caller thread; no internal
  queue, fan-out or worker thread.
- **Input owner:** caller. The Plan borrows input bytes only until
  `cflow_plan_eval_array` returns and never stores their address.
- **Plan owner:** `cflow_plan` owns immutable instruction and eligibility
  metadata until `cflow_plan_destroy`. Concurrent evaluations only read it.
- **Selection owner:** one evaluation owns the one-bit-per-input selection
  vector until the first Map has consumed it, or until a Filter-only result is
  complete. It is freed on every success or failure exit.
- **Intermediate owner:** one evaluation owns at most two adjacent Map-stage
  buffers while committing a transition. The previous stage is freed
  immediately after the next stage succeeds.
- **Result owner:** the successful `cflow_result` owns exactly
  `selected_count * output_type->size` bytes. Failure leaves the caller result
  zeroed. Zero selected values return a null data pointer.
- **Capacity:** selection uses `input_count / 8 + (input_count % 8 != 0)` bytes.
  Every intermediate and final result uses the overflow-checked exact
  `selected_count * stage_output_size` byte count.
- **Full/backpressure:** there is no growth or backpressure. Any arithmetic or
  allocation failure immediately aborts with `false` and releases auxiliary and
  partial result storage.
- **Invalidation:** borrowed input pointers are not retained across callbacks or
  after return. An intermediate stage is invalidated when the next stage commits
  and releases it. No raw intermediate pointer crosses evaluation or suspension.
- **Concurrency:** Plans remain immutable and read-only; every mutable byte is
  evaluation-local, so parallel callers do not share intermediate or selection
  state.

## Resource Accounting

Add a private, non-installed evaluation profile entry point for tests and
benchmarks. For an eligible fused run it reports actual requested allocation
calls and bytes, peak live requested bytes, exact result bytes, selection bytes,
intermediate bytes and staged input-copy bytes. The ordinary public entry point calls
the same implementation with a null profile pointer.

For the fixed Win64 benchmark the calculated stage-fused budget is:

- selection: `1024 / 8 = 128` bytes;
- exact non-final Map intermediate: `512 * sizeof(long) = 2,048` bytes;
- exact result: `512 * sizeof(double) = 4,096` bytes;
- allocation calls: 3;
- allocated requested bytes: `128 + 2,048 + 4,096 = 6,272` bytes;
- peak live requested bytes: `2,048 + 4,096 = 6,144` bytes;
- staged input-copy bytes: 0;
- full intermediate Map allocations: 0.

This is a 38.75% reduction in requested allocation bytes, eliminates the 4,096
byte staged input copy and preserves the 6,144-byte peak owned live budget of
the materialized calculation. Allocator metadata and implementation-specific
usable size are outside these counters.

## Complexity

For `n` inputs, `f` Filters and `m` Maps, time is `O(n*f + k*m)`, where `k` is
the surviving input count. Auxiliary space is `O(n/8 + k*i)`, where `i` is the
largest adjacent intermediate-stage footprint. Owned result space is exactly
`O(k * output_size)`.

## Compatibility and Risks

- No public API, ABI, result format, allocator dependency or Plan ownership
  contract changes.
- Result order, values, count and type remain unchanged.
- Input was already documented as borrowed for the call; fusion removes an
  implementation copy but does not extend the borrow.
- Callbacks that mutate input or expose side effects while declaring the value
  contract violate their positive `NO_ALIAS`/purity guarantees. Such behavior is
  not preserved as an observable sequencing contract.
- Internal profiling is diagnostic only and remains excluded from installation.

## Verification and Performance Gate

- TDD resource test: six inputs, three outputs, one selection byte, 12 bytes of
  exact non-final Map intermediate on Win64, 24 exact result bytes, three
  allocations, 37 allocated bytes, 36 peak live requested bytes and zero staged
  input-copy bytes. Formulas use `sizeof(long)` so other host ABIs remain valid.
- Fallback test: a stateful Map reports that fusion was not selected and retains
  correct output.
- Test empty input, all-filtered input, map-only and filter-only Plans.
- Compare Plan output with interpreter/Kernel and keep the custom-adapter test.
- Run all CMeta/CFlow tests under MSVC Release and Clang Release.
- Run five 50,000-sample benchmark executions per compiler against an identical
  compiled Plan whose fusion flag is disabled. Accept only if the paired Plan
  median does not regress by more than 5%. Historical absolute measurements are
  context only because CPU frequency varied materially between sessions; report
  gains without claiming Direct parity.
- Confirm fixed benchmark profile values match the 6,272-byte allocation and
  6,144-byte peak calculations.

The final post-refactor paired medians were 119.6 versus 119.5 M items/s on MSVC
(+0.1%) and 118.3 versus 105.1 M items/s on Clang (+12.6%). Direct medians in
the same runs were 2,030.0 and 1,724.6 M items/s respectively. The remaining gap is
dominated by generic erased callback dispatch and owned-result construction,
which Direct avoids through generated typed calls and caller-owned output.

## Rollback

Removing the eligibility fields, stage-fused evaluator, private profile entry point
and its tests restores the materialized evaluator. No persisted data or public
consumer migration is required.
