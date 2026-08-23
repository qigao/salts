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
runs it repeatedly on these fixed x64 runner images:

- Ubuntu 22.04 with the `linux-release-user` GCC preset;
- Ubuntu 24.04 with the `linux-release-user` GCC preset;
- Windows Server 2022 with the `win-release-user` MSVC preset; and
- Windows Server 2025 with the `win-release-user` MSVC preset.

Each matrix entry performs five sequential runs. It records the commit, runner
image, OS, CPU, compiler configuration, CMake cache and complete benchmark
stdout. Each host uploads a separate artifact and exposes one representative
run in the GitHub job summary.

The workflow uses fixed OS labels rather than `*-latest`, following GitHub's
[runner image label guidance](https://github.com/actions/runner-images#available-images).
The runner image itself can still change over time, so `ImageOS` and
`ImageVersion` are part of the evidence.

## Measurement Contract

- Configuration must be `Release`; Debug and sanitizer measurements are out of
  scope.
- `BUILD_BENCHMARKS=ON` and `BUILD_TESTS=ON` satisfy the repository's current
  dependent-option contract; the target-only build compiles just the benchmark
  and its dependencies.
- The benchmark remains the sole source of workload size, correctness checks,
  timing regions and throughput units.
- Runs on different hosts or runner-image revisions are evidence samples, not
  directly comparable performance gates.
- The workflow fails on configuration, compilation, a missing executable, a
  non-zero benchmark result or a missing artifact.
- No throughput threshold is enforced because GitHub-hosted runner allocation,
  CPU model and contention are not controlled by this repository.

## Compatibility and Risk

This change does not modify CFlow/CMeta APIs, execution semantics, ownership,
IR, allocator behavior or benchmark workload. CI cost increases by four Release
jobs when relevant files change. Fixed runner labels reduce OS migration noise,
while the uploaded metadata preserves the remaining runner-image variability.

macOS is not included in this phase: the repository has a base macOS Release
preset, but no CI-tested user preset with the same vcpkg manifest contract used
by Linux and Windows. Adding it here would combine platform enablement with
benchmark collection.

## Verification

- Configure and build `cflow_direct_benchmark` locally with
  `win-release-user`, `BUILD_BENCHMARKS=ON` and `BUILD_TESTS=ON`.
- Run the Release executable and confirm all semantic assertions pass.
- Validate the workflow syntax and its four explicit matrix entries.
- Open the PR against PR #35's merged base and inspect every matrix job and
  uploaded artifact.

## Rollback

Removing the workflow and these two design/plan documents restores the previous
state. No persisted data or consumer migration is involved.
