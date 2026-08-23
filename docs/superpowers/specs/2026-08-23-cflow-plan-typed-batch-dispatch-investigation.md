# CFlow Plan Typed Batch Dispatch Investigation

## Question

The stage-fused Plan retains exact owned results and stable stage order but is
still 14-17 times slower than generated Direct execution. Determine how much of
that gap comes from the erased `cmeta_callable_invoke_fn` adapter boundary, and
whether Plan can safely bypass it without changing a public callable contract.

## Existing Contract

- A bound `cmeta_callable` owns both a raw `cmeta_fn` target and an `invoke`
  adapter. Plan execution currently calls the adapter.
- Named `typed(...)` callables have a zero-sized capture and a raw unary target,
  but custom callables may have the same representation and intentionally
  implement different adapter behavior.
- Capturing lambdas have no raw target and must use their adapter.
- Effects and properties describe callback semantics; they do not identify the
  adapter implementation or grant permission to replace it.
- `cmeta_callable` and the operator-specific callable wrappers are public ABI.

Therefore `capture_size == 0` plus a valid raw target is not sufficient evidence
that invoking the raw target is observationally equivalent to invoking the
adapter. Production specialization requires an explicit, authoritative callable
capability; adding one would change a public interface or ABI and is outside this
investigation.

## Experiment

Add a benchmark-only fixed-pipeline control that receives the already-bound raw
function pointers for `int -> bool`, `int -> long` and `long -> double`. It uses
the same one-bit selection vector, exact survivor-sized intermediate, exact owned
result and stage order as Plan, but calls each raw typed function pointer directly
inside its stage loop.

The control must:

- validate non-empty input/output arguments and checked byte arithmetic;
- allocate selection, intermediate and result with the same ownership sequence;
- return the same literal output as Direct and Plan;
- run beside Direct, erased fused, stage-fused Plan and materialized Plan;
- remain entirely inside the non-installed benchmark target.

## Decision Rule

Run five 50,000-sample executions under MSVC and Clang Release. If the raw
predecoded median is less than 20% faster than stage-fused Plan, reject further
dispatch specialization as insufficient. If it is at least 20% faster, record
the upper bound and stop before production changes; request explicit authorization
for the public callable capability needed to preserve custom-adapter semantics.

No benchmark result authorizes silently bypassing `cmeta_callable.invoke`.

## Results

Five 50,000-sample executions produced:

| Compiler | Raw predecoded staged-owned | Stage-fused Plan | Raw / Plan |
|---|---:|---:|---:|
| MSVC Release | 288.4 M items/s (232.5-310.9) | 114.5 M items/s (104.5-134.4) | 2.52x (+151.9%) |
| Clang Release | 297.3 M items/s (227.0-309.0) | 135.8 M items/s (108.7-144.4) | 2.19x (+118.9%) |

Direct medians in the same runs were 1,906.8 M items/s on MSVC and 1,862.9
M items/s on Clang. Raw batch dispatch removes more than half of the measured
Plan gap, but Direct remains 6.61x and 6.27x faster because its typed calls can
be inlined and optimized across the complete generated loop; the raw control
still performs one indirect function-pointer call per callback and item.

## Decision

The result exceeds the 20% gate on both compilers, so a production capability is
worth designing. It cannot be inferred from current fields:

1. **Explicit callable dispatch capability (recommended):** add an immutable
   `canonical_raw`/dispatch-kind field set only by named `typed(...)` factories.
   `cmeta_callable_bind` validates that the capability has zero capture, a value
   protocol and a valid raw target. Plan records signature-specific batch loops;
   custom adapters and captures keep the existing invoke path. This is clear and
   fail-fast, but changes the public `cmeta_callable` ABI and requires consumers
   to recompile.
2. **Pointer marker in an existing callback field:** preserves structure size but
   overloads `generate` or another callback with unrelated capability semantics.
   It is brittle, easy for custom constructors to misuse and not recommended.
3. **Generated schema-only specialization:** preserves runtime ABI but recreates
   the existing Direct model rather than improving dynamically compiled Plan.

The recommended change affects CMeta construction/binding, CFlow Plan compile
and execution, C/C++ header compatibility, callable equality, custom-adapter
tests and benchmark coverage. Callable state remains immutable and copied with
Graph/Plan; inconsistent capability metadata must fail during bind/compile, with
no runtime fallback. Because this changes public ABI, implementation requires
explicit user authorization.
