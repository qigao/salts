# CFlow Network Benchmark Wait Modes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Compare the existing busy-drive benchmark loop with an event-driven blocking mode and report normalized CPU efficiency for TCP/UDP native backends.

**Architecture:** Keep Actor and Executor public APIs unchanged. The benchmark's blocking mode wires the existing Actor advisory wake callback to a condition-variable edge latch, drains Actor/Executor work on the benchmark thread, and sleeps only after rechecking the latch under its mutex. Busy mode retains the current yield loop as a baseline.

**Tech Stack:** ISO C11, CFlow IO Actor/native backend, TurboUtils mutex/condition/clock primitives, TinyTest, PowerShell GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-08-25-cflow-network-performance-ci-design.md`

## Protocol

- Data unit: one copied `cflow_io_completion` in the existing fixed two-slot probe.
- Source of truth: Actor request state; the probe is a bounded observation released by acknowledge.
- Topology: native backend producers wake one benchmark-thread Actor/Executor consumer.
- Backpressure: existing Actor/native bounded rejection statuses remain authoritative and visible.
- Wake: blocking mode uses a mutex-protected edge latch, so a wake before sleep cannot be lost.
- Shutdown: stop submission, drain/close/destroy Actor while the wake context is alive, then destroy condition and mutex.

### Task 1: Freeze wait-mode and metric behavior

**Files:**

- Modify: `cflow/benchmarks/cflow_network_benchmark.c`

- [x] Add focused TinyTest assertions for `busy|blocking` parsing, invalid input, and CPU efficiency arithmetic.
- [x] Build the benchmark and confirm RED because the new parser/metric helpers are absent.

### Task 2: Implement event-driven blocking wait

**Files:**

- Modify: `cflow/benchmarks/cflow_network_benchmark.c`

- [x] Add the bounded wake latch and initialize it only for blocking mode.
- [x] Wire Actor wake to the latch and split busy-drive from blocking drain/wait behavior.
- [x] Preserve timeout, completion copy, acknowledge, teardown, and rejection semantics.
- [x] Build and run reduced TCP/UDP scenarios in both modes.

### Task 3: Report and collect CPU efficiency

**Files:**

- Modify: `cflow/benchmarks/cflow_network_benchmark.c`
- Modify: `.github/workflows/cflow-release-benchmarks.yml`
- Modify: `docs/superpowers/specs/2026-08-25-cflow-network-performance-ci-design.md`

- [x] Add additive JSON fields `wait_mode`, `application_mib_per_second`, `cpu_core_equivalents`, and `application_mib_per_cpu_second`.
- [x] Run both wait modes in CI and include mode plus normalized efficiency in raw filenames and Markdown.
- [x] Validate mode and finite nonnegative metrics while retaining schema v1 and informational-only policy.

### Task 4: Verify and publish

- [x] Run focused benchmark tests and all eight reduced protocol/profile/mode scenarios.
- [x] Run adjacent CFlow IO Actor/native tests, workflow/parser review, `git diff --check`, and CodeGraph affected analysis.
- [ ] Commit, push the existing PR branch, and use GitHub checks to collect the cross-platform comparison.
