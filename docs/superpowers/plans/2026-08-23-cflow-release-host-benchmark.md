# CFlow Release Host Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans
> to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for
> tracking.

**Goal:** Collect reproducible CFlow Direct/Plan Release benchmark evidence from
multiple supported GitHub-hosted runner images.

**Architecture:** Add one dedicated matrix workflow. Reuse existing platform
Release presets, build only the benchmark target, run five samples per host,
and upload raw output plus enough host/compiler metadata to interpret it. Do not
turn heterogeneous shared-runner measurements into a throughput gate.

**Tech Stack:** GitHub Actions, CMake Presets, Ninja, vcpkg, MSVC, GCC,
PowerShell, TinyTest benchmark output.

**Spec:** `docs/superpowers/specs/2026-08-23-cflow-release-host-benchmark-design.md`

## Constraints

- Use Release configuration only.
- Preserve all CFlow/CMeta behavior and benchmark workload semantics.
- Pin OS runner labels and record runner image revisions.
- Fail fast on setup/build/run errors and missing artifacts.
- Keep raw per-run evidence; do not enforce cross-host throughput thresholds.

## Task 1: Establish the Release baseline

**Files:**

- Inspect: `.github/workflows/cmeta.yml`
- Inspect: `CMakeUserPresets.json`
- Inspect: `cflow/benchmarks/CMakeLists.txt`
- Inspect: `cflow/benchmarks/cflow_direct_benchmark.c`

- [x] Confirm PR #35's merged base and create an isolated feature worktree.
- [x] Confirm the existing Linux/GCC and Windows/MSVC Release presets.
- [x] Build and run the unchanged benchmark with MSVC Release.
- [x] Verify semantic assertions are outside timing and pass before CI changes.

## Task 2: Add the fixed-host Release matrix

**Files:**

- Create: `.github/workflows/cflow-release-benchmarks.yml`

- [x] Add pull-request, master-push and manual triggers with focused paths.
- [x] Add Ubuntu 22.04, Ubuntu 24.04, Windows 2022 and Windows 2025 entries.
- [x] Reuse `linux-release-user` and `win-release-user` with
  `BUILD_BENCHMARKS=ON`, `BUILD_TESTS=ON` and target-only builds.
- [x] Run five sequential benchmark samples and fail on any non-zero exit.

## Task 3: Preserve interpretable evidence

**Files:**

- Modify: `.github/workflows/cflow-release-benchmarks.yml`

- [x] Capture commit, runner image, OS, CPU, compiler and CMake configuration.
- [x] Retain every raw benchmark output in a host-specific artifact.
- [x] Publish one representative run in the GitHub job summary.
- [x] Document why heterogeneous shared runners have no common hard threshold.

## Task 4: Verify and deliver

**Files:**

- Inspect: all changed files and generated CI results

- [x] Reconfigure a clean local Release tree with the workflow cache variables.
- [x] Build and run `cflow_direct_benchmark` successfully.
- [x] Validate workflow structure, matrix cardinality and Release-only contract.
- [x] Review the diff for production behavior changes and secret/path leakage.
- [ ] Commit, push, open a PR against
  `test/cflow-calculus-conformance-phase-f1`, and inspect all matrix jobs.
