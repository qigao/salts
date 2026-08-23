# CFlow Typed AOT Stage IR Phase F-3 Design

## Context

Phase F-2 generates a fused typed loop and a Surface Graph from one
`CFlowDirectSteps` schema. Phase F-2B then gives dynamic Plan execution an
explicit canonical-raw batch capability. The remaining boundary is not another
runtime dispatch optimization: Graph and Plan do not retain the compile-time C
callee token which lets Direct emit an ordinary call that the compiler may
inline across stages.

The schema is already the compile-time fact source, but its stage meaning is not
available as a typed, inspectable IR and there is no executable check that an IR
instance denotes the same linear program as a Graph. F-3 makes that boundary
explicit without claiming portable C11 can reconstruct static calls from an
arbitrary runtime Graph.

## Decision

Add a public, immutable `cflow_aot_pipeline_ir` view containing an ordered array
of `cflow_aot_stage_ir` rows. Every row records:

- Filter or Map stage kind;
- one explicitly valued dispatch primitive: `StaticTarget`,
  `CanonicalRawBatch`, or `Adapter`;
- semantic input and output type descriptors;
- borrowed callable metadata; and
- an optional diagnostic target name, required for `StaticTarget`.

The dispatch values are stable ABI data. `StaticTarget` is declared by the
compile-time schema producer; it is not inferred from a runtime function
pointer. Runtime validation can derive type continuity, callable signature,
effects, properties, capture bounds and canonical-raw capability, but it cannot
prove that an arbitrary string or pointer came from a same-translation-unit C
token. The generated macro is the trusted construction boundary for that
provenance.

`cflow_direct_pipeline` replays its existing schema into three derived views:

1. the immutable Stage IR;
2. the Surface Graph builder; and
3. the fused typed evaluator.

The generated `<name>_ir()` returns the static IR. `<name>_eligible()` retains
its compile-time-unrolled schema checks: routing every Direct invocation through
the generic IR iterator caused a measured MSVC throughput regression above the
10% project threshold. The hot path still contains direct
`typed_call(callable)` expressions and never traverses the IR. Tests require the
unrolled eligibility result and the generic IR eligibility result to agree.

## Validation and Equivalence

`cflow_aot_pipeline_ir_validate` checks the representation and linear type
chain. All stages require trivial copy/destroy value types, one-argument value
callables, pure effects, deterministic/total/no-alias properties, and exact
Filter/Map signature rules.

Dispatch-specific validation is fail-fast:

- `StaticTarget` requires zero capture and a non-empty target name. Its static
  provenance remains a producer assertion.
- `CanonicalRawBatch` requires the callable's explicit canonical-raw
  capability and zero capture.
- `Adapter` requires a valid invocation adapter and may carry an inline capture.

`cflow_aot_pipeline_ir_inline_eligible` additionally requires every stage to be
`StaticTarget`. It does not promise that a particular compiler will inline; it
proves only that source generation has a named static target for every stage.

`cflow_aot_pipeline_ir_match_graph` normalizes a borrowed Graph into temporary
owned IR, then compares the root's exact linear Source/Filter/Map path with the
Stage IR. It checks topology, order, types and semantic callable identity, and
logically expands optimizer-fused Map chains back into their ordered callable
stages. On success it commits a small witness containing the source Graph
version and matched stage count. On failure it clears the witness and returns a
static error message. The witness records the completed check; it is not a
reusable authority after the source Graph mutates.

## Ownership and Resource Protocol

- **Data unit:** one immutable Stage IR row; no payload values are executed by
  validation or matching.
- **Fact source:** the compile-time `CFlowDirectSteps` schema. Stage IR, Graph
  builder and evaluator are derived views.
- **IR ownership:** generated stage arrays, target strings and pipeline
  descriptors have translation-unit static lifetime. The public IR borrows type
  descriptors and callable metadata and never frees them.
- **Graph ownership:** matching borrows the input Graph for the call. It owns and
  destroys only its temporary normalized Graph.
- **Capacity:** the stage count is fixed by the schema and bounded by the
  existing CMeta replay limit of 16. Runtime validation performs no growth.
- **Thread model:** validation and matching are synchronous and single-threaded
  per call. Static IR is immutable and may be shared; a Graph must not mutate
  concurrently with matching.
- **Order:** stage order is exact and observable. Matching rejects branches,
  detached root nodes, relations, nested subgraphs and extra/missing stages.
- **Failure:** invalid representation, ineligible contracts, normalization
  failure and semantic mismatch are distinct static error strings. No fallback
  selects Plan or Kernel.
- **Hot path:** Direct evaluation does not traverse the IR and retains zero
  allocation and zero callback-dispatch behavior.

## Compatibility

This phase adds public types and functions and gives the existing Direct stage
enum explicit values without changing those values. It does not change the
layout of Graph, Plan, callable or existing generated evaluator parameters.
Generated pipelines gain translation-unit-local IR data and `<name>_ir()`;
existing source remains valid.

The IR is an in-process typed representation, not a persisted or wire format.
Descriptor and callable identity remain semantic rather than address-based
across translation units.

## Verification

- Compile-time assertions lock all dispatch and stage-kind values.
- A generated pipeline exposes three ordered static-target rows with exact
  semantic types and validates as inline-eligible.
- Its IR matches its generated Surface Graph and reports the source version and
  exact stage count.
- A Graph with a different callable or stage order is rejected and leaves a
  cleared witness.
- Manually constructed canonical-raw and adapter rows validate according to
  their declared dispatch; contradictory dispatch/capture state fails.
- Existing Direct, Plan and Kernel output, error, allocation and adapter/raw
  dispatch tests remain unchanged.
- MSVC and Clang Release test matrices pass; the existing Direct benchmark must
  remain within the repository's 10% regression threshold.

## Measured Results

Five 50,000-sample Release runs after keeping generic IR validation outside the
Direct invocation boundary produced these medians:

| Compiler | Direct | Raw staged | Materialized Plan | Optimized Plan |
|---|---:|---:|---:|---:|
| MSVC | 2,719.4 M items/s | 387.4 M | 151.6 M | 354.7 M |
| Clang | 2,463.3 M items/s | 369.8 M | 157.3 M | 318.5 M |

Direct is 11.7% and 23.0% above the preceding F-2B medians on this machine, so
F-3 introduces no measured throughput regression. These cross-run gains are
environmental evidence, not attributed to Stage IR because the evaluator loop
is unchanged.

An intermediate implementation called the generic IR validator once per Direct
evaluation. Its five-run MSVC median fell to 2,153.1 M items/s, 11.5% below the
F-2B baseline. Restoring the schema-unrolled eligibility boundary raised the
same-session median by 26.3%, confirming that runtime IR traversal belonged in
the control plane rather than the frequently invoked Direct entry point.

Existing resource assertions remain exact. Direct evaluation still has no
allocation site; Stage IR data is static. Plan retains one-bit selection,
survivor-sized intermediate/result storage and no staged input copy. Graph
matching allocates only its temporary normalized control-plane Graph and destroys
it before returning.

## Deferred Work

F-3 does not lower arbitrary runtime Graphs into `StaticTarget`, serialize IR,
emit C files, invoke a compiler, use executable memory, add LTO configuration or
promise machine-code inlining. A later source-generation tool can consume the
same Stage IR contract, but must obtain target symbols from a trusted build-time
registry rather than infer them from erased runtime pointers. The matcher
understands structural Map fusion but does not reconstruct stages removed by
idempotence or other semantic rewrites; those require an optimizer proof trace.

## Rollback

Remove the Stage IR public declarations and implementation, restore generated
eligibility to its schema replay, and remove the generated IR view/tests. The
F-2 Direct evaluator and F-2B canonical-raw Plan path remain independently
usable.
