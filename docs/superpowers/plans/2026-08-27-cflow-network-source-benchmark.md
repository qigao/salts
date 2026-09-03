# CFlow Network Source Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a reproducible network benchmark comparison that isolates direct IO Actor overhead from the Reactive IO Source/Run path and reports optional per-operation admission versus completion-drive timing.

**Architecture:** Extend the existing loopback network fixture with an explicit `actor`/`source` client driver. Keep `actor` as the default and preserve its current result shape and timing behavior. The Source driver reuses the same raw echo peer, native backend, sockets, payloads, wait modes, and operation allocation policy, while adding only Graph/Run/Source/Scheduler ownership. Reject Source with the dual-native peer because that combination is outside this diagnostic's single-variable comparison. Stage timing is opt-in so clock reads do not perturb historical Actor measurements.

**Tech Stack:** C11, Salts CFlow IO Actor/Reactive Source/Run, native IO backends, TinyTest benchmarks, CMake Presets, GitHub Actions PowerShell reporting.

**Spec:** `CFLOW_NETWORK_DRIVER` accepts `actor` (default) or `source`. `source` requires `CFLOW_NETWORK_PEER=raw`. `CFLOW_NETWORK_STAGE_TIMING` accepts `0` (default) or `1`. JSON keeps schema `cflow-network-benchmark/v1`, adds `driver`, `stage_timing`, `io_operations`, `admission_ns`, `completion_drive_ns`, `admission_mean_ns`, and `completion_drive_mean_ns`. Timing sums are zero when disabled. Source owns one continuous Run for the benchmark fixture; every operation is prepared only after downstream demand, encoded to a typed completion value, delivered to the sink, acknowledged by the adapter, and fully drained before teardown.

## Task 1: Lock down configuration and timing arithmetic

**Files:**
- Modify: `cflow/benchmarks/cflow_network_benchmark.c`

- [x] Add TinyTest cases for default/valid/invalid driver parsing, stage-timing parsing, and rejection of `source + native`.
- [x] Build `cflow_network_benchmark` and confirm the new tests fail for the missing behavior.
- [x] Add the smallest enums, parsers, names, compatibility validation, and overflow-safe timing accumulation needed to pass.
- [x] Run only the benchmark configuration spec and confirm it passes.

## Task 2: Add the Reactive Source client driver

**Files:**
- Modify: `cflow/benchmarks/cflow_network_benchmark.c`

- [x] Add a failing one-exchange TCP/raw Source integration test using the same fixture and payload verification as Actor mode.
- [x] Build and run the focused spec to confirm the expected failure.
- [x] Add Source driver state with explicit ownership of surface/normalized graphs, scheduler, Source, owner, Run, sink callbacks, and the pending native operation.
- [x] Implement prepare/encode/drive/sink callbacks, one-operation demand, owner/scheduler pumping, completion error propagation, and exact-once operation release.
- [x] Route raw-peer client operations through the selected driver without changing the dual-native Actor path.
- [x] Close Run, drain owner, close owner, destroy scheduler/graphs, then destroy the native backend; preserve the first cleanup error.
- [x] Run focused TCP and UDP Source correctness cases in blocking and busy modes.

## Task 3: Emit comparable diagnostics and exercise them in CI

**Files:**
- Modify: `cflow/benchmarks/cflow_network_benchmark.c`
- Modify: `.github/workflows/cflow-release-benchmarks.yml`

- [x] Add failing configuration/output assertions for driver identity and disabled/enabled stage totals.
- [x] Measure API admission and completion/drive only when the diagnostic flag is enabled, using checked accumulation and an operation count.
- [x] Add the new fields to benchmark title/JSON while keeping existing field names and meanings stable.
- [x] Keep the existing Actor matrix unchanged and add a five-run Actor/Source raw TCP latency diagnostic with matching stage timing on the Ubuntu 22.04 epoll host.
- [x] Extend the workflow summary with Actor versus Source P50/P99, CPU, and stage means; retain raw JSONL artifacts.
- [x] Validate workflow YAML structure and PowerShell field access locally where tooling permits.

## Task 4: Verify behavior, regressions, and benchmark evidence

**Files:**
- Verify: `cflow/benchmarks/cflow_network_benchmark.c`
- Verify: `.github/workflows/cflow-release-benchmarks.yml`

- [x] Build the Release benchmark target through `win-release-user` in the Visual Studio developer environment.
- [x] Run the configuration spec and the one-exchange Source integration tests.
- [x] Run reduced Actor and Source TCP/raw latency benchmarks with identical samples, exchanges, payload, backend, and wait mode; parse both JSON lines and calculate the delta.
- [x] Run reduced Source UDP/raw and busy-wait smoke benchmarks.
- [x] Run adjacent CFlow IO Source and native IO CTest targets.
- [x] Inspect `git diff --check`, changed-file diff, and worktree status; document any unrun cross-platform checks and residual risk.
