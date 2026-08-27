# CFlow Network Benchmark Methodology Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the CFlow network benchmark dataset support paired Actor/Reactive Source conclusions and same-runner Linux backend comparisons without changing runtime behavior or the `cflow-network-benchmark/v1` producer.

**Architecture:** Move deterministic ordering and paired statistics into a small PowerShell helper that the workflow and a standalone test both execute. Keep every benchmark JSON record intact, attach workflow-only `benchmark_run` metadata when writing comparison JSONL, rotate execution order to reduce drift, and collect epoll/io_uring/poll in one Ubuntu 24 job so backend comparisons share a runner.

**Tech Stack:** PowerShell 7, GitHub Actions YAML, CFlow network benchmark JSONL, actionlint.

**Spec:** `docs/superpowers/plans/2026-08-27-cflow-network-source-benchmark.md`

## Global Constraints

- Preserve `cflow-network-benchmark/v1` producer fields and CFlow runtime behavior.
- Keep five independent process runs per configuration and retain every raw JSON record.
- Report paired deltas as the median of per-run percentages, never as the percentage between unrelated medians.
- Run Actor/Source diagnostics on every host/backend with the same raw TCP 64-byte latency workload and enabled stage timing.
- Compare Linux epoll/io_uring/poll only when collected sequentially inside one Ubuntu 24 runner.
- Keep admission and completion/drive fields for compatibility; describe admission as API handoff and use their sum for the comparable stage total.

---

### Task 1: Tested benchmark statistics and ordering helper

**Files:**
- Create: `.github/scripts/cflow-benchmark-stats.ps1`
- Create: `.github/tests/cflow-benchmark-stats-test.ps1`

**Interfaces:**
- Produces: `Get-CflowMedian -Values <double[]> -> double`.
- Produces: `Get-CflowRotatedOrder -Values <string[]> -Run <int> -> string[]`.
- Produces: `Get-CflowDriverOrder -Run <int> -WaitMode <blocking|busy> -BackendIndex <int> -> string[]`.
- Produces: `Get-CflowPairedSourceSummary -Reports <object[]> -Backend <string> -WaitMode <string> -ExpectedRuns <int> -> PSCustomObject`.

- [x] **Step 1: Write the failing test**

  Add literal fixtures for runs 1 through 3 and assert median calculation, cyclic backend ordering, balanced driver ordering, rejection of a missing pair, and these hand-calculated paired results:

  ```powershell
  $summary = Get-CflowPairedSourceSummary -Reports $reports `
    -Backend epoll -WaitMode blocking -ExpectedRuns 3
  Assert-Equal $summary.paired_p50_delta_pct 10.0
  Assert-Equal $summary.paired_p99_delta_pct 20.0
  Assert-Equal $summary.source_slower_p50_runs 2
  ```

- [x] **Step 2: Run the test to verify RED**

  Run: `pwsh -NoProfile -File .github/tests/cflow-benchmark-stats-test.ps1`

  Expected: failure because `.github/scripts/cflow-benchmark-stats.ps1` does not exist.

- [x] **Step 3: Implement the minimal helper**

  Validate nonempty values, positive runs, supported wait modes, unique run IDs, exact Actor/Source pairs, finite positive denominator metrics, and return both driver medians plus paired P50/P99/wall/CPU-time/CPU-efficiency/combined-stage deltas.

- [x] **Step 4: Run the test to verify GREEN**

  Run: `pwsh -NoProfile -File .github/tests/cflow-benchmark-stats-test.ps1`

  Expected: exit 0 with `cflow benchmark stats tests passed`.

- [x] **Step 5: Re-run after mutation checks**

  Temporarily verify that changing one fixture's `benchmark_run` to a duplicate or removing a Source record fails; restore the literal fixture and confirm GREEN.

### Task 2: Same-runner backend matrix and cross-platform Source dataset

**Files:**
- Modify: `.github/workflows/cflow-release-benchmarks.yml`

**Interfaces:**
- Consumes: all Task 1 helper functions.
- Produces: per-host `network-results.jsonl`, `dual-native-results.jsonl`, and `reactive-source-results.jsonl` with a `benchmark_run` field added by the workflow.

- [x] **Step 1: Consolidate Ubuntu 24 backend jobs**

  Replace the three Ubuntu 24 matrix entries with one entry containing `network_backends: epoll,io_uring,poll`; single-backend hosts use `epoll`, `iocp`, or `kqueue`. Export `NETWORK_BACKENDS`, dot-source the helper, validate every requested backend, and rotate backend order per run.

- [x] **Step 2: Preserve complete raw measurements**

  Include backend in run filenames, add `benchmark_run` to parsed report objects before `ConvertTo-Json -Compress`, and group dual-native medians by `backend, protocol, payload_bytes, wait_mode`.

- [x] **Step 3: Expand the Actor/Source comparison**

  Remove the Ubuntu-22-only gate. For every backend and wait mode, use `Get-CflowDriverOrder`, collect exactly one Actor and Source report for each run, and retain the existing error/rejection/stale-completion validation.

- [x] **Step 4: Replace unpaired aggregation**

  Use `Get-CflowPairedSourceSummary` and report median Actor/Source P50/P99 plus paired P50, P99, wall, CPU-time, CPU-efficiency, and combined-stage deltas. State explicitly that admission is API handoff and Source's deferred Actor submit belongs to completion/drive.

- [x] **Step 5: Validate the workflow locally**

  Run:

  ```text
  pwsh -NoProfile -File .github/tests/cflow-benchmark-stats-test.ps1
  actionlint .github/workflows/cflow-release-benchmarks.yml
  ```

  Expected: both commands exit 0.

### Task 3: Regression verification and delivery

**Files:**
- Verify: `.github/scripts/cflow-benchmark-stats.ps1`
- Verify: `.github/tests/cflow-benchmark-stats-test.ps1`
- Verify: `.github/workflows/cflow-release-benchmarks.yml`

**Interfaces:**
- Consumes: Task 1 helper contract and Task 2 workflow wiring.
- Produces: a reviewable commit and a new PR benchmark run.

- [x] **Step 1: Run static and repository checks**

  Run the PowerShell test, actionlint, `git diff --check`, and inspect the complete changed-file diff. Confirm no runtime or public C headers changed.

- [ ] **Step 2: Commit the methodology change**

  Commit message: `bench(cflow): make network comparisons paired`

- [ ] **Step 3: Push and verify CI creation**

  Push `feat/cflow-reactive-io-source`, confirm PR #123 points at the new commit, and capture the new `CFlow release host benchmarks` run URL.

- [ ] **Step 4: Review the first generated dataset**

  Confirm every job succeeds, every artifact has the expected backend groups and Actor/Source pairs, all correctness counters remain zero, and Ubuntu 24 backend records share one `runner_name`, image version, and CPU model.
