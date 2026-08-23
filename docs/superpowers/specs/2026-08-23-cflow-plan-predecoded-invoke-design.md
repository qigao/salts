# CFlow Plan Predecoded Invoke Optimization Design

## Background

The controlled Direct/Plan investigation measured scalar erased invocation as
the dominant cost of the current Filter/Map Plan. A fixed 1,024-item pipeline
performs 2,048 callable invocations per evaluation. Each ordinary `typed(...)`
call currently enters its generated callable adapter and then repeats generic
signature lookup, target validation, signature switching and byte-copy logic in
`cmeta_fn_invoke`.

The Plan also repeats callable signature lookup for every materialized map stage
on every evaluation, even though a compiled Plan is immutable and the Graph has
already bound and validated each callable.

## Decision

Optimize the existing dynamic Plan without changing its public representation
or observable result:

- CMeta will generate one header-local invoke adapter per enabled unary and
  binary signature. Ordinary named `typed(...)` CFlow callables select the
  adapter matching their function pointer type. The adapter preserves the
  existing null checks, byte-safe argument loads, optional result storage and
  typed function call, but it does not perform a runtime signature switch.
- Plan compilation will snapshot each callable's authoritative `invoke` entry
  and validated input/output type descriptors into private instruction data.
  Evaluation calls that cached entry directly and does not resolve signatures
  again.
- Capturing lambdas, partial applications and manually constructed callables
  retain their custom `invoke` functions. The optimizer will never infer that a
  zero-sized capture makes `meta.call` authoritative: `capture_size == 0` is an
  ownership fact, not a dispatch-kind marker.
- Generator and reduce execution remain behaviorally unchanged. Reduce may use
  the cached authoritative entry, but signature-specific work in this phase is
  limited to the typed value adapters exercised by Filter/Map.

The Plan remains dynamically dispatched and cannot inline arbitrary user
functions. AOT Direct remains the upper bound for this workload.

## Architecture and State

The immutable compiled Plan remains the sole owner of copied callable values and
its callback metadata. A private compiled-call record contains:

- the callable value copied from the optimized Graph;
- its authoritative invoke function pointer;
- the input type descriptor; and
- the output type descriptor.

Compilation validates and commits the record once. Evaluation only reads it, so
concurrent evaluations do not share mutable state. Failure before the compiled
instruction is appended destroys all partially allocated metadata through the
existing Plan cleanup path.

## Ownership and Memory Protocol

This phase does not change evaluation buffers, allocation counts, result
ownership, capacity policy, backpressure or shutdown behavior. The Plan owns its
compiled-call array until `cflow_plan_destroy`; evaluation borrows that immutable
array for the duration of the call. Callable captures remain copied by value in
the compiled Plan.

The later Filter/Map fusion phase will address the separately measured input
copy and intermediate-buffer costs. It requires its own bounded scratch and
failure protocol and is intentionally not combined with dispatch work here.

## Compatibility and Risks

- No public struct, function signature, data format or CMake consumer contract
  changes.
- Header-generated typed adapters preserve the current callable ABI and invoke
  contract. Their function-pointer identity is an implementation detail; each
  callable value remains stable after construction and copying.
- Custom callables continue to execute their custom adapters, including side
  effects or validation not represented by `meta.call`.
- The principal implementation risk is a generated adapter disagreeing with an
  enabled signature. Generation must use the same `CMETA_ALL_SIGNATURES` source
  as `cmeta_raw_call`, and tests must cover unary, binary, capture and partial
  application paths.

## Performance Gate

Use the existing fixed benchmark and compare five-run medians in the same
Release build tree. The change is accepted only if:

- production Plan throughput improves by at least 30% on the primary MSVC
  Release measurement;
- Direct throughput does not regress by more than 5%; and
- the full CFlow test set and focused CMeta callable tests remain green.

Clang Release is a required cross-compiler check and is reported separately.
Variance ranges and the exact build flags remain part of the evidence; a single
sample is not an acceptance result.

## Verification

- Add a private-Plan test proving compile-time callback/type predecode is
  committed into every Filter/Map instruction.
- Preserve output parity among interpreter, Plan, Kernel and Direct paths.
- Run a custom-adapter trap through Plan to prove its authoritative invoke entry
  is still executed. Capture and partial-application admission are a separate
  existing contract issue and are not weakened or silently bypassed here.
- Run the focused CMeta and CFlow test targets, then all `^cflow_` tests.
- Run five benchmark samples under MSVC Release and Clang Release and compare
  medians against the recorded investigation baseline.
- Inspect the optimized Plan object to confirm ordinary typed callbacks no
  longer retain calls to `cmeta_fn_invoke` in their generated adapters.

## Observed Evidence

The final comparison uses five runs of the existing 10,000-sample,
1,024-input-item benchmark. Throughput is million input items per second; ranges
are the lowest and highest observed run.

| Path | Investigation baseline | Predecoded median (range) | Ratio |
|---|---:|---:|---:|
| MSVC Direct | 2,368.7 | 3,044.4 (2,983.9-3,060.2) | 1.29x |
| MSVC Plan | 42.1 | 177.1 (170.9-178.4) | 4.21x |
| Clang Direct | 2,146.8 | 2,667.1 (2,638.9-2,671.6) | 1.24x |
| Clang Plan | 53.7 | 160.7 (157.8-167.7) | 2.99x |

The Plan gain is approximately 321% on MSVC and 199% on Clang, so the 30%
acceptance gate passes. Direct did not regress beyond the 5% gate. The Direct
increase is not attributed to this Plan change because the generated Direct
loop does not use the modified adapter; it is measurement/environment variance
relative to the earlier investigation session.

Clang object inspection shows Filter and Map calling cached instruction
function pointers directly. The three ordinary benchmark adapters contain
signature checks and their typed indirect target calls, but no relocation or
call to `cmeta_fn_invoke`. The Plan executor object has no relocation to
`cmeta_callable_invoke` or `cmeta_callable_signature`; the deliberately erased
benchmark control retains its public erased-dispatch calls.

All 14 CMeta/CFlow CTest targets pass in both MSVC Release and Clang Release.
The focused custom-adapter trap also confirms Plan still calls the callable's
authoritative adapter rather than bypassing it.

## Remaining Cost and Memory Direction

Plan is still approximately 17.2 times slower than Direct on the MSVC medians
and 16.6 times slower on Clang. The residual dynamic path performs an indirect
adapter call followed by an indirect typed target call for every item and cannot
be inlined or vectorized like generated Direct.

Memory behavior is unchanged: the fixed Windows workload still performs three
evaluation allocations/frees totalling 10,240 bytes, copies 4,096 input bytes,
materializes 6,144 map-output bytes and peaks at 6,144 live bytes. Dispatch is
now cheaper, so Filter/Map fusion and single-result allocation are the next
measurable Plan optimization. That phase must preserve immutable Plan state,
checked size arithmetic and per-evaluation ownership rather than introducing a
shared mutable scratch buffer.

The repository's capture and partial-application macros currently generate
metadata with no raw `meta.call` target, while `cmeta_callable_bind` requires a
valid raw target. Consequently the unbuilt `demo_lambda` path is rejected before
Plan compilation. This is a pre-existing callable-admission contract issue and
was not hidden by weakening validation in this performance patch.

## Rollback

Reverting the generated adapter selection and the private compiled-call record
restores the old dispatch path. No persisted data, public consumer migration or
result conversion is required.
