# CMeta/CFlow Canonical Raw Batch Dispatch Design

## Context

The stage-fused CFlow Plan already avoids the materialized input copy and uses
exact survivor-sized intermediate/result storage. Five paired Release runs still
showed only 114.5 M items/s on MSVC and 135.8 M items/s on Clang, while a
benchmark-only raw staged path reached 288.4 M and 297.3 M items/s respectively.
The remaining measured difference is dominated by one erased adapter call per
callback and item.

The current representation cannot safely infer adapter equivalence. A custom
callable can have zero capture and a valid raw target while its `invoke` adapter
performs intentional behavior. Capturing lambdas require the adapter. Effects
and properties describe callback semantics, not permission to bypass it.

## Decision

Add an explicit public `cmeta_callable_dispatch` tag with stable values:

- `CMETA_CALLABLE_DISPATCH_ADAPTER = 0` keeps the existing adapter contract and
  is the default for zero initialization and `CMETA_CALLABLE_INIT`.
- `CMETA_CALLABLE_DISPATCH_CANONICAL_RAW = 1` promises that the resolved raw
  `cmeta_fn` target is the canonical operation represented by the generated
  adapter.

`CMETA_CALLABLE_INIT` keeps its existing arguments and remains adapter-safe.
`CMETA_CANONICAL_RAW_CALLABLE_INIT` is added for named typed factories generated
by CMeta/CFlow. Capturing lambdas, partial application and custom initializers
continue to use `CMETA_CALLABLE_INIT`.

`cmeta_callable_bind` validates the tag. Canonical raw requires zero capture and
an otherwise valid raw function contract. Adapter callables validate their
signature/effects/properties and protocol-specific adapter, and may omit the raw
target because the adapter is authoritative. This also restores the documented
capturing-lambda path, whose generated metadata intentionally has no raw target.
Invalid tags and contradictory canonical/capture state fail at bind time; there
is no downgrade or runtime fallback.

`cmeta_callable_can_dispatch_canonical_raw` exposes the validated capability.
Callable equality compares the dispatch tag and, for canonical raw callables,
the active signature-specific raw target. Adapter equality otherwise retains
its existing callback/capture semantics.

## Plan Compilation and Execution

`cflow_plan_call` gains one private predecoded unary batch function pointer.
Plan compilation resolves it only when the bound callable advertises canonical
raw dispatch. Every unary signature enabled by the active CMeta signature policy
gets a generated batch loop.

The batch loop is invoked once per Filter or Map stage:

- Filter reads the borrowed original input, honors the current selection bitset,
  and removes rejected indices while maintaining the exact survivor count.
- The first Map compacts selected input into exact survivor-sized output; later
  Maps consume contiguous owned intermediate storage.
- Raw inputs/results use `memcpy` at the erased byte boundary, preserving
  alignment safety. The typed raw target remains one indirect call per item.
- Calls without the capability retain the existing per-item adapter path.

The private evaluation profile records raw batch stage calls and adapter item
calls. This provides a behavioral test boundary without exposing Plan internals
as installed API.

Time complexity remains O(n * stages); auxiliary selection space remains
ceil(n/8), and exact intermediate/result ownership is unchanged. No pool, arena,
cache or additional retained buffer is introduced because allocation volume is
already bounded and measured.

## Compatibility

This deliberately changes the size/layout of public `cmeta_callable` and the
operator wrappers that embed it. All CMeta/CFlow consumers must recompile; no
persisted or wire representation is supported. Existing macro call sites remain
source compatible because `CMETA_CALLABLE_INIT` keeps its argument list and
defaults to adapter dispatch. Designated initializers that omit the new field
also remain adapter-safe through zero initialization. Positional struct
initializers may require source updates.

No observable output, order, effect/property rule, ownership boundary or failure
semantics changes for previously admitted callables. A custom adapter with a raw
target remains observable through Kernel and Plan. Adapter-only capturing
callables, previously rejected despite their documented representation, now bind
and execute through their adapter; they never enter canonical raw dispatch.

## Verification

- CMeta behavior: named typed capability, adapter default, invalid tag,
  canonical-with-capture rejection, and canonical raw target equality.
- CFlow behavior: the standard Filter/Map pipeline uses three raw batch stage
  calls and zero adapter item calls; custom and capturing callables use adapter
  item calls and preserve outputs/side effects.
- C/C++ public header compilation and the existing CMeta/CFlow test matrix.
- Five 50,000-sample MSVC and Clang Release benchmark runs against the retained
  raw staged control, materialized Plan, stage-fused Plan and Direct paths.
- Allocation/peak-memory assertions remain unchanged.

## Rollback

Remove the dispatch tag/helper/canonical initializer and Plan batch pointer/loops,
then restore named typed factories to `CMETA_CALLABLE_INIT`. Existing adapter
execution and exact-memory fused Plan behavior remain a complete rollback point.

## AOT and IR Boundary

Filter-prefix/Map-chain fusion is semantically derivable from stable IR facts:
linear topology, continuous types, pure effects, deterministic/total/no-alias
properties, stable order and explicit input/result ownership. Machine inlining is
not an IR semantic and cannot be guaranteed from a runtime function pointer.

The current Graph/Plan IR can prove fusion legality and choose adapter versus
canonical-raw batch dispatch. It cannot reconstruct Direct's compile-time named
callee, so an IR-only Plan optimization cannot remove the remaining indirect raw
call. Closing that gap requires a later typed AOT Stage IR (or equivalent source
generation/LTO/JIT boundary) that distinguishes `StaticTarget`,
`CanonicalRawBatch` and `Adapter`, represents the `Filter* ; Map*` fusion region,
and carries a lowering/equivalence witness back to Graph semantics. This phase
does not add that IR or claim that raw capability guarantees inlining.

## Results

Five 50,000-sample Release runs produced:

| Compiler | Direct | Raw staged ceiling | Materialized Plan | Optimized Plan |
|---|---:|---:|---:|---:|
| MSVC | 2,433.6 M items/s | 303.4 M (279.7-331.9) | 126.5 M | 280.5 M (262.4-301.5) |
| Clang | 2,002.6 M items/s | 305.5 M (292.3-315.0) | 123.8 M | 249.6 M (239.7-264.8) |

Compared with the preceding stage-fused medians (114.5 M MSVC, 135.8 M
Clang), optimized Plan improved 145.0% and 83.8%. It reaches 92.5% and 81.7%
of the raw staged ceiling and is 2.22x/2.02x faster than materialized Plan.
Direct remains 8.68x/8.02x faster because its generated typed loop permits
inlining and cross-stage optimization; dynamic Plan still performs one raw
function-pointer call per callback and item.

Existing memory assertions remain exact: one-bit selection, survivor-sized
intermediate/result buffers, no staged input copy, and unchanged allocation,
allocated-byte and peak-live-byte counts.
