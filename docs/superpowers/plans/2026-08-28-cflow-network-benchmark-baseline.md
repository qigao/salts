# CFlow Network Benchmark Baseline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make CFlow network benchmark results unambiguous and quantify serialized Actor overhead against a direct socket baseline.

**Architecture:** Extend the existing benchmark driver boundary with a raw/blocking-only direct mode that reuses the loopback fixture without initializing CFlow runtime objects. Preserve the JSON v1 contract, then update the release workflow to expose Echo/s and paired direct-versus-Actor medians.

**Tech Stack:** C11, TinyTest benchmark macros, Salts Platform sockets/threads, CMake presets, GitHub Actions PowerShell.

**Spec:** `docs/superpowers/specs/2026-08-28-cflow-network-benchmark-baseline-design.md`

## Global Constraints

- Preserve all existing Actor, Source, and dual-native behavior.
- Direct mode is bounded to the existing raw echo fixture and serialized exchange count.
- Direct mode supports only raw peer, blocking wait mode, and disabled stage timing.
- Do not change the `cflow-network-benchmark/v1` JSON field set.
- Do not add dependencies or production CFlow APIs.

---

### Task 1: Direct socket benchmark driver

**Files:**
- Modify: `cflow/benchmarks/cflow_network_benchmark.c`
- Test: embedded TinyTest specifications in `cflow/benchmarks/cflow_network_benchmark.c`

**Interfaces:**
- Consumes: existing `network_fixture`, raw echo server, TCP/UDP socket helpers.
- Produces: `NETWORK_DRIVER_DIRECT`, `CFLOW_NETWORK_DRIVER=direct`, direct TCP/UDP exchange behavior, `backend=socket` JSON records.

- [x] **Step 1: Write failing parser and compatibility tests**

Add expectations that `direct` parses, maps back to the `direct` name, accepts raw/blocking/stage-off configuration, and rejects native peer, busy wait, or stage timing.

- [x] **Step 2: Run the benchmark target and verify RED**

Run `cmake --build --preset win-release-user --target cflow_network_benchmark` and confirm compilation fails because `NETWORK_DRIVER_DIRECT` is not defined.

- [x] **Step 3: Implement minimal direct configuration support**

Add the enum/name/parser validation, skip runtime endpoint and wake-latch initialization, and retain a blocking client socket for direct mode.

- [x] **Step 4: Write failing real loopback tests**

Add one TCP and one UDP fixture test that select direct mode, exchange a literal payload through the raw peer, verify exact echoed bytes, and cleanly destroy the fixture.

- [x] **Step 5: Run the benchmark target and verify RED**

Run the target and confirm the new tests fail because direct exchange is not implemented.

- [x] **Step 6: Implement direct TCP and UDP exchange paths**

Use existing exact TCP helpers and bounded UDP datagram calls; validate UDP byte count and peer address before accepting the echo.

- [x] **Step 7: Run focused tests and direct benchmark smoke cases**

Build the target, then run TCP and UDP with `CFLOW_NETWORK_DRIVER=direct`, raw peer, blocking wait, small sample counts, and verify one successful JSON record per invocation.

### Task 2: Release evidence and metric clarity

**Files:**
- Modify: `.github/workflows/cflow-release-benchmarks.yml`
- Modify: `.github/scripts/cflow-benchmark-stats.ps1`
- Test: `.github/tests/cflow-benchmark-stats-test.ps1`

**Interfaces:**
- Consumes: JSON v1 records with `driver`, `backend`, Echo/s, application MiB/s, CPU, latency, and error counters.
- Produces: general summary with Echo/s plus paired direct-versus-Actor raw results at 64 B, 1 KiB, and 64 KiB.

- [x] **Step 1: Write failing paired-statistics helper tests**

Add literal three-run Actor/direct records whose median paired ratio differs from the ratio of unpaired medians. Assert driver counterbalancing, paired Echo/s and application-throughput ratios, P99 deltas, missing pairs, and mismatched attempt counts.

- [x] **Step 2: Implement paired-statistics helpers**

Add `Get-CflowBaselineDriverOrder` and `Get-CflowPairedDirectSummary`; require exactly one positive, equal-attempt Actor/direct pair for every expected run.

- [x] **Step 3: Add workflow validation that fails without direct records**

Create a bounded diagnostic loop that expects one valid direct and one valid Actor JSON record per backend, payload, and run, and rejects mismatched driver/backend/error fields.

- [x] **Step 4: Add paired median aggregation**

Calculate direct and Actor median Echo/s and application MiB/s plus Actor/direct ratios from same-run pairs; label MiB/s as application payload throughput.

- [x] **Step 5: Clarify the general network summary**

Add Echo/s beside application MiB/s and state that latency-profile MiB/s is a payload-rate derivative, not a streaming bandwidth result.

- [x] **Step 6: Validate workflow syntax and smoke the same configurations locally**

Inspect the YAML diff, run the direct and Actor configurations used by the diagnostic, and confirm the generated JSON satisfies every workflow predicate.

### Task 3: Regression verification

**Files:**
- Verify: `cflow/benchmarks/cflow_network_benchmark.c`
- Verify: `.github/workflows/cflow-release-benchmarks.yml`

**Interfaces:**
- Consumes: completed benchmark and workflow changes.
- Produces: reproducible build/test evidence and a clean scope review.

- [x] **Step 1: Run the focused benchmark executable in default Actor mode**

Confirm embedded tests and the benchmark complete with no errors or rejections.

- [x] **Step 2: Run adjacent CFlow I/O tests**

Run the preset test regex covering `cflow_io_actor_test`, `cflow_io_native_test`, and `cflow_io_source_test`.

- [x] **Step 3: Review diff and repository state**

Confirm only the benchmark, workflow, design, and plan files changed; check that no build output or `.codegraph` artifact is staged.
