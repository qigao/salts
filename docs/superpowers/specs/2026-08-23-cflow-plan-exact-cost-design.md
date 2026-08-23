# CFlow Plan Exact Cost Design

## Goal

Refine the CMeta-CFlow calculus with a source-level model of the current fused
value Plan path and keep that model separate from wall-clock benchmark claims.
The reference workload is the Release benchmark in
[PR #44](https://github.com/qigao/turbo-utils/pull/44): one filter followed by
two canonical raw maps.

## Evidence boundary

- The C Graph stores dense node IDs in `nodes[]`; node lookup is indexed.
- Successor discovery currently scans `edges[]` during Graph validation and Plan
  compilation. This is control-plane work and is absent from Plan evaluation.
- The compiled Plan owns a contiguous `code[]` instruction tape. The fused
  value evaluator scans the filter stage over all `n` input positions, scans
  the first map over all `n` selection positions, and scans the second map over
  the `k` selected values.
- [PR #44](https://github.com/qigao/turbo-utils/pull/44) supplies Release
  measurements and C assertions. Lean proves symbolic
  formulas and representation/refinement obligations, not elapsed time or the
  correctness of the compiled C binary.

## Formal model

Add a `FusedFilterMapMap` workload with input count `n`, selected count `k`,
intermediate item size, and result item size. A validity premise requires
`k <= n` and positive item sizes.

`PlanEvalMetrics` records source-level events that are more precise than the v1
ten-dimensional `Cost`: Graph queries, instruction visits, stage dispatches,
user callback applications, element visits, allocation/resource statistics,
and non-empty memory passes. The canonical raw path has:

```text
graphQueries       = 0
instructionVisits  = 2                    when n > 0
rawBatchStageCalls = 3                    when n > 0
adapterItemCalls   = 0
userCalls          = n + 2*k              when n > 0
elementVisits      = 2*n + k              when n > 0
selectionBytes     = ceil(n / 8)          when n > 0
intermediateBytes  = k * intermediateSize when n > 0
resultBytes        = k * resultSize       when n > 0
allocatedBytes     = the sum above
allocations        = one per non-empty allocation
peakLiveBytes      = intermediateBytes + max(selectionBytes, resultBytes)
memoryPasses       = 2 + (k > 0 ? 1 : 0) when n > 0
```

The profile projects into the existing `Cost` without changing its ten stable
dimensions. Function-pointer dispatches are the three raw batch stage calls
plus the per-value raw callback calls. Copy counts remain unspecified because
the C `memcpy` operations may lower to loads/stores and the v1 logical copy
dimension does not define those implementation details.

## Representation equivalence

The logical Graph and physical Plan storage are separate. Model linear tape,
tree, and hash-indexed encodings as storage choices that expose a canonical
stage schedule. Two encodings are representation-equivalent only when they
produce the same schedule and output observation. Prove that equivalent
encodings have the same Plan metrics.

This does not claim every Graph is a tree: a tree cannot natively preserve DAG
sharing and joins without references or duplication. It also does not select a
hash table for dense node IDs. If Graph-to-Plan compilation is measured as a
bottleneck, the first candidate is an O(V+E) temporary adjacency/CSR index;
the execution Plan remains a contiguous instruction tape.

## Trust and compatibility

- No production C/C++ API or runtime behavior changes.
- No new dependency, axiom, `sorry`, or `admit`.
- The C-to-Lean conformance boundary remains explicit: current C assertions are
  witnesses for the benchmark shape, not a verified compiler extraction.
- Empty input and empty selection are represented explicitly; the exact
  three-allocation theorem requires `n > 0`, `k > 0`, and positive item sizes.

## Verification

Run the focused Phase G Lean file, then `lake test`, `lake build`, a proof escape
scan, and `git diff --check`. Cross-check the `n=1024`, `k=512`, `sizeof(long)=8`,
`sizeof(double)=8` witness against PR #44's C assertions.
