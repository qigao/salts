# CFlow Release Host Benchmark Design

## Background

PR #35 established a correctness-checked benchmark that decomposes the CFlow
Direct/Plan performance gap. Its local results are valid only for the measured
machine, compiler and runner state. A repeatable CI path is needed to collect
the same evidence from multiple hosts without treating shared GitHub runners as
stable performance infrastructure.

## Decision

Add a dedicated GitHub Actions workflow that builds only
`cflow_direct_benchmark` with the repository's existing Release presets and
runs it repeatedly on these fixed x64 runner images. The benchmark executable
contains both the established Direct/Plan data-path suite and a separate
Graph-path representation suite:

- Ubuntu 22.04 with `release-linux-ninja` for configure and
  `build-default-linux` for build;
- Ubuntu 24.04 with the same Linux configure/build preset pair;
- Windows Server 2022 with `release-win-msvc-ninja` for configure and
  `build-release-windows` for build; and
- Windows Server 2025 with the same Windows configure/build preset pair.

Each matrix entry performs five sequential runs. It records the commit, runner
image, OS, CPU, compiler configuration, CMake cache and complete benchmark
stdout. Each host uploads a separate artifact and exposes one representative
run in the GitHub job summary.

The workflow uses fixed OS labels rather than `*-latest`, following GitHub's
[runner image label guidance](https://github.com/actions/runner-images#available-images).
The runner image itself can still change over time, so `ImageOS` and
`ImageVersion` are part of the evidence.

## Graph-Path Representation Contract

The additional suite isolates control-plane traversal after a linear Graph has
already been built, validated and normalized. It compares six equivalent views
derived from that same Graph:

- the current `out_degree` plus `single_successor` flat-edge API path;
- one flat-edge scan that resolves zero or one successor;
- a dense node-id-to-successor array;
- a pointer-linked degenerate tree for the same linear path;
- a bounded TurboSTL HashMap from node id to successor; and
- the compiled Plan's contiguous instruction tape.

The Graph, derived indexes and Plan are constructed before timing. Every timed
sample traverses one complete path and produces the same operator count and
order-sensitive checksum. Correctness is asserted before timing at boundary,
typical and peak sizes; timed regions contain traversal and checksum work only.

The fixed fixture owns every representation until the case completes. Dense
and tree views use compile-time-bounded storage sized for the peak case. The
HashMap owns copied keys and values, has an explicit entry limit, and reserves
the exact edge count before timing. All views are immutable and single-threaded
during measurement. Destroying the fixture releases Plan, Graph and HashMap
storage after the benchmark case.

This suite measures topology lookup and pointer/layout costs. It does not run
callbacks, allocate result buffers or process input values, so its results must
not be presented as Direct-versus-Plan execution throughput. The Plan tape is a
lowered sequential representation; the other rows are candidate Graph lookup
representations, not proposed public APIs.

For a linear path with `V` nodes and `E = V - 1` edges, the measured traversal
and representation budgets are:

| View | Setup | One traversal | Additional payload |
| --- | --- | --- | --- |
| Current Graph APIs | none | `Theta(V * E)`; two edge scans for non-terminal nodes | none |
| One-pass flat edges | none | `Theta(V * E)`; one edge scan per node | none |
| Dense successor array | `Theta(V + E)` | `Theta(V)` | `V * sizeof(cflow_node_id)` |
| Linked path tree | `Theta(V + E)` | `Theta(V)` | `V * sizeof(path_node)` including pointer alignment |
| TurboSTL HashMap | expected `Theta(E)` | expected `Theta(V)` | bounded states, hashes, aligned keys and aligned values at reserved capacity |
| Compiled Plan tape | current compile path includes `Theta(V * E)` Graph scans | `Theta(V)` | Plan implementation, instructions and prepared call-chain payload |

The benchmark fixture reserves peak-bounded dense and linked storage once; the
table describes the logical payload a production representation would require.
HashMap and Plan allocator bookkeeping is implementation-dependent, so the
suite does not claim an exact resident-set size. In particular, the Plan row
times only the already-compiled tape. It cannot hide or amortize the current
flat-edge scans used by `cflow_plan_graph_supported` and `cflow_plan_compile`.

## Measurement Contract

- Configuration must be `Release`; Debug and sanitizer measurements are out of
  scope.
- `BUILD_BENCHMARKS=ON` and `BUILD_TESTS=ON` satisfy the repository's current
  dependent-option contract; the target-only build compiles just the benchmark
  and its dependencies.
- The benchmark remains the sole source of workload sizes, correctness checks,
  timing regions and throughput units. Data-path and graph-path rows retain
  distinct labels and work units.
- Runs on different hosts or runner-image revisions are evidence samples, not
  directly comparable performance gates.
- The workflow fails on configuration, compilation, a missing executable, a
  non-zero benchmark result or a missing artifact.
- No throughput threshold is enforced because GitHub-hosted runner allocation,
  CPU model and contention are not controlled by this repository.

## Compatibility and Risk

This change does not modify CFlow/CMeta APIs, execution semantics, ownership,
IR or allocator behavior. It extends only the non-installed benchmark target
and adds a private benchmark dependency on TurboSTL for the HashMap control.
CI cost increases through the added suite within the existing four Release
jobs. Fixed runner labels reduce OS migration noise, while the uploaded metadata
preserves the remaining runner-image variability.

macOS is not included in this phase: the repository has a base macOS Release
preset, but no CI-tested repository-level preset pair with the same vcpkg
manifest contract used by Linux and Windows. Adding it here would combine
platform enablement with benchmark collection.

## Verification

- Configure with `release-win-msvc-ninja`, then build
  `cflow_direct_benchmark` with `build-release-windows`,
  `BUILD_BENCHMARKS=ON` and `BUILD_TESTS=ON`.
- Run the Release executable and confirm all data-path semantic assertions and
  graph-path representation equivalence assertions pass.
- Validate the workflow syntax and its four explicit matrix entries.
- Open the PR against PR #35's merged base and inspect every matrix job and
  uploaded artifact.

## Rollback

Removing the workflow and these two design/plan documents restores the previous
state. No persisted data or consumer migration is involved.
