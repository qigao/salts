# CFlow Network Stage Timing Attribution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split CFlow network benchmark post-admission timing into drive, wait, completion-processing, and residual components without breaking existing result consumers.

**Architecture:** Timing remains private to `cflow_network_benchmark.c`; no CFlow runtime API changes. The benchmark emits additive version-2 JSON fields, while the PowerShell statistics and release workflow validate and summarize the new contract and continue accepting legacy version-1 fixtures.

**Tech Stack:** C11, TurboUtils monotonic timing, TinyTest, PowerShell, GitHub Actions, CMake Presets

**Spec:** `docs/superpowers/specs/2026-08-29-cflow-network-stage-timing-design.md`

## Global Constraints

- Keep the JSON schema string `cflow-network-benchmark/v1` and every existing field.
- Do not change public CFlow headers, runtime state, backend state machines, or ownership.
- Enabled version-2 records must satisfy `completion_drive_ns >= drive_ns + wait_ns + completion_process_ns` and emit the exact difference as `completion_residual_ns`.
- Disabled timing emits version `0` and zero for every total and mean.
- Use checked arithmetic and fail fast on overflow or inconsistent decomposition.
- Do not commit or push until the user explicitly requests it.

---

### Task 1: Statistics contract

**Files:**
- Modify: `.github/tests/cflow-benchmark-stats-test.ps1`
- Modify: `.github/scripts/cflow-benchmark-stats.ps1`

**Interfaces:**
- Consumes: benchmark report properties `stage_timing_version`, `drive_mean_ns`, `wait_mean_ns`, `completion_process_mean_ns`, and `completion_residual_mean_ns`.
- Produces: `Assert-CflowStageTimingReport` validation and paired median/delta summary properties for each version-2 component.

- [x] **Step 1: Write failing PowerShell tests**

Add version-2 fixture properties, assert component medians and paired deltas,
assert malformed decompositions throw, and retain one fixture without a
version field to verify legacy acceptance.

- [x] **Step 2: Run the test and observe the missing-contract failure**

Run: `pwsh -NoProfile -File .github/tests/cflow-benchmark-stats-test.ps1`

Expected: FAIL because `Assert-CflowStageTimingReport` or the new summary
properties do not exist.

- [x] **Step 3: Implement the minimal statistics contract**

Add a validator that accepts absent version as legacy v1, enforces all v2
totals/means and exact residual arithmetic, and enforces zero values for
disabled version-0 reports. Replace new-run combined-stage interpretation with
named component summaries while retaining legacy summary properties.

- [x] **Step 4: Run the PowerShell tests**

Run: `pwsh -NoProfile -File .github/tests/cflow-benchmark-stats-test.ps1`

Expected: PASS.

### Task 2: Benchmark timing decomposition

**Files:**
- Modify: `cflow/benchmarks/cflow_network_benchmark.c`

**Interfaces:**
- Consumes: `turbo_hrtime()`, Actor `network_pump`, Source `network_source_pump`, cond-wait, yield, result-copy, and acknowledge boundaries.
- Produces: fixture totals and JSON fields `drive_ns`, `wait_ns`, `completion_process_ns`, `completion_residual_ns`, plus corresponding means and `stage_timing_version`.

- [x] **Step 1: Write failing TinyTest assertions**

Extend the stage accumulator test to require atomic updates for every
component, exact residual derivation, and overflow rejection. Extend the real
Source stage test to require a valid non-zero decomposed sample.

- [x] **Step 2: Build and run the focused test to observe failure**

Run the repository's `win-release-user` build preset for target
`cflow_network_benchmark`, then run the TinyTest filter for network stage
measurement tests.

Expected: compilation or assertion failure because the new fields and
accumulator parameters are absent.

- [x] **Step 3: Implement local timing samples and accumulator checks**

Measure each pump, wait, and completion-processing boundary only when stage
timing is enabled. Accumulate a batch atomically, derive residual only after
validating the component sum, and retain the legacy full post-admission total.

- [x] **Step 4: Emit additive version-2 JSON fields**

Emit version `2` for enabled timing and `0` otherwise, with integer totals and
three-decimal means for all four new components.

- [x] **Step 5: Run focused and adjacent benchmark tests**

Run the focused stage tests, then the complete `cflow_network_benchmark`
TinyTest suite through the configured Release preset.

Expected: PASS with no benchmark errors, rejections, or stale completions.

### Task 3: Release workflow contract

**Files:**
- Modify: `.github/workflows/cflow-release-benchmarks.yml`

**Interfaces:**
- Consumes: version-2 JSON fields and `Assert-CflowStageTimingReport` from `.github/scripts/cflow-benchmark-stats.ps1`.
- Produces: fail-fast artifact validation and Markdown tables that identify drive, wait, completion-processing, and residual costs separately.

- [x] **Step 1: Add version-2 required-field and invariant checks**

Require the additive fields for newly emitted stage-timed reports, use the
shared validator, and require disabled records to report version `0` with all
stage fields zero.

- [x] **Step 2: Replace misleading table labels**

Keep legacy combined-stage columns only where historical v1 data is compared;
add named version-2 component columns for current Actor/Source and Source-window
summaries.

- [x] **Step 3: Parse and execute workflow PowerShell boundaries**

Parse every embedded `pwsh` block and run the repository statistics test.

Expected: all blocks parse and the statistics test passes.

### Task 4: Verification and self-review

**Files:**
- Review: all files modified by Tasks 1-3

**Interfaces:**
- Consumes: the design invariants and built benchmark artifact.
- Produces: reproducible Windows evidence and a clean scoped diff ready for user-requested commit/push.

- [x] **Step 1: Run the smallest relevant tests**

Run the statistics test and focused stage timing TinyTests.

- [x] **Step 2: Run adjacent Release regression tests**

Run the repository CTest filter that owns `cflow_network_benchmark` and any
registered CFlow network benchmark correctness tests.

- [x] **Step 3: Inspect one emitted timed and untimed JSON record**

Confirm version values, all required fields, exact residual arithmetic, and
zero-valued disabled fields.

- [x] **Step 4: Review the final diff**

Check that no public header or runtime implementation changed, no generated
artifact or `.codegraph/` file is staged, and all legacy JSON fields remain.

### Task 5: Version-3 statistics contract

**Files:**
- Modify: `.github/tests/cflow-benchmark-stats-test.ps1`
- Modify: `.github/scripts/cflow-benchmark-stats.ps1`

- [x] **Step 1: Add a valid version-3 fixture and malformed nested sums**

Require `dispatch`, `executor`, `completion-ready`, `wake-resume`, and both
nested residual totals and means. Retain explicit version-2 coverage.

- [x] **Step 2: Run the PowerShell test and observe version 3 rejection**

Run: `pwsh -NoProfile -File .github/tests/cflow-benchmark-stats-test.ps1`

- [x] **Step 3: Accept and validate versions 0, 2, and 3**

Version 0 requires every present field to be zero and accepts historical
records without version-3 fields; version 2 remains readable; version 3
additionally enforces both nested conservation equations.

### Task 6: Version-3 benchmark measurements

**Files:**
- Modify: `cflow/benchmarks/cflow_network_benchmark.c`

- [x] **Step 1: Add failing deterministic accumulator and wait-split tests**

Exercise exact nested residuals, atomic overflow rejection, signal-before-
resume attribution, and no-signal residual attribution without relying on wall
clock duration.

- [x] **Step 2: Build the benchmark target and observe the focused failure**

- [x] **Step 3: Implement private pump and wake-latch instrumentation**

Time Actor/Source dispatch and execution boundaries. Store the first pending
wake timestamp under the existing latch mutex and clear it with the pending
edge. Do not change runtime APIs or synchronization topology.

- [x] **Step 4: Emit additive version-3 totals and means**

Keep the schema string and every legacy field; emit version `3` only for
enabled timing and version `0` otherwise.

### Task 7: Compact handoff report

**Files:**
- Modify: `.github/scripts/cflow-benchmark-stats.ps1`
- Modify: `.github/tests/cflow-benchmark-stats-test.ps1`
- Modify: `.github/workflows/cflow-release-benchmarks.yml`

- [x] **Step 1: Add paired v3 summary assertions**

- [x] **Step 2: Generate `transport-handoff.md`**

Use separate TCP/UDP sections and one row per Actor/Source payload. Show only
dispatch, execution, drive residual, completion-ready, wake-resume, and wait
residual medians in ns/op.

- [x] **Step 3: Parse embedded workflow PowerShell and run stats tests**

### Task 8: Version-3 verification and review

- [x] **Step 1: Run PowerShell and focused TinyTest tests**
- [x] **Step 2: Run adjacent Release benchmark tests**
- [x] **Step 3: Inspect emitted timed and disabled JSON records**
- [x] **Step 4: Review compatibility, ownership, and scoped diff**
