# CFlow IO Model and UDP API Baseline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Separate framework-model cost from real-network API behavior without changing public CFlow APIs.

**Architecture:** Two evidence paths remain intentionally distinct. An in-memory mock benchmark compares direct-control, Actor, the directly consumed IO Source adapter, and the complete Source runtime under the same completion workload and reports nanoseconds per value. A real UDP benchmark compares a blocking raw socket baseline, the asynchronous Actor API, and the reactive Source API under the same bounded send/receive workload. The mock path isolates cumulative framework control layers; the real path includes the operating-system backend mechanism and answers end-to-end API questions.

**Tech Stack:** C11, CFlow native IO Actor/Source, TurboUtils Platform sockets, TinyTest, PowerShell benchmark statistics, GitHub Actions, CMake presets.

**Spec:** `docs/superpowers/specs/2026-08-28-cflow-network-benchmark-baseline-design.md`, `docs/superpowers/specs/2026-08-29-cflow-coroutine-source-await-design.md`; GitHub issues #147 and #148.

## Global Constraints

- No public CFlow header, ABI, ownership, cancellation, completion, or error semantic changes.
- The data unit is one fixed-size UDP datagram; application bytes count each echo payload once and wire bytes count both directions.
- The benchmark thread is the single submit/drive/ack owner. Backend completion may wake from another thread, but delivery records are mutated only through the manual Executor path.
- Payload and address storage are borrowed from fixed arrays until authoritative completion is delivered and acknowledged.
- Real UDP workload capacity is `1..8`; mock Source control windows are `1/4/8/16/32/64`. Zero, overflow, unsupported protocol/peer/profile, full admission, invalid completion, and timeout fail explicitly.
- A successful Actor submission reaches exactly one acknowledged terminal completion; fixture cleanup drains any accepted request before destruction.
- No implicit fallback between Direct, Actor, Source, readiness, or completion mechanisms.
- Hosted-runner results are regression-shape evidence, not same-machine performance proof.
- Mock-model ratios and real-socket ratios are never combined: they measure different causal boundaries.

---

### Task 0: Same-work mock model control paths

**Files:**
- Modify: `cflow/benchmarks/cflow_io_source_benchmark.c`
- Modify: `.github/scripts/cflow-benchmark-stats.ps1`
- Modify: `.github/tests/cflow-benchmark-stats-test.ps1`
- Modify: `.github/workflows/cflow-release-benchmarks.yml`

**Interfaces:**
- Consumes: a synchronous mock completion, the IO Actor, the reactive IO Source, and TinyTest benchmark timing.
- Produces: exactly one `cflow-io-model-benchmark/v1` record for each of `direct-control`, `actor`, `io-source-adapter`, and `source-runtime` per run/window.

- [x] **Step 1: Add a direct-control loop and an Actor completion loop beside the existing Source benchmark**
- [x] **Step 2: Validate equal timed/processed values and lifecycle counters for all four models**
- [x] **Step 3: Add strict same-run conversion and paired cost-ratio aggregation tests**
- [x] **Step 4: Emit separate `io-model` CI artifacts and summary tables**

---

### Task 1: Common bounded UDP pipeline execution

**Files:**
- Modify: `cflow/benchmarks/cflow_network_benchmark.c`
- Test: `cflow/benchmarks/cflow_network_benchmark.c`

**Interfaces:**
- Consumes: existing `network_fixture`, native operation submission, manual Actor drive, Source batch execution, and raw UDP sockets.
- Produces: `network_exchange_pipeline_batch(...)`, additive workload-window JSON fields, and pipeline support for `direct`, `actor`, and `source`.

- [x] **Step 1: Write failing configuration and Actor batch tests**

Add literal assertions that UDP/raw/throughput pipeline accepts Direct and Actor, rejects invalid combinations, and that four Actor exchanges produce four matching payloads, eight acknowledged IO operations, zero active requests, and a peak workload window of four.

- [x] **Step 2: Run the focused benchmark test and verify RED**

Run the Release benchmark target and filter the new Actor pipeline case. Expected failure: pipeline validation rejects Actor or the new batch entry point is not implemented.

- [x] **Step 3: Implement the minimal fixed-array pipeline**

Use the existing maximum-eight storage. For Actor, submit the complete send batch, drive to all terminal completions, acknowledge each request, then repeat for receives. Store completion time and result index in the benchmark-owned operation record so completion order cannot corrupt latency attribution. Direct performs the same batch shape with raw `sendto`/`recvfrom`; Source keeps its existing batch path.

- [x] **Step 4: Run the focused tests and verify GREEN**

Run the Direct, Actor, and Source pipeline cases plus configuration validation. Expected: all pass with no rejections, stale completions, or retained active requests.

### Task 2: Machine-readable layer comparison

**Files:**
- Modify: `.github/scripts/cflow-benchmark-stats.ps1`
- Modify: `.github/tests/cflow-benchmark-stats-test.ps1`

**Interfaces:**
- Consumes: pipeline JSON records keyed by run, backend, payload, wait mode, and workload window.
- Produces: `Get-CflowPipelineLayerSummary` with paired Actor/Direct and Source/Actor throughput, CPU-efficiency, p99, and combined-stage metrics.

- [x] **Step 1: Write failing PowerShell behavior tests**

Create two literal paired runs for all three drivers and assert hand-derived median ratios. Add missing-driver, duplicate-driver, mismatched-attempt, and mismatched-window rejection cases.

- [x] **Step 2: Run the PowerShell test and verify RED**

Run `.github/tests/cflow-benchmark-stats-test.ps1`. Expected failure: `Get-CflowPipelineLayerSummary` is not defined.

- [x] **Step 3: Implement strict paired aggregation**

Filter only `workload=pipeline`, require exactly one record per driver and run, require equal positive attempts and exact window identity, validate finite positive metrics, then return medians and paired ratios without fallback.

- [x] **Step 4: Run the PowerShell test and verify GREEN**

Expected: all existing and new benchmark-stat tests pass.

### Task 3: Cross-platform CI collection and verification

**Files:**
- Modify: `.github/workflows/cflow-release-benchmarks.yml`

**Interfaces:**
- Consumes: the benchmark executable and `Get-CflowPipelineLayerSummary`.
- Produces: rotated Direct/Actor/Source runs for UDP pipeline windows `1/4/8`, JSONL artifacts, per-run Markdown, and paired layer medians.

- [x] **Step 1: Add an executable workflow-contract test that fails on Source-only data**

Extend the PowerShell test fixture to exercise the workflow aggregation input with all three drivers; the pre-change Source-only workflow cannot satisfy the layer summary.

- [x] **Step 2: Run the contract test and verify RED**

Expected failure: missing Direct/Actor pipeline pairs.

- [x] **Step 3: Extend collection with rotated driver order**

For every backend, payload, wait mode, window, and run, rotate `direct`, `actor`, and `source`; require matching protocol/profile/workload/window/attempt counts, zero errors/rejections/stale completions, and exact Source peak occupancy. Emit both existing Source window curves and the new same-window layer summary.

- [x] **Step 4: Verify scripts, build, correctness, and benchmark smoke data**

Run the PowerShell test, parse the workflow PowerShell block, build `cflow_network_benchmark` with `win-release-user`, run the focused and full benchmark executable, then run one small window `1/4/8` Direct/Actor/Source smoke matrix and validate every JSON record.

- [x] **Step 5: Review compatibility and diff hygiene**

Confirm only benchmark/CI/documentation files changed, `git diff --check` is clean, no public header changed, no `.codegraph` artifact is staged, and the measured conclusions are reported as facts only after fresh runs.

### Task 4: Mock Source attribution boundary

**Files:**
- Modify: `cflow/benchmarks/cflow_io_source_benchmark.c`
- Modify: `.github/scripts/cflow-benchmark-stats.ps1`
- Modify: `.github/tests/cflow-benchmark-stats-test.ps1`
- Modify: `.github/workflows/cflow-release-benchmarks.yml`

**Interfaces:**
- Consumes: public `cflow_source_resume`, the IO Source owner driver, the existing identity Graph/Runtime benchmark, and the existing synchronous mock completion.
- Produces: four cumulative model records named `direct-control`, `actor`, `io-source-adapter`, and `source-runtime`. The adapter record excludes Graph, Runtime, Scheduler, and Sink; the runtime record includes all four because the public Runtime contract requires a Sink.

- [x] **Step 1: Make the statistics contract require four cumulative layers**

Extend the literal PowerShell fixture with `io-source-adapter`, require exactly one record per model and run, and assert hand-derived paired median absolute deltas for Actor/direct-control, adapter/Actor, and runtime/adapter. Verify RED because the converter still accepts only three model names.

- [x] **Step 2: Add the direct IO Source adapter benchmark**

Construct the existing windowed IO Source without opening a Run, drive it through `cflow_source_resume` with the remaining downstream-demand snapshot, run its owner only on WAIT or final acknowledge drain, and verify every prepared operation becomes one encoded, observed, released, and acknowledged value. Do not arm a waitable because the synchronous mock driver is polled explicitly, matching the existing direct Source tests.

- [x] **Step 3: Emit and aggregate the four-layer contract**

Emit `cflow-io-model-benchmark/v1` records for all four cumulative models, rename the full model to `source-runtime`, validate adapter/runtime lifecycle and drive counters, and report absolute paired ns/value deltas as primary metrics. Keep ratios as contextual fields only.

- [x] **Step 4: Update separate mock CI artifacts**

Require four model groups per window, add adapter and runtime median columns plus Actor/direct, adapter/Actor, and runtime/adapter paired deltas, and state explicitly that runtime/adapter includes Graph, Runtime, Scheduler, and Sink delivery.

- [x] **Step 5: Verify the attribution boundary**

Run the PowerShell RED/GREEN test, build `cflow_io_source_benchmark` with `win-release-user`, execute workflow-sized windows including 1 and 64, parse all four records, run the adjacent network benchmark regression, parse the workflow PowerShell block, and finish with `git diff --check`.

### Task 5: Optional coroutine Source await adapter

**Files:**
- Modify: `cflow/minicoro/include/cflow/minicoro.h`
- Modify: `cflow/minicoro/src/minicoro.c`
- Modify: `cflow/minicoro/tests/cflow_minicoro_test.c`
- Modify: `cflow/benchmarks/CMakeLists.txt`
- Modify: `cflow/benchmarks/cflow_io_source_benchmark.c`
- Modify: `.github/scripts/cflow-benchmark-stats.ps1`
- Modify: `.github/tests/cflow-benchmark-stats-test.ps1`
- Modify: `.github/workflows/cflow-release-benchmarks.yml`

**Interfaces:**
- Consumes: a borrowed `cflow_source`, the active `cflow_resume_ctx`, and the Source-provided waitable.
- Produces: `cflow_minicoro_await_source(cflow_minicoro *, cflow_source *, void *)`, which returns the first non-`WAIT` `cflow_step`, plus a `coroutine-source-adapter` benchmark record with `added_worker_threads=0`.

- [x] **Step 1: Write Source await behavior tests**

Add a real test Source whose scripted steps cover immediate `VALUE`, `WAIT` then `VALUE_AND_DONE`, `ERROR`, invalid `WAIT`, and cancellation. Assert that the exact resume context crosses the suspension, the wake callback only notifies the owner, cancelling the outer Resumable calls Source cancel exactly once, code after the await is not entered, and managed Source output types fail before Source resume.

- [x] **Step 2: Run the focused test and verify RED**

Run `cmake --build --preset win-release-user --target cflow_minicoro_test` and `ctest --preset win-release-user -R "^cflow_minicoro_test$" --output-on-failure` after configuring `win-release-user` with `CFLOW_ENABLE_MINICORO=ON`. Expected failure: `cflow_minicoro_await_source` is undeclared or undefined.

- [x] **Step 3: Implement the minimal borrowed Source adapter**

Add one active borrowed-Source field to the opaque minicoro state. Resume the Source using the current context, suspend only for a valid `WAIT`, and return the first non-`WAIT` step. Admit only valid trivial-copy/trivial-destroy Source output descriptors. During outer cancellation, call the active Source's cancel operation instead of cancelling its waitable a second time. Do not move or destroy the Source.

- [x] **Step 4: Run the focused and adjacent lifecycle tests**

Rebuild and run `cflow_minicoro_test`, then run `cflow_io_source_test` and `cflow_runtime_test`. Expected: all tests pass with no duplicate cancellation, retained request, or changed Source behavior.

- [x] **Step 5: Add a same-work coroutine adapter benchmark**

Wrap the existing directly consumed mock IO Source in one long-lived minicoro Resumable. For every value, await the Source and yield the same integer to the benchmark owner. Emit `coroutine-source-adapter` beside the four existing records and an explicit `added_worker_threads=0`; preserve direct Source and full Runtime as separate alternative consumers rather than treating coroutine and Runtime as cumulative layers.

- [x] **Step 6: Extend strict benchmark aggregation tests**

Add literal five-model fixtures. Require one coroutine record per run/window, validate its zero added-worker count, and report `coroutine-source-adapter - io-source-adapter` as an absolute nanoseconds-per-value delta. Keep `source-runtime - io-source-adapter` unchanged because Runtime and coroutine are alternative consumers.

- [x] **Step 7: Verify benchmark semantics and compatibility**

Run the PowerShell test, build and run `cflow_io_source_benchmark` at windows 1 and 64, validate equal processed values/lifecycle counters across all models, run `git diff --check`, and confirm core CFlow builds with `CFLOW_ENABLE_MINICORO=OFF`.
