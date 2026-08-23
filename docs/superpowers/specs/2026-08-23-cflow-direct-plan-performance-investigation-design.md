# CFlow Direct/Plan Performance Investigation Design

## Background

The Phase F-2 benchmark reports a much larger Direct/Plan throughput gap than
expected for a three-stage Filter/Map pipeline. The existing result compares two
valid end-to-end contracts, but it does not isolate the costs responsible for
the gap: Direct writes to caller-owned storage, while Plan copies its input,
executes erased callables over materialized stage buffers, returns owned storage
and destroys that result inside the timed region.

## Decision

Extend the benchmark with controlled, behavior-equivalent paths that separate
the following factors without changing production APIs:

- a generated fused Direct path using caller-owned output;
- a fused typed caller-buffer loop without generated preflight checks;
- a fused typed loop with one owned output allocation;
- a staged typed loop that reproduces Plan-shaped input copying, intermediate
  materialization and allocation/free traffic without erased dispatch;
- a fused erased-callable loop using caller-owned output; and
- the existing production Plan path.

Every path uses the same input and produces the same complete output. Correctness
is checked outside timing, and an external translation unit consumes the output
inside timing so the compiler must preserve observable result production.
Throughput is reported as input items per second; it is not labelled as operator
invocations per second.

The investigation also records repeated MSVC and Clang Release measurements,
examines optimized machine code, and uses Windows CPU sampling when available.
Measurements are evidence for this machine, compiler and workload only.

## Ownership and Memory Accounting

The benchmark paths make ownership explicit:

- caller-buffer paths borrow input and output for one invocation and allocate
  nothing in the timed evaluator;
- owned-output paths allocate exactly the storage they return and free it in the
  same timed sample;
- the Plan-shaped staged path owns each temporary buffer and transfers ownership
  to the next stage only after that stage succeeds.

For the fixed 1,024-item workload with 50% filter selectivity, source-level
memory accounting will report allocation calls, freed buffers, allocated bytes,
copied input bytes and materialized intermediate bytes. Static accounting is
labelled as calculation unless runtime instrumentation independently verifies it.

## Compatibility and Risk

No public header, ABI, Plan instruction representation, executor semantics or
allocator policy changes in this investigation. Benchmark-only helpers must not
be linked into production targets. A production optimization will be proposed
only after the evidence identifies its dominant cost and its ownership/error
contract can be stated independently.

## Verification

- Build and run the benchmark with MSVC Release and Clang Release.
- Validate every controlled path against the production Plan output before
  timing.
- Run the focused CFlow Direct test and the complete CFlow test set after edits.
- Inspect the optimized executable/object code for fused-loop vectorization and
  Plan dispatch boundaries.
- Keep all benchmark labels and calculations reproducible from constants in the
  benchmark source.

## Observed Evidence

The historical measurement below used 10,000 samples of 1,024 input items. The
current benchmark source uses 50,000 samples of 1,024 input items. The following
are five-run medians from the historical measurement in million input items per
second, with the observed range in parentheses:

| Path | MSVC `/O2` | Clang `-O3` |
|---|---:|---:|
| generated Direct | 2,368.7 (2,086.0-2,549.2) | 2,146.8 (2,103.4-2,331.4) |
| typed fused caller buffer | 3,122.7 (2,341.4-3,374.7) | 2,866.3 (2,519.6-3,023.2) |
| typed fused owned result | 2,769.2 (2,312.3-2,941.5) | 2,805.7 (1,961.5-2,855.6) |
| typed staged owned result | 1,260.0 (1,113.0-1,304.5) | 1,829.6 (1,236.9-1,915.6) |
| bound erased fused caller buffer | 49.9 (46.2-52.1) | 58.7 (53.4-65.6) |
| production Plan | 42.1 (38.8-43.0) | 53.7 (51.6-55.2) |

Neither Release configuration enables link-time optimization. Optimized Clang
machine code shows the Direct loop inlined and unrolled by two inputs, while the
typed staged map loops contain SIMD integer multiply and double conversion/multiply
instructions. `step_filter` and `step_map` retain relocations to
`cmeta_callable_invoke`; `step_map` also retains allocator calls and a separate
loop for every map callback.

For this fixed workload, Plan invokes 2,048 bound erased callables per evaluation:
1,024 filters and two maps over 512 retained values. On Win64, where `long` is
four bytes, it performs three allocations and frees totalling 10,240 allocated
bytes, with 6,144 peak live bytes. It copies 4,096 input bytes, compacts 2,044
bytes for this alternating predicate, and materializes 6,144 map-output bytes.
On LP64 the corresponding allocation total is 12,288 bytes and peak live storage
is 8,192 bytes. These byte and call counts are source-derived calculations, not
allocator instrumentation.

The erased fused control is only 1.19 times faster than Plan on MSVC and 1.09
times faster on Clang. In contrast, replacing direct typed calls with bound
erased calls reduces the controlled fused path by roughly 49-63 times. Therefore
scalar erased invocation is the dominant cost for this trivial pipeline;
allocation, copying, stage materialization and instruction dispatch together
account for the remaining approximately 8-16% of Plan time in these runs. The
typed controls still show that Plan-shaped memory materialization costs 1.5-2.2
times versus one owned typed output, so memory optimization remains worthwhile
after dispatch is addressed.

Windows Performance Recorder could not start the CPU profile because the process
lacked the system-performance profiling policy (`0xc5585011`). No sampling-trace
claim is made; the attribution above rests on controlled benchmarks, source
accounting and optimized object-code inspection.

## Optimization Direction

The first Plan optimization should predecode each supported CMeta signature into
a signature-specialized instruction kernel so validation, signature switching
and argument/result byte copies are not repeated for every item. This still
retains an indirect user-function call and therefore cannot be expected to match
same-translation-unit Direct inlining.

The memory follow-up should fuse eligible Filter/Map instructions into one item
loop, borrow the immutable input directly, allocate only the final maximum-sized
result, and use bounded per-evaluation scratch for type-changing intermediate
values. A reusable caller-owned evaluation context could later amortize result
and scratch allocation without making immutable plans stateful or weakening
concurrent evaluation. Near-Direct performance ultimately requires AOT-generated
or JIT-specialized code because a dynamic Plan cannot inline arbitrary callable
targets.

## Rollback

Removing the benchmark-only paths and these investigation documents restores the
previous state. No persisted data, generated format or public consumer requires
migration.
