# CFlow Reactive Source Payload Gradient Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Determine whether the measured Actor-to-Reactive-Source overhead scales with payload bytes, while preserving the CFlow runtime and the `cflow-network-benchmark/v1` producer.

**Architecture:** Extend the workflow-only paired statistics helper to key comparisons by payload and report median per-run absolute nanosecond deltas alongside percentages. Run counterbalanced Actor/Source TCP diagnostics at 64 B, 1 KiB, and 64 KiB with a bounded, identical sample budget, retain every raw record, and aggregate each payload independently.

**Tech Stack:** PowerShell 7, GitHub Actions YAML, CFlow network benchmark JSONL, actionlint.

**Spec:** `docs/superpowers/plans/2026-08-27-cflow-network-source-benchmark.md`

## Global Constraints

- Do not change CFlow runtime code, public headers, or the benchmark JSON producer schema.
- Use five paired process runs per backend/payload/wait configuration and retain every JSON record.
- Use `100 samples x 64 exchanges = 6,400` observations for every payload; this bounds the 64 KiB configuration to 400 MiB application data per process while keeping sample cardinality identical.
- Rotate payload order by run and counterbalance Actor/Source order by run, backend, wait mode, and payload.
- Treat absolute per-exchange and per-stage nanosecond deltas as the primary payload-scaling evidence; percentages remain contextual.

---

### Task 1: Payload-keyed paired statistics

**Files:**
- Modify: `.github/tests/cflow-benchmark-stats-test.ps1`
- Modify: `.github/scripts/cflow-benchmark-stats.ps1`

**Interfaces:**
- Extend: `Get-CflowDriverOrder -Run <int> -WaitMode <blocking|busy> -BackendIndex <int> -PayloadIndex <int> -> string[]`.
- Extend: `Get-CflowPairedSourceSummary -Reports <object[]> -Backend <string> -WaitMode <string> -PayloadBytes <int64> -ExpectedRuns <int> -> PSCustomObject`.
- Add result fields: `payload_bytes`, `paired_p50_delta_ns`, `paired_p99_delta_ns`, `paired_wall_delta_ns_per_exchange`, `paired_cpu_time_delta_ns_per_exchange`, and `paired_combined_stage_delta_ns`.

- [x] **Step 1: Write the failing tests**

  Add payload fields to the literal three-run fixture, add a second payload pair, require payload filtering, assert payload-sensitive driver ordering, and assert hand-calculated absolute deltas.

- [x] **Step 2: Verify RED**

  Run: `pwsh -NoProfile -File .github/tests/cflow-benchmark-stats-test.ps1`

  Expected: parameter binding fails because `PayloadBytes` and `PayloadIndex` are not implemented.

- [x] **Step 3: Implement the minimal helper changes**

  Filter exact pairs by backend, payload, wait mode, run, and driver. Require equal positive attempted counts inside each pair. Calculate absolute deltas per run before taking medians.

- [x] **Step 4: Verify GREEN and mutation coverage**

  Run the test, then confirm missing pairs, duplicate pairs, and mixed payload records are rejected or isolated as specified.

### Task 2: Three-payload workflow matrix

**Files:**
- Modify: `.github/workflows/cflow-release-benchmarks.yml`

**Interfaces:**
- Produces: payload-keyed raw files, JSONL records, per-run summary rows, and paired median rows for 64, 1024, and 65536 bytes.

- [x] **Step 1: Configure the bounded diagnostic workload**

  Define the three payloads plus fixed samples/exchanges, record them in metadata, set the environment only around the Source diagnostic matrix, and validate the report echoes all three values.

- [x] **Step 2: Balance and retain measurements**

  Rotate payload order by run, include canonical payload index in driver counterbalancing, include payload in filenames, and retain every parsed record with `benchmark_run`.

- [x] **Step 3: Aggregate by payload**

  Group on `backend, payload_bytes, driver, wait_mode`; require the exact group count and run count; emit absolute nanosecond and percentage paired deltas per payload.

- [x] **Step 4: Update job summary methodology text**

  State the payloads, fixed sample budget, pairing rule, absolute-delta interpretation, and the fact that the benchmark payload remains caller-owned.

### Task 3: Verification and dataset review

**Files:**
- Verify: `.github/scripts/cflow-benchmark-stats.ps1`
- Verify: `.github/tests/cflow-benchmark-stats-test.ps1`
- Verify: `.github/workflows/cflow-release-benchmarks.yml`

- [x] **Step 1: Run local verification**

  Run the PowerShell tests, parse the workflow YAML through PowerShell, run actionlint, run `git diff --check`, and inspect the complete diff. Confirm no C runtime or public header changed.

- [ ] **Step 2: Commit and push the benchmark change**

  Commit with `bench(cflow): measure Source payload scaling`, push `feat/cflow-reactive-io-source`, and capture the new PR #123 benchmark run URL.

- [ ] **Step 3: Review the generated dataset**

  Require all jobs and artifacts, exact payload/backend/wait/driver/run cardinality, zero errors/rejections/stale completions, and compare absolute Actor-to-Source deltas across payloads before concluding whether payload copying is implicated.
