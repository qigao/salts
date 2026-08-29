# CFlow Native I/O Round-Trip Batching Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the serialized send-completion barrier from Actor and reactive Source loopback round trips without changing public APIs or ownership semantics.

**Architecture:** Reuse the existing bounded multi-operation runners to admit receive before send as a two-operation batch.  Preserve one native completion and acknowledge per operation, map out-of-order results by index, and retain the previous partial-transfer and error behavior.

The endpoint/request and single-owner backend-loop boundary follows the libuv
design model as a reference, while preserving CFlow's existing public API,
demand, acknowledgement, and Graph ownership semantics.

**Tech Stack:** C11, CFlow IO Actor/Source/native adapters, TurboUtils threading, TinyTest benchmarks, CMake presets.

**Spec:** `docs/architecture/cflow-native-io-round-trip-batching.md`

## Global Constraints

- Do not change installed headers, ABI, public error codes, or ownership rules.
- Do not allocate or copy payload bytes on the hot path.
- Reuse the existing bounded request slots and batch runners.
- A successful operation admission must still reach exactly one acknowledge/release terminal path.
- Invalid capacity or partial progress must fail explicitly; no fallback or silent retry is added.

---

### Task 1: Specify concurrent round-trip admission

**Files:**
- Modify: `cflow/benchmarks/cflow_network_benchmark.c`
- Test: `cflow/benchmarks/cflow_network_benchmark.c`

**Interfaces:**
- Consumes: `network_exchange()`, the private endpoint batch observation, and `cflow_io_source_owner_get_window_stats()`.
- Produces: observable peak in-flight count of two for one Actor/Source raw-peer round trip.

- [x] **Step 1: Change the Source round-trip assertion**

Change the existing requested-window test to require:

```c
check_equal(window_stats.peak_occupied, (size_t)2u);
```

- [x] **Step 2: Add an Actor round-trip behavior test**

Create a raw-peer TCP Actor fixture with request capacity two, execute one
round trip, and assert:

```c
check_equal(fixture.client.batch_peak_operations, (size_t)2u);
check_equal(actor_stats.acknowledged, (uint64_t)2u);
check_equal(actor_stats.active_requests, (size_t)0u);
```

- [x] **Step 3: Run the focused tests and verify RED**

Run the Release benchmark executable with filters for the requested Source
window and Actor paired round trip.  Expected: both tests fail because the
current path admits one operation at a time and does not record a paired peak.

### Task 2: Admit receive and send as one bounded batch

**Files:**
- Modify: `cflow/benchmarks/cflow_network_benchmark.c`
- Test: `cflow/benchmarks/cflow_network_benchmark.c`

**Interfaces:**
- Consumes: `network_run_actor_operations()` and `network_run_source_operations()`.
- Produces: `network_run_async_operations()` and raw TCP/UDP paired-transfer helpers used only by Actor and Source benchmark drivers.

- [x] **Step 1: Add a private driver dispatcher**

Add a static helper that accepts an operation array and result array, selects
the Actor or Source batch runner, rejects every other driver with
`TURBO_EINVAL`, and records the largest successful admitted operation count in
`workload_peak_in_flight`.

- [x] **Step 2: Add the TCP paired-transfer helper**

Maintain independent sent and received offsets.  Build receive first and send
second whenever both remain, run one bounded batch, validate each completion,
and advance offsets.  Submit one operation only when the other direction has
already completed.

- [x] **Step 3: Add the UDP paired-transfer helper**

Build receive first and send second, run one bounded batch, validate exact
datagram lengths and source address, and preserve payload equality validation.

- [x] **Step 4: Route raw Actor/Source round trips through the helpers**

Keep Direct, Vector, and dual-native-peer paths unchanged.  Use paired helpers
only when the peer is raw and the driver is Actor or Source.

- [x] **Step 5: Build and verify GREEN**

Build `cflow_network_benchmark`, rerun the two focused tests, then run all
non-benchmark correctness cases in the executable.  Expected: zero failures.

### Task 3: Validate behavior and performance boundaries

**Files:**
- Verify: `cflow/benchmarks/cflow_network_benchmark.c`
- Verify: `cflow/tests/cflow_io_actor_test.c`
- Verify: `cflow/tests/cflow_io_native_test.c`

**Interfaces:**
- Consumes: unchanged public CFlow Actor, Source, Executor, and native backend contracts.
- Produces: fresh correctness and latency evidence for the branch.

- [x] **Step 1: Run adjacent CFlow tests**

Build and run `cflow_io_actor_test`, `cflow_io_native_test`, and the complete
non-benchmark portion of `cflow_network_benchmark` through the documented
Windows Release preset environment.

- [x] **Step 2: Run 1 KiB Windows latency samples**

Run TCP and UDP Actor/Source/Direct round-trip profiles with blocking wait,
raw peer, fixed samples/exchanges, and stage timing.  Preserve the raw output
for before/after comparison.

- [x] **Step 3: Review the diff and protocol invariants**

Confirm no public header changed, no payload allocation/copy was added, every
successful admission is acknowledged, Direct/Vector behavior is unchanged,
and the only documentation changes are this specification and plan.
