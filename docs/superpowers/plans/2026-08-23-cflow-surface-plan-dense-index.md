# CFlow Surface-to-Plan Dense Successor Index Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Remove repeated flat-edge scans from Surface normalization and
optimization without changing public CFlow semantics or layouts.

**Architecture:** Reuse the private, phase-owned dense successor view once per
source subgraph. Thread it through lower and optimizer path helpers, while the
flat edge array remains the sole topology fact source.

**Tech Stack:** C11, CMake Presets, TinyTest, MSVC Release.

**Spec:** `docs/superpowers/specs/2026-08-23-cflow-surface-plan-dense-index-design.md`

## Task 1: Establish staged Release evidence

**Files:**

- Modify: `cflow/benchmarks/cflow_graph_path_benchmark.c`

- [x] Add correctness-checked normalize, optimize and full Surface-to-Plan
  lifecycle helpers.
- [x] Keep fixture construction outside timed regions and phase-owned outputs
  inside timed regions.
- [x] Build the Release benchmark and run it five times before production code
  changes.
- [x] Record per-stage medians at 1, 16, 256 and 4096 operators.

## Task 2: Characterize edge-order-independent phase behavior

**Files:**

- Modify: `cflow/tests/cflow_pipeline_test.c`

- [x] Build a valid linear Surface Graph and reorder its flat edge storage.
- [x] Assert normalization, optimization and Surface-to-Plan compilation remain
  valid and produce the expected instruction count.
- [x] Run the focused test before production changes to preserve current
  behavior as a characterization contract.

## Task 3: Index normalization traversal

**Files:**

- Modify: `cflow/src/lower.c`

- [x] Build exactly one dense successor view per visited source subgraph.
- [x] Replace the per-node flat scan with indexed lookup.
- [x] Map allocation and invariant failures to explicit normalization errors.
- [x] Destroy the local view on every success and failure path.
- [x] Run the focused pipeline and dense-index tests until GREEN.

## Task 4: Index optimizer traversal and MAP fusion

**Files:**

- Modify: `cflow/src/opt.c`

- [x] Build exactly one dense successor view per optimized source subgraph.
- [x] Pass it through main traversal, MAP-chain discovery, callable copying and
  canonicalization accounting.
- [x] Preserve rewrite stats, proof-trace event order and transactional cleanup.
- [x] Map allocation and invariant failures to explicit optimizer errors.
- [x] Run optimizer, pipeline and dense-index tests until GREEN.

## Task 5: Measure, verify and deliver

**Files:**

- Inspect: all changed production, test, benchmark and design files.

- [x] Run the staged Release benchmark five times after optimization and
  calculate same-host median speedups.
- [x] Build all CFlow test and benchmark targets in MSVC Release.
- [x] Run the complete CFlow test set with output on failure.
- [x] Inspect public-header, ABI and final-diff scope.
- [ ] Commit, push and open a PR stacked on `perf/cflow-plan-dense-index`.
