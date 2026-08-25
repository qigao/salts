# CFlow TCP/UDP Performance CI Implementation Plan

> **Execution:** After native backend correctness is GREEN, because the benchmark
> consumes that public API.

**Goal:** Produce reproducible TCP/UDP native-backend performance evidence in CI.

**Architecture:** A loopback echo fixture drives Actor native send/recv operations;
TinyTest reports human metrics, a stable JSON line carries quantiles/resources/outcomes,
and the existing release benchmark workflow aggregates and uploads per-host artifacts.

**Tech Stack:** C11 sockets, CFlow IO Actor/native backend, TurboUtils clock/thread,
TinyTest `benchmark_io`, PowerShell in GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-08-25-cflow-network-performance-ci-design.md`

### Task 1: Add RED loopback/report contract tests

Create a small TCP/UDP native loopback test that checks payload equality, exact Actor
completion/ack/release, zero errors/rejections and clean shutdown. Confirm RED before
sharing its fixture with the benchmark.

### Task 2: Implement the benchmark

Create `cflow/benchmarks/cflow_network_benchmark.c` and its CMake target. Add checked
configuration, fixed latency storage, CPU/RSS collection, percentile calculation,
TinyTest `benchmark_io` and one validated JSON result per process.

### Task 3: Extend CI evidence collection

Modify `.github/workflows/cflow-release-benchmarks.yml` to build/test the target, run
TCP/UDP latency/throughput scenarios repeatedly, validate JSON, write JSONL/Markdown,
append an informational summary and upload the existing artifact directory.

### Task 4: Verify

Run focused correctness tests, all four local benchmark scenarios with reduced counts,
JSON parsing, Release benchmark target build, workflow syntax review, `git diff --check`,
CodeGraph affected analysis and adjacent CFlow/Platform regressions.
