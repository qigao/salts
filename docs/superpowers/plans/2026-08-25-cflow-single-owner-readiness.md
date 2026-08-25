# CFlow Single-Owner Readiness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the epoll/kqueue native adapter's per-operation fd lifecycle and intermediate worker handoff with persistent per-socket readiness ownership while preserving the public submit/completion API.

**Architecture:** The platform readiness reactor remains the sole blocking poller. A new additive continuation arm lets a callback request the next one-shot interest, which Platform commits only after the callback returns; the existing `turbo_readiness_arm()` callback-reentry behavior remains `TURBO_EBUSY`. The CFlow readiness adapter retains at most two lanes per original socket identity: one duplicated descriptor and registration for FIFO reads, and one for FIFO writes. This avoids mutable combined-interest races while making descriptor lifecycle scale with live sockets instead of operations. Initial nonblocking attempts run on submit and retries run directly from the reactor callback. Fixed-capacity request and socket tables preserve bounded memory and explicit backpressure.

**Tech Stack:** C11, TurboUtils Platform readiness reactor, CFlow I/O Actor, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-25-cflow-native-io-backends-design.md`

## Global Constraints

- Keep `cflow_io_native_backend_*` signatures and operation/completion ownership unchanged.
- A successful Actor submit still moves the operation; exactly one completion and one release/ack terminal path remain required.
- Request and socket identity tables are fixed at `request_capacity`; full tables reject explicitly.
- `forget_socket` is the lifecycle boundary after close and after all operations for that socket complete.
- epoll/kqueue own readiness waiting; no additional readiness worker thread remains. A bidirectional retained socket consumes two Platform registration slots and duplicate descriptors.
- IOCP and io_uring implementations keep their existing completion behavior.
- Shutdown stops admission, requires zero active requests, closes retained registrations, joins the reactor, then releases storage.

---

### Task 0: Add callback-return continuation rearm

**Files:**
- Modify: `platform/include/turbo/readiness.h`
- Modify: `platform/src/readiness.c`
- Modify: `platform/tests/readiness_contract_suite.c`

**Protocol:**
- `turbo_readiness_arm()` keeps its public one-shot and callback `TURBO_EBUSY` semantics.
- `turbo_readiness_arm_continuation()` accepts a callback that returns `COMPLETE` or `REARM` with validated next interests.
- Platform invokes callbacks without its mutex, then serializes the returned decision with shutdown, close, unarm, external arm, generation, and terminal state before calling the backend arm hook.
- A terminal callback cannot rearm. A malformed result completes without rearm and is reported as `TURBO_EINVAL` by dispatch. A backend rearm failure produces exactly one terminal callback with `events == 0` and the backend error.

- [x] Write and verify a RED fake-backend contract test for one continuation rearm.
- [x] Implement the additive public types/API and common arm admission path.
- [x] Add invalid-result, backend-rearm-failure, shutdown-race, and old-API compatibility tests.
- [x] Run the fake readiness contract and state-model tests GREEN.

---

### Task 1: Lock the persistent socket lifecycle with tests

**Files:**
- Modify: `cflow/tests/cflow_io_native_test.c`
- Modify: `cflow/include/cflow/io_native.h`

**Interfaces:**
- Consumes: `cflow_io_native_backend_forget_socket(cflow_io_native_backend *, uintptr_t)`.
- Produces: observable readiness behavior where a known quiescent socket is forgotten once and a repeated/unknown identity returns `TURBO_ENOENT`.

- [x] **Step 1: Write the failing readiness test**

Add a platform-guarded helper that completes and acknowledges a TCP operation, closes the original socket, expects the first `forget_socket` to return `TURBO_OK`, and expects the second call for the same identity to return `TURBO_ENOENT`.

```c
check_equal(cflow_io_native_backend_forget_socket(
                &fixture.backend, (uintptr_t)closed_socket),
            TURBO_OK);
check_equal(cflow_io_native_backend_forget_socket(
                &fixture.backend, (uintptr_t)closed_socket),
            TURBO_ENOENT);
```

- [x] **Step 2: Verify RED**

Run the repository's configured release target and focused test. Expected failure: readiness currently returns `TURBO_OK` for the second unknown identity.

```powershell
cmake --build --preset win-release-user --target cflow_io_native_test
ctest --preset win-release-user -R cflow_io_native_test --output-on-failure
```

- [x] **Step 3: Document the lifecycle contract**

Update the header comment so all native backends retain bounded identity where their OS model requires it, and require `forget_socket` after close/quiescence. Document `TURBO_EBUSY` and `TURBO_ENOENT`.

- [x] **Step 4: Commit with Task 2 after GREEN**

The test intentionally remains failing until the readiness implementation exists; do not commit a red branch.

---

### Task 2: Make readiness registrations persistent and remove the worker

**Files:**
- Modify: `cflow/src/io_native_readiness.c`
- Test: `cflow/tests/cflow_io_native_test.c`

**Interfaces:**
- Consumes: `turbo_readiness_register`, `turbo_readiness_arm_continuation`, `turbo_readiness_close`, Actor completion callback.
- Produces: the existing `cflow_io_native_impl_ops` table without a separate readiness worker thread.

- [x] **Step 1: Replace per-request descriptors with bounded socket records**

Use fixed arrays allocated at init. Each socket owns independent read/write lanes so each lane has one fixed-interest FIFO and no armed-interest mutation:

```c
typedef struct cflow_readiness_socket_record {
    uintptr_t socket_identity;
    cflow_readiness_lane lanes[2];
    size_t active_requests;
    bool active;
} cflow_readiness_socket_record;
```

Each request record borrows its socket record until completion. First use duplicates and registers once; later operations reuse it. Allocation failure, table full, invalid descriptors, and registration errors return the existing explicit Turbo error codes.

- [x] **Step 2: Execute initial attempts synchronously**

`readiness_submit` reserves both records, performs a nonblocking send/recv outside the gate, completes immediately on success/error, or arms the persistent registration on `EAGAIN`.

- [x] **Step 3: Complete readiness directly from the reactor callback**

The callback claims pending requests for one lane under the gate, retries normal I/O outside the gate up to `completion_batch_capacity`, publishes completions, and rearms that lane only when work remains blocked. A terminal backend error cannot rearm and therefore drains that lane up to the configured `request_capacity` hard cap so every accepted request receives authoritative terminal evidence. Callbacks never retain request pointers after their terminal completion.

- [x] **Step 4: Preserve cancellation and shutdown**

Cancellation transitions a pending record once and publishes `CFLOW_IO_COMPLETION_CANCELLED`. `forget_socket` rejects active requests, closes the persistent registration/duplicate, clears the socket slot, and returns `TURBO_ENOENT` for an unknown identity. Shutdown closes remaining quiescent registrations before reactor destruction.

- [x] **Step 5: Verify GREEN**

```powershell
cmake --build --preset win-release-user --target cflow_io_native_test cflow_io_actor_test
ctest --preset win-release-user -R "cflow_io_(native|actor)_test" --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add cflow/include/cflow/io_native.h cflow/src/io_native_readiness.c cflow/tests/cflow_io_native_test.c docs/superpowers/plans/2026-08-25-cflow-single-owner-readiness.md
git commit -m "perf(cflow): make readiness sockets persistent"
```

---

### Task 3: Verify CPU, syscalls, and cross-platform compatibility

**Files:**
- Modify only if measurement schema needs existing counters: `cflow/benchmarks/cflow_network_benchmark.c`
- Generated, untracked remote evidence: `diagnostics/io-scheduler-89bcd81/`

**Interfaces:**
- Consumes: existing `CFLOW_BENCHMARK_JSON` schema and backend selection environment variables.
- Produces: before/after wall time, process CPU cores, throughput, P99, fd lifecycle syscall counts, and focused regression output.

- [x] **Step 1: Run Windows compile and focused tests**

Use `win-release-user` from a VS developer environment. This verifies the shared code and confirms IOCP still compiles unchanged.

- [x] **Step 2: Run Linux focused tests in the remote worktree**

Build and run `cflow_io_actor_test` and `cflow_io_native_test` with `linux-release-user`.

- [x] **Step 3: Re-run the paired epoll benchmark**

Use identical TCP latency workloads for blocking and busy modes. Redirect output to files and compare `cpu_core_equivalents`, application throughput, and P99.

- [x] **Step 4: Re-run syscall counts**

Run reduced `strace -f -c` samples. Expected behavioral proof: `fcntl` and `close` scale with sockets instead of operations; tracing time is excluded from performance conclusions.

- [x] **Step 5: Check repository state and diff**

```bash
git diff --check
git status --short
```

- [ ] **Step 6: Push the existing feature branch**

Push only after fresh test and benchmark evidence, then update PR #95 with measured before/after results and remaining limitations.

## Measurement Summary

The paired Linux epoll run used the same host and 51,200 TCP exchanges for each before/after sample. The blocking policy improved wall time from 25.90 s to 20.36 s (-21.4%), process CPU time from 22.79 s to 21.84 s (-4.2%), and throughput from 0.1206 MiB/s to 0.1535 MiB/s (+27.2%). Its CPU-core equivalent rose from 0.880 to 1.073 because wall time fell faster than CPU time; this metric is sensitive to host scheduling and is not evidence of additional work by itself.

The busy policy improved wall time from 24.90 s to 18.78 s (-24.6%), process CPU time from 44.01 s to 32.30 s (-26.6%), throughput from 0.1255 MiB/s to 0.1664 MiB/s (+32.6%), and CPU-core equivalent from 1.768 to 1.720. A reduced `strace -f -c` sample showed six `fcntl` calls and nine `close` calls for 1,280 exchanges, replacing the previous per-operation descriptor churn. Trace timing is excluded from latency and throughput conclusions.

The retained design intentionally costs up to two duplicate descriptors and two Platform registrations per bidirectional socket. The Lean readiness model still covers one-shot slot safety and legacy callback reentry. Neither Lean nor the current C state projection represents callback-return continuation transitions; the C helper checks concrete callback-form exclusivity, while fake/native contract tests cover continuation behavior and deterministic close/unarm/shutdown races.
