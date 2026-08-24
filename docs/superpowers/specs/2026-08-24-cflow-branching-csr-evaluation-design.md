# CFlow Branching CSR Evaluation Design

## Background

Issue #46 asks for benchmark evidence before a private CSR-derived view may be
used by a branching CFlow phase. The current Graph keeps one flat edge array as
the topology fact source. Validation, normalization, optimization and Plan
compilation derive a dense single-successor index because valid naked DATA
fan-out is rejected; user-visible branching is represented by `RELATION` nodes
that reference nested, individually linear subgraphs.

This distinction is part of the decision. A general-adjacency control can show
what CSR costs on fan-out shapes, but it is not evidence that an existing valid
CFlow runtime path benefits from CSR.

## Decision Scope

Add a benchmark-only adjacency laboratory. It derives immutable views from a
borrowed `cflow_subgraph`, compares their traversal and construction costs, and
ships differential tests. It does not change `cflow_graph`, `cflow_subgraph`,
Graph validation, lowering, optimization, runtime execution or the Plan tape.

The compared paths are:

- current public flat-edge lookup: one `out_degree` scan plus one outgoing-edge
  scan per node;
- one pass over the flat fact-source edge array as the lower-bound control;
- a TurboSTL HashMap from source node to a contiguous target span;
- pointer-linked adjacency with one node head and one link per edge; and
- CSR with `V + 1` `size_t` offsets and `E` `cflow_node_id` targets.

The dense successor view remains the relevant production reference for valid
single-path subgraphs. It is reported separately because it cannot represent a
general fan-out adjacency list.

## Workloads

Every synthetic workload has source-grouped flat edges for timed parity, while
the differential tests also use interleaved source order to prove that each
derived view preserves the original order within a source interval.

| Case | Vertices | Edges | Purpose |
| --- | ---: | ---: | --- |
| Empty | 1 | 0 | zero-edge boundary |
| Single edge | 2 | 1 | minimal non-empty boundary |
| Typical sparse DAG | 256 | 384 | ordinary sparse control |
| High fan-out | 257 | 256 | one hub and many targets |
| Skewed degree | 1024 | bounded by 2046 | hub plus low-degree tail |
| Peak sparse DAG | 4096 | bounded by 8190 | configured peak memory/traversal |

A separate valid nested-RELATION fixture contains multiple linear branch
subgraphs. Its complete `cflow_graph_validate` lifecycle is timed as the real
branching control-plane path. The adjacency rows traverse every constituent
subgraph without treating relation references as DATA edges.

## Correctness Contract

For every successful derived view:

- every valid flat edge occurs exactly once for its source;
- outgoing targets for one source retain their flat-edge appearance order;
- CSR offsets are monotonic, `offsets[0] == 0`, and `offsets[V] == E`;
- traversal reports the same edge count and order-sensitive per-source digest
  as the flat reference;
- endpoint, addition and multiplication overflow fail before publication; and
- invalid input or allocation failure leaves the output in a reusable zero
  state.

Test-only allocator injection forces failure after the first successful CSR
allocation so transactional cleanup is directly exercised. Production CFlow
allocation behavior is unchanged.

## Ownership and Memory Protocol

The borrowed flat subgraph is the sole topology fact source. A fixture owns its
edge storage. Each derived view owns only its private allocations from build
success until destroy; it is immutable and single-threaded during traversal.
Mutation or destruction of the source invalidates all derived views, so the
benchmark destroys the views before the fixture.

Capacity is exactly the checked `V`/`E` input. There is no growth, retry,
fallback or hidden unbounded allocation. Build failures return a distinct
status and publish no partial view. Destroy restores zero state.

Memory output uses bytes passed to the underlying heap allocator, not process
RSS or allocator-private headers:

- allocation count: number of requested heap blocks, including transient
  HashMap key/value preparation blocks;
- allocated bytes: cumulative requested bytes during one complete build;
- retained bytes: requested bytes still owned after successful build;
- peak bytes: maximum concurrently requested bytes during build.

HashMap bookkeeping is calculated from its public capacity/stride/alignment
fields and the current TurboSTL single-storage-block contract. It includes the
`sequence_allocate` alignment prefix in each map, prepared-key and
prepared-value `malloc` request. Platform allocator headers and heap rounding
remain outside the measurement.

## Measurement Contract

Construction rows time one complete build-and-destroy lifecycle. Traversal rows
construct outside the timed block, traverse a complete immutable adjacency,
and publish a volatile digest. Correctness assertions execute outside timing.

TinyTest `benchmark_batch` is used for lifecycle rows and `benchmark_ops` for
edge traversal rows. Empty, single-edge, typical, fan-out, skewed and peak
shapes are all timed; the empty traversal uses one boundary-work unit because
it has no edge operation to count. Shared runners produce raw evidence only
and enforce no wall-clock threshold.

The existing fixed-host workflow runs the combined executable five times on
Ubuntu 22.04/24.04 GCC and Windows 2022/2025 MSVC Release hosts, retaining raw
stdout and host/compiler metadata as artifacts.

## Adoption Rule

CSR may enter a production phase only when both conditions are demonstrated on
that real valid phase:

1. topology lookup is at least 20% of total time or exceeds 1000 lookups per
   second; and
2. CSR improves that phase by more than 30% without increasing retained or
   peak requested bytes by more than 20% against its current representation.

Synthetic naked-fan-out wins cannot satisfy the first condition. A failure of
either condition selects rejection: keep the benchmark evidence and the dense
single-successor production view, and do not add CSR to CFlow runtime code.

## Local Release Evidence and Decision

The local evidence was collected on Windows 11 10.0.26200, an AMD Ryzen 9
7940HX, MSVC 19.44.35217 x64 and `CMAKE_BUILD_TYPE=Release`. The valid nested
fixture contains 11 subgraphs, 273 nodes and 262 edges. The dense consumer
performs exactly one successor lookup for each source node, or 273 lookups per
lifecycle. Each timed sample repeats the complete lifecycle 64 times, for
17,472 successor lookups and roughly 78--145 microseconds per representation
sample. Five independent runs produced these normalized per-lookup times:

| Run | Complete validation (ns) | Dense lifecycle (ns) | CSR lifecycle (ns) |
| ---: | ---: | ---: | ---: |
| 1 | 22.703 | 4.494 | 7.994 |
| 2 | 23.640 | 4.518 | 8.155 |
| 3 | 22.971 | 4.478 | 8.174 |
| 4 | 23.491 | 4.644 | 8.152 |
| 5 | 23.441 | 4.606 | 8.271 |
| Median | 23.441 | 4.518 | 8.155 |

The standalone production dense lifecycle is an estimated
`4.518 / 23.441 = 19.27%` of the measured validation time. More directly, its
273 actual successor calls per lifecycle yield
`1e9 / 4.518 = 221,336,875` calls per second when construction, traversal and
destruction are all charged to those calls. This satisfies the issue's
greater-than-1000-lookups-per-second profiling alternative; it does not claim
to satisfy the separate 20% time-share alternative.

For the same valid nested fixture, allocator-request accounting reports:

| Representation | Allocations | Allocated bytes | Retained bytes | Peak bytes |
| --- | ---: | ---: | ---: | ---: |
| Production dense successor | 11 | 1,092 | 132 | 132 |
| Candidate CSR | 22 | 3,320 | 400 | 400 |

Against the production reference, CSR changes the median lifecycle time by
`(8.155 - 4.518) / 4.518 = +80.50%` and both retained and peak requested
bytes by `(400 - 132) / 132 = +203.03%`. It therefore fails both adoption
gates: it does not improve the measured valid phase by more than 30%, and its
memory regression is greater than 20%.

The decision is **reject CSR for production CFlow phases**. The flat edge array
remains the topology fact source and the private dense successor remains the
derived representation used by validation, lowering, optimization and Plan
compilation. Synthetic general-adjacency rows remain in the benchmark as
reproducible controls, but cannot override the valid-Graph result.

### Fixed-host confirmation

[CFlow release host benchmarks run 118](https://github.com/qigao/turbo-utils/actions/runs/32712195163)
completed successfully on all four fixed Release hosts. Every artifact contains
host/compiler metadata, five direct benchmark runs and five parallel-reduce
runs. All 20 direct runs passed. Median valid nested results are:

| Host | Compiler | Dense (ns/lookup) | CSR (ns/lookup) | CSR time change |
| --- | --- | ---: | ---: | ---: |
| Ubuntu 22.04 | GCC 11.4.0 | 4.853 | 7.847 | +61.69% |
| Ubuntu 24.04 | GCC 13.3.0 | 5.419 | 7.229 | +33.40% |
| Windows 2022 | MSVC 19.44.35228 | 5.173 | 10.798 | +108.74% |
| Windows 2025 | MSVC 19.51.36256 | 5.836 | 10.057 | +72.33% |

Each host reports the same deterministic allocator-request totals: dense
retains and peaks at 132 bytes, while CSR retains and peaks at 400 bytes
(`+203.03%`). CSR therefore fails the performance and memory gates on every
fixed host. These shared-runner measurements confirm the rejection decision;
they remain evidence rather than timing gates.

## Compatibility and Rollback

The change adds only non-installed benchmark support, a test executable and
evidence documents. Public ABI, errors, ordering and runtime behavior remain
unchanged. Rollback removes those benchmark/test sources and documents; no
consumer migration or persisted-data action is required.
